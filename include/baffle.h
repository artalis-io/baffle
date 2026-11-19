#ifndef BF_BAFFLE_H
#define BF_BAFFLE_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32) && !defined(BF_STATIC)
#if defined(BF_BUILDING_LIBRARY)
#define BF_API __declspec(dllexport)
#else
#define BF_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define BF_API __attribute__((visibility("default")))
#else
#define BF_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define BF_VERSION_MAJOR 0u
#define BF_VERSION_MINOR 1u
#define BF_VERSION_PATCH 0u
#define BF_STRUCT_VERSION 1u
#ifdef __cplusplus
#define BF_INIT(type) []() { type bf_value = {}; bf_value.struct_size=(uint32_t)sizeof(type); bf_value.struct_version=BF_STRUCT_VERSION; return bf_value; }()
#else
#define BF_INIT(type) { .struct_size=(uint32_t)sizeof(type), .struct_version=BF_STRUCT_VERSION }
#endif
typedef int32_t bf_result;
#define BF_OK ((bf_result)0)
#define BF_NEED_OUTPUT ((bf_result)1)
#define BF_ERROR_INVALID ((bf_result)-1)
#define BF_ERROR_CAPACITY ((bf_result)-2)
#define BF_ERROR_UNSUPPORTED ((bf_result)-3)
#define BF_ERROR_STATE ((bf_result)-4)
#define BF_ERROR_NONFINITE ((bf_result)-5)
#define BF_ERROR_OVERFLOW ((bf_result)-6)
typedef struct bf_library_info {
    uint32_t struct_size, struct_version;
    uint32_t major, minor, patch, features;
} bf_library_info;
/* Borrowed pointers last only for a call. Records may have larger unknown tails;
 * version 1 required prefixes may not be shortened. ABI zero is provisional. */
BF_API bf_result bf_library_query(bf_library_info *info);
BF_API const char *bf_result_description(bf_result result);
#ifdef __cplusplus
}
#endif
#endif
