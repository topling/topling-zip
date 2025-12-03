#pragma once
#include <cstdint> // int64_t, uint64_t
#include <map> // map
#include <memory> // allocator
#include <string> // string
#include <vector> // vector

#ifndef SIDE_PLUGIN_JSON_USE_STD_MAP
#include <terark/gold_hash_map.hpp>

namespace terark {

#define JsonStrMap_Base gold_hash_map<Key, Val \
    , std::hash<Key>, std::equal_to<Key> \
    , node_layout<std::pair<Key, Val>, unsigned, SafeCopy, ValueOut> \
    >
template<class Key, class Val, class IgnoreLess, class IgnoreAlloc>
class JsonStrMap : public JsonStrMap_Base {
public:
    typedef IgnoreAlloc allocator_type;
    using JsonStrMap_Base::gold_hash_map;
    using iterator = typename JsonStrMap_Base::iterator;
    iterator erase(iterator iter) { return this->std_erase(iter); }
    size_t   erase(const Key& key) { return JsonStrMap_Base::erase(key); }
};
} // namespace terark

#endif // #ifndef SIDE_PLUGIN_JSON_USE_STD_MAP

/*!
@brief namespace for Niels Lohmann
@see https://github.com/topling
@since version 1.0.0
*/
namespace topling
{
/*!
@brief default JSONSerializer template argument

This serializer ignores the template arguments and uses ADL
([argument-dependent lookup](https://en.cppreference.com/w/cpp/language/adl))
for serialization.
*/
template<typename T = void, typename SFINAE = void>
struct adl_serializer;

template<template<typename U, typename V, typename... Args> class ObjectType =
#ifdef SIDE_PLUGIN_JSON_USE_STD_MAP
         std::map,
#else
         terark::JsonStrMap,
#endif
         template<typename U, typename... Args> class ArrayType = std::vector,
         class StringType = std::string, class BooleanType = bool,
         class NumberIntegerType = std::int64_t,
         class NumberUnsignedType = std::uint64_t,
         class NumberFloatType = double,
         template<typename U> class AllocatorType = std::allocator,
         template<typename T, typename SFINAE = void> class JSONSerializer =
         adl_serializer,
         class BinaryType = std::vector<std::uint8_t>>
class basic_json;

/*!
@brief JSON Pointer

A JSON pointer defines a string syntax for identifying a specific value
within a JSON document. It can be used with functions `at` and
`operator[]`. Furthermore, JSON pointers are the base for JSON patches.

@sa [RFC 6901](https://tools.ietf.org/html/rfc6901)

@since version 2.0.0
*/
template<typename BasicJsonType>
class json_pointer;

/*!
@brief default JSON class

This type is the default specialization of the @ref basic_json class which
uses the standard template types.

@since version 1.0.0
*/
#ifndef SIDE_PLUGIN_JSON_USE_STD_MAP
typedef basic_json<terark::JsonStrMap> json;
/*
typedef basic_json<terark::JsonStrMap> JsonBase;
class json : public JsonBase {
public:
    using JsonBase::JsonBase;
};
*/
#else
using json = basic_json<>;
#endif

}  // namespace topling

