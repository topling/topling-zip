// unit test for NestTrieDAWG & NestLoudsTrieTpl object lifecycle:
//   1. copy cons must deep copy layer data (m_layer_id_rank/m_layer_ref),
//      dict_rank and Iterator depend on it
//   2. operator= must not double free (m_layer_id_rank owns its memory)
//   3. NestTrieDAWG::swap must swap m_cache & m_zpNestLevel along with m_trie
//   4. build_fsa_cache can be called multiple times (old cache is deleted)
//   5. mem_size must count both core and nested trie of a mixed trie
//   6. save_mmap/load_mmap round trip keeps full functionality
// and functional coverage:
//   - lower_bound & iterator seek_lower_bound on hit/miss/boundary probes
//   - match_dawg on prefix chains
//   - conf.commonPrefix(multi-fragment in debug, mmap format version 2)
//   - short keys without any zpath(HasLink=false paths) + the empty key
//   - 256 way root fan-out(FastLabel bitmap label format)
//   - get_random_keys_append, self assignment
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#define _SCL_SECURE_NO_WARNINGS
#endif

#include <terark/fsa/nest_trie_dawg.hpp>
#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>

#if !defined(_MSC_VER)
#include <unistd.h> // getpid
#endif

using namespace terark;

// keys = hex(i) + 2 shared fragments + random tail:
// shared fragments are high freq, so they go to core str pool,
// random tails are long, so nesting is triggered ==> mixed trie
static std::vector<std::string>
make_keys(size_t num, size_t seed) {
	static const char* frags[8] = {
		"-alpha/bravo/charlie", "-delta/echo/foxtrot-",
		"-golf/hotel/india-00", "-juliett/kilo/lima-1",
		"-mike/november/oscar", "-papa/quebec/romeo-2",
		"-sierra/tango/unifo-", "-victor/whiskey/xray",
	};
	std::mt19937_64 rnd(seed);
	std::vector<std::string> keys;
	keys.reserve(num);
	for (size_t i = 0; i < num; ++i) {
		char hex[16];
		snprintf(hex, sizeof(hex), "%08zx", i);
		std::string k = hex;
		k += frags[rnd() % 8];
		k += frags[rnd() % 8];
		for (size_t j = 0; j < 16; ++j)
			k += char('a' + rnd() % 26);
		keys.push_back(std::move(k));
	}
	std::sort(keys.begin(), keys.end());
	keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
	return keys;
}

// all bytes 0x00-0xFF as the first byte: the root fan-out is 256, covering
// the FastLabel bitmap label format(fan-out >= 36) in search and iterator
static std::vector<std::string>
make_wide_fanout_keys(size_t per_byte, size_t seed) {
	std::mt19937_64 rnd(seed);
	std::vector<std::string> keys;
	keys.reserve(per_byte * 256);
	for (size_t b = 0; b < 256; ++b) {
		for (size_t i = 0; i < per_byte; ++i) {
			std::string k;
			k += char(b);
			for (size_t j = 0; j < 12; ++j)
				k += char('a' + rnd() % 26);
			keys.push_back(std::move(k));
		}
	}
	std::sort(keys.begin(), keys.end());
	keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
	return keys;
}

// the empty key + all short keys: no fragment reaches minLinkStrLen, so
// the trie has no zpath at all, covering the HasLink=false code paths
static std::vector<std::string>
make_short_keys() {
	std::vector<std::string> keys;
	keys.push_back("");
	for (char c = 'a'; c <= 'z'; ++c)
		keys.push_back(std::string(1, c));
	for (char c1 = 'a'; c1 <= 'z'; ++c1)
		for (char c2 = 'a'; c2 <= 'z'; ++c2)
			keys.push_back(std::string(1, c1) + std::string(1, c2));
	std::sort(keys.begin(), keys.end()); // "aa" < "z": generation order is unsorted
	return keys;
}

template<class Dawg>
static void build_dawg(Dawg& dawg, const std::vector<std::string>& keys,
                       const char* commonPrefix = "") {
	SortableStrVec strVec;
	for (const std::string& k : keys)
		strVec.push_back(k);
	NestLoudsTrieConfig conf;
	conf.isInputSorted = true;
	conf.commonPrefix = commonPrefix;
	dawg.build_from(strVec, conf);
}

