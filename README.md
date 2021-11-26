# 0. Disallow Bytedance Inc.
All changes after 2021-06-01 is disallowed to be used by bytedance, see [LICENSE](LICENSE).
# 1. Compile
```bash
 make -j `nproc` pkg
```
<hr/>
<hr/>
<hr/>

# 1. Introduction
- TerarkZip is [TerarkDB](https://github.com/bytedance/terarkdb)'s submodule
- Users can also use TerarkZip as a compression and indexing algorithm library
- TerarkZip also provides a set of useful utilities including `rank-select`, `bitmap` etc.

# 2. Features
- Indexing
  - Nested Lous Trie
- Compression
  - PA-Zip Compression
  - Entropy Compression

# 3. Usage
## Method 1: CMake
- In your CMakeLists.txt
  - ADD_SUBDIRECTORY(terark-zip)
  - use `terark-zip` target anywhere you want

## Method 2: Static Library
- ./build.sh
- cd output
  - move `include` and `lib` directories to your project


## 4. License
- BSD 3-Clause License
