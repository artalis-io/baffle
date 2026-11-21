#include <baffle.h>
int main(void)
{
    bf_library_info i = BF_INIT(bf_library_info);
    return bf_library_query(&i) != BF_OK;
}