// check lower_bound & iterator seek_lower_bound on a probe which may miss,
// the expectation is computed independently by std::lower_bound
template<class Dawg>
static void verify_lower_bound_probe(const Dawg& dawg, ADFA_LexIterator* iter,
                                     const std::vector<std::string>& keys,
                                     const std::string& probe) {
	size_t exp = std::lower_bound(keys.begin(), keys.end(), probe)
	           - keys.begin();
	MatchContext ctx;
	size_t lb_idx = size_t(-1), dict_rank = size_t(-1);
	dawg.lower_bound(ctx, probe, &lb_idx, &dict_rank);
	TERARK_VERIFY_EQ(dict_rank, exp);
	// KNOWN ISSUE: the `index` output of lower_bound is not verified on
	// miss(it may skip words at shallower levels), it has no consumer
	// (nlt_index.cc only consumes dict_rank)
	if (exp < keys.size()) {
		TERARK_VERIFY(iter->seek_lower_bound(probe));
		TERARK_VERIFY(iter->word() == fstring(keys[exp]));
	}
	else {
		TERARK_VERIFY(!iter->seek_lower_bound(probe));
	}
}

// full functional check: index, nth_word, dict_rank(lower_bound),
// dict_rank_to_state/state_to_dict_rank round trip, bidirectional iterator,
// iterator seek_lower_bound(hit and miss), random keys restore.
// dict_rank & Iterator require intact layer data(m_layer_id_rank/m_layer_ref)
template<class Dawg>
static void verify_dawg(const Dawg& dawg, const std::vector<std::string>& keys) {
	TERARK_VERIFY_EQ(dawg.num_words(), keys.size());
	ADFA_LexIteratorUP iter(dawg.adfa_make_iter());
	std::string word;
	for (size_t i = 0; i < keys.size(); ++i) {
		fstring key = keys[i];
		size_t idx = dawg.index(key);
		TERARK_VERIFY_NE(idx, BaseDAWG::null_word);
		dawg.nth_word(idx, &word);
		TERARK_VERIFY(fstring(word) == key);
		MatchContext ctx;
		size_t lb_idx = size_t(-1), dict_rank = size_t(-1);
		dawg.lower_bound(ctx, key, &lb_idx, &dict_rank);
		TERARK_VERIFY_EQ(lb_idx, idx);
		TERARK_VERIFY_EQ(dict_rank, i); // keys are sorted ascending
		size_t state = dawg.dict_rank_to_state(i);
		TERARK_VERIFY_EQ(dawg.state_to_dict_rank(state), i);
		TERARK_VERIFY(iter->seek_lower_bound(key)); // exact hit
		TERARK_VERIFY(iter->word() == key);
	}
	TERARK_VERIFY_EQ(dawg.index("~~~non-existent-key~~~"), BaseDAWG::null_word);
	// miss probes: below the first key, above the last key, between keys,
	// and a proper prefix of an existing key.
	// the empty probe word: dict_rank must be 0. the key path is probing ""
	// when the trie contains the empty word(keysShort, with fsa cache): the
	// root term must be counted in at the cache branch entry of lower_bound,
	// then rank=1 and dec=1 cancel out, without that count-in dict_rank
	// wraps to size_t(-1)
	verify_lower_bound_probe(dawg, iter.get(), keys, std::string());
	verify_lower_bound_probe(dawg, iter.get(), keys, keys.back() + "\x01");
	for (size_t i = 0; i < keys.size(); i += 7) {
		verify_lower_bound_probe(dawg, iter.get(), keys, keys[i] + "\x01");
		if (size_t len = keys[i].size()) {
			verify_lower_bound_probe(dawg, iter.get(), keys,
			                         keys[i].substr(0, len - 1));
		}
	}
	bool ok = iter->seek_begin();
	for (size_t i = 0; i < keys.size(); ++i) {
		TERARK_VERIFY(ok);
		TERARK_VERIFY(iter->word() == fstring(keys[i]));
		ok = iter->incr();
	}
	TERARK_VERIFY(!ok);
	ok = iter->seek_end();
	for (size_t i = keys.size(); i-- > 0; ) {
		TERARK_VERIFY(ok);
		TERARK_VERIFY(iter->word() == fstring(keys[i]));
		ok = iter->decr();
	}
	TERARK_VERIFY(!ok);
	// get_random_keys_append restores words, seq_id is the word id
	SortableStrVec rand_keys;
	dawg.get_random_keys_append(&rand_keys, 100);
	TERARK_VERIFY_EQ(rand_keys.size(), 100);
	for (size_t i = 0; i < rand_keys.size(); ++i) {
		TERARK_VERIFY_EQ(dawg.index(rand_keys[i]), rand_keys.m_index[i].seq_id);
	}
}

