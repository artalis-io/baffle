#include <baffle.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            abort();                                                                               \
        }                                                                                          \
    } while (0)
int main(void)
{
    bf_library_info i = BF_INIT(bf_library_info);
    CHECK(bf_library_query(&i) == BF_OK && i.major == 0);
    CHECK(bf_library_query(NULL) == BF_ERROR_INVALID);
    i.struct_version = 99;
    CHECK(bf_library_query(&i) == BF_ERROR_INVALID);
    puts("foundation conformance passed");
    return 0;
}
