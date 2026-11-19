#include <baffle.h>
#include <string.h>
bf_result bf_library_query(bf_library_info *info)
{
    bf_library_info value = BF_INIT(bf_library_info);
    if (!info || info->struct_size < sizeof(*info) || info->struct_version != BF_STRUCT_VERSION) return BF_ERROR_INVALID;
    value.major=BF_VERSION_MAJOR; value.minor=BF_VERSION_MINOR; value.patch=BF_VERSION_PATCH;
    memcpy((unsigned char *)info+8,(const unsigned char *)&value+8,sizeof(value)-8);
    return BF_OK;
}
const char *bf_result_description(bf_result r)
{
    switch(r) {
    case BF_OK: return "success"; case BF_NEED_OUTPUT: return "output capacity required";
    case BF_ERROR_INVALID: return "invalid argument"; case BF_ERROR_CAPACITY: return "insufficient storage";
    case BF_ERROR_UNSUPPORTED: return "unsupported configuration"; case BF_ERROR_STATE: return "invalid state";
    case BF_ERROR_NONFINITE: return "nonfinite PCM"; case BF_ERROR_OVERFLOW: return "arithmetic overflow";
    default: return "unknown result";
    }
}
