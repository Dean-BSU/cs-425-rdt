#include "rdt.h"
#include "io.h"
#include <stdio.h>
#include <string.h>

#ifdef TEST
#define main main_exclude
#endif

int main(int argc, char *argv[])
{
    rdt_config_t config;

    if (argc == 1) {
        rdt_print_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
                      strcmp(argv[1], "--help") == 0)) {
        rdt_print_usage(argv[0]);
        return 0;
    }
    if (rdt_parse_args(argc, argv, &config) != 0) {
        rdt_print_usage(argv[0]);
        return 1;
    }
    return rdt_run(&config) == 0 ? 0 : 2;
}