template<class Dawg>
static void test_copy_and_assign(const std::vector<std::string>& keysA,
                                 const std::vector<std::string>& keysB) {
	Dawg d1;
	build_dawg(d1, keysA);
	verify_dawg(d1, keysA);

	Dawg d2(d1); // copy cons must deep copy layer data
	verify_dawg(d2, keysA);
	TERARK_VERIFY_EQ(d2.zp_nest_level(), d1.zp_nest_level());
	// copy allocates tight, the original may keep build-time slack capacity
	TERARK_VERIFY_GT(d2.mem_size(), 0);
	TERARK_VERIFY_LE(d2.mem_size(), d1.mem_size());

	Dawg d3;
	build_dawg(d3, keysB);
	d3 = d1; // assign over an already built object: must not leak/double-free
	verify_dawg(d3, keysA);
	TERARK_VERIFY_EQ(d3.zp_nest_level(), d1.zp_nest_level());

	Dawg& ref = d3; // self assign via a reference
	d3 = ref;
	verify_dawg(d3, keysA);
}

// match_dawg fires on_match for every word which is a prefix of the probe
template<class Dawg>
static void test_match_dawg() {
	const std::string base = "match/dawg/prefix/chain";
	std::vector<std::string> keys;
	for (size_t len = 1; len <= base.size(); ++len)
		keys.push_back(base.substr(0, len));
	for (char c = '0'; c <= '9'; ++c) // some noise keys
		keys.push_back(std::string("noise-") + c);
	std::sort(keys.begin(), keys.end());
	Dawg dawg;
	build_dawg(dawg, keys);
	verify_dawg(dawg, keys);
	const std::string probe = base + "/tail-not-a-word";
	std::vector<std::pair<size_t, size_t> > matches; // (len, word_id)
	size_t match_len = dawg.match_dawg(probe,
		[&](size_t len, size_t nth) { matches.emplace_back(len, nth); });
	TERARK_VERIFY_GE(match_len, base.size());
	TERARK_VERIFY_EQ(matches.size(), base.size()); // every prefix is a word
	for (auto& m : matches) {
		TERARK_VERIFY_EQ(m.second, dawg.index(probe.substr(0, m.first)));
	}
	// a probe with no matched prefix at all
	matches.clear();
	dawg.match_dawg("~~~", [&](size_t len, size_t nth) {
		matches.emplace_back(len, nth);
	});
	TERARK_VERIFY_EQ(matches.size(), 0);
}

// input keys do not contain conf.commonPrefix, but the built dawg serves
// full keys(prefix + key), covering writePrefixFrag/patchNestStrVec and
// the common prefix support in the mmap format(version 2)
template<class Dawg>
static void test_common_prefix(const std::vector<std::string>& keys) {
	// longer than debug MAX_FRAG(6) to cover multi-fragment prefix
	const std::string prefix = "common-prefix/0123456789/abcdefghijklm/";
	std::vector<std::string> fullKeys(keys.size());
	for (size_t i = 0; i < keys.size(); ++i)
		fullKeys[i] = prefix + keys[i];
	Dawg d1;
	build_dawg(d1, keys, prefix.c_str());
	verify_dawg(d1, fullKeys);
	char fpath[128];
#if defined(_MSC_VER)
	snprintf(fpath, sizeof(fpath), "nest_trie_dawg_test_cp.nlt");
#else
	snprintf(fpath, sizeof(fpath), "/tmp/nest_trie_dawg_test_cp-%d.nlt", (int)getpid());
#endif
	d1.save_mmap(fstring(fpath));
	{
		std::unique_ptr<MatchingDFA> dfa(MatchingDFA::load_mmap(fpath));
		auto d2 = dynamic_cast<Dawg*>(dfa.get());
		TERARK_VERIFY(NULL != d2);
		verify_dawg(*d2, fullKeys);
	}
	::remove(fpath);
}

// A one-byte tail is stored directly in label rather than nestStrVec. Cover
// MAX_FRAG + 1 so patchNestStrVec must not reserve that byte in the core pool.
template<class Dawg>
static void test_common_prefix_label_only_tail() {
	const size_t maxFrag = TERARK_IF_DEBUG(6, 253);
	const std::string prefix(maxFrag + 1, 'p');
	const std::vector<std::string> keys = {
		"aaaaaaaaaaaa", "aaaaaaaabbbb", "aaaaaaaacccc",
	};
	std::vector<std::string> fullKeys(keys.size());
	for (size_t i = 0; i < keys.size(); ++i)
		fullKeys[i] = prefix + keys[i];
	Dawg dawg;
	build_dawg(dawg, keys, prefix.c_str());
	verify_dawg(dawg, fullKeys);
}

