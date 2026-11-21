#include <baffle.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    struct {
        bf_library_info info;
        unsigned char tail[32];
    } extended;
    memset(&extended, 0xa5, sizeof(extended));
    extended.info = (bf_library_info)BF_INIT(bf_library_info);
    extended.info.struct_size = sizeof(extended);
    CHECK(bf_library_query(&extended.info) == BF_OK &&
          extended.info.struct_size == sizeof(extended));
    for (unsigned n = 0; n < sizeof(extended.tail); ++n)
        CHECK(extended.tail[n] == 0xa5);
    i.struct_size = sizeof(i) - 1;
    CHECK(bf_library_query(&i) == BF_ERROR_INVALID);
    i.struct_size = sizeof(i);
    i.struct_version = 99;
    CHECK(bf_library_query(&i) == BF_ERROR_INVALID);
    puts("foundation conformance passed");
    return 0;
}