template<class Dawg>
static void test_swap_and_fsa_cache(const std::vector<std::string>& keysA,
                                    const std::vector<std::string>& keysB) {
	Dawg d1, d2;
	build_dawg(d1, keysA);
	build_dawg(d2, keysB);
	TERARK_VERIFY(d1.build_fsa_cache(0.5, NULL));
	TERARK_VERIFY(!d2.has_fsa_cache());
	size_t nl1 = d1.zp_nest_level(), nl2 = d2.zp_nest_level();
	d1.swap(d2);
	TERARK_VERIFY_EQ(d1.zp_nest_level(), nl2);
	TERARK_VERIFY_EQ(d2.zp_nest_level(), nl1);
	TERARK_VERIFY(!d1.has_fsa_cache());
	TERARK_VERIFY(d2.has_fsa_cache());
	verify_dawg(d1, keysB); // m_cache must follow its m_trie,
	verify_dawg(d2, keysA); // otherwise queries go through a wrong cache
	// build cache multi times: old cache must be deleted, not leaked
	TERARK_VERIFY(d1.build_fsa_cache(0.3, "BFS"));
	TERARK_VERIFY(d1.build_fsa_cache(0.6, "CFS"));
	verify_dawg(d1, keysB);
	d1.swap(d2); // swap back, both have cache now
	verify_dawg(d1, keysA);
	verify_dawg(d2, keysB);
}

template<class Dawg>
static void test_save_load(const std::vector<std::string>& keysA) {
	Dawg d1;
	build_dawg(d1, keysA);
	char fpath[128];
#if defined(_MSC_VER)
	snprintf(fpath, sizeof(fpath), "nest_trie_dawg_test.nlt");
#else
	snprintf(fpath, sizeof(fpath), "/tmp/nest_trie_dawg_test-%d.nlt", (int)getpid());
#endif
	d1.save_mmap(fstring(fpath));
	{
		std::unique_ptr<MatchingDFA> dfa(MatchingDFA::load_mmap(fpath));
		auto d2 = dynamic_cast<Dawg*>(dfa.get());
		TERARK_VERIFY(NULL != d2);
		verify_dawg(*d2, keysA);
	}
	::remove(fpath);
}

// ---- NestLoudsTrieTpl level tests ------------------------------------

template<class Trie>
static size_t sum_mem_size(const Trie* trie) {
	size_t sum = trie->m_louds.mem_size()
	           + trie->m_is_link.mem_size()
	           + trie->m_next_link.mem_size()
	           + trie->total_states() // m_label_data
	           + trie->m_core_size;
	if (trie->m_next_trie)
		sum += sum_mem_size(trie->m_next_trie);
	return sum;
}

template<class Trie>
static bool has_mixed_level(const Trie* trie) {
	if (trie->m_core_size && trie->m_next_trie)
		return true;
	return trie->m_next_trie && has_mixed_level(trie->m_next_trie);
}

static void test_trie_level(const std::vector<std::string>& keysA,
                            const std::vector<std::string>& keysB) {
	typedef NestLoudsTrie_SE_256_32_FL Trie;
	typedef Trie::rank_select_t rank_select_t;
	NestLoudsTrieConfig conf;
	conf.isInputSorted = true;
	auto build = [&conf](Trie& trie, rank_select_t& is_term,
	                     valvec<size_t>& termNodes,
	                     const std::vector<std::string>& keys) {
		SortableStrVec strVec;
		for (const std::string& k : keys)
			strVec.push_back(k);
		auto buildTerm = [&](const valvec<size_t>& linkVec) {
			is_term.resize_fill(trie.m_is_link.size(), false);
			for (size_t node_id : linkVec)
				is_term.set1(node_id);
			is_term.build_cache(0, 1);
			termNodes.assign(linkVec);
		};
		trie.build_patricia(strVec, buildTerm, conf);
		trie.init_for_term(is_term);
	};
	auto verify = [](const Trie& trie, const valvec<size_t>& termNodes,
	                 const std::vector<std::string>& keys) {
		std::string word;
		for (size_t i = 0; i < keys.size(); ++i) {
			trie.restore_dawg_string(termNodes[i], &word);
			TERARK_VERIFY(fstring(word) == fstring(keys[i]));
		}
	};
	Trie t1;
	rank_select_t is_term1;
	valvec<size_t> termNodes1;
	build(t1, is_term1, termNodes1, keysA);
	verify(t1, termNodes1, keysA);

	// the test keys must trigger the mixed case(core + nested trie),
	// in which mem_size must count both core and nested trie
	TERARK_VERIFY(has_mixed_level(&t1));
	TERARK_VERIFY_EQ(t1.mem_size(), sum_mem_size(&t1));

	// copy cons must copy layer data with the init_for_term layout
	Trie t2(t1);
	size_t layer_num = t1.m_layer_id_rank.size();
	TERARK_VERIFY_GT(layer_num, 0);
	TERARK_VERIFY_EQ(t2.m_layer_id_rank.size(), layer_num);
	TERARK_VERIFY((void*)t2.m_layer_ref == (void*)t2.m_layer_id_rank.end());
	TERARK_VERIFY_EZ(memcmp(t2.m_layer_id_rank.data(), t1.m_layer_id_rank.data(),
	                        sizeof(Trie::layer_id_rank_t) * layer_num));
	TERARK_VERIFY_EZ(memcmp(t2.m_layer_ref, t1.m_layer_ref,
	                        sizeof(Trie::layer_ref_t) * layer_num));
	verify(t2, termNodes1, keysA);
	for (size_t i = 0; i < keysA.size(); i += 97) {
		size_t node_id = termNodes1[i];
		TERARK_VERIFY_EQ(t2.state_to_dict_rank(node_id, is_term1),
		                 t1.state_to_dict_rank(node_id, is_term1));
	}

	// assign over an already built trie: the old impl double-freed
	// m_layer_id_rank(dtor + clear() on the destructed object)
	Trie t3;
	rank_select_t is_term3;
	valvec<size_t> termNodes3;
	build(t3, is_term3, termNodes3, keysB);
	t3 = t1;
	// t3 is a tight copy, mem_size must still equal the sum of its parts
	TERARK_VERIFY_EQ(t3.mem_size(), sum_mem_size(&t3));
	TERARK_VERIFY_LE(t3.mem_size(), t1.mem_size());
	TERARK_VERIFY_EQ(t3.m_layer_id_rank.size(), layer_num);
	verify(t3, termNodes1, keysA);
}

// no zpath(HasLink=false paths) + the empty key; also with fsa cache
template<class Dawg>
static void test_short_keys(const std::vector<std::string>& keysShort) {
	Dawg dawg;
	build_dawg(dawg, keysShort);
	verify_dawg(dawg, keysShort);
	TERARK_VERIFY(dawg.build_fsa_cache(0.9, NULL));
	verify_dawg(dawg, keysShort);
}

// root fan-out is 256: FastLabel bitmap label format, also copy it
template<class Dawg>
static void test_wide_fanout(const std::vector<std::string>& keysWide) {
	Dawg d1;
	build_dawg(d1, keysWide);
	verify_dawg(d1, keysWide);
	Dawg d2(d1);
	verify_dawg(d2, keysWide);
}

template<class Dawg>
static void test_one_dawg_type(const std::vector<std::string>& keysA,
                               const std::vector<std::string>& keysB,
                               const std::vector<std::string>& keysShort,
                               const std::vector<std::string>& keysWide) {
	test_copy_and_assign<Dawg>(keysA, keysB);
	test_swap_and_fsa_cache<Dawg>(keysA, keysB);
	test_save_load<Dawg>(keysA);
	test_match_dawg<Dawg>();
	test_common_prefix<Dawg>(keysB);
	test_common_prefix_label_only_tail<Dawg>();
	test_short_keys<Dawg>(keysShort);
	test_wide_fanout<Dawg>(keysWide);
}

int main() {
	std::vector<std::string> keysA = make_keys(3000, 20260820);
	std::vector<std::string> keysB = make_keys(2000, 42);
	std::vector<std::string> keysShort = make_short_keys();
	std::vector<std::string> keysWide = make_wide_fanout_keys(8, 7);

	test_trie_level(keysA, keysB);

	// cover: rank-select-mixed(is_term is a view into trie) + FastLabel
	test_one_dawg_type<NestLoudsTrieDAWG_Mixed_XL_256_32_FL>(keysA, keysB, keysShort, keysWide);
	// cover: separate is_term rank-select + FastLabel + 64 bit index
	test_one_dawg_type<NestLoudsTrieDAWG_SE_512_64_FL>(keysA, keysB, keysShort, keysWide);
	// cover: FastLabel = false (label_first_byte search path)
	test_one_dawg_type<NestLoudsTrieDAWG_IL_256>(keysA, keysB, keysShort, keysWide);

	printf("%s: all passed\n", __FILE__);
	return 0;
}
