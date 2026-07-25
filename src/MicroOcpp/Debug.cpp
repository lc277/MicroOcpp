// matth-x/MicroOcpp
// Copyright Matthias Akstaller 2019 - 2024
// MIT License

#include <string.h>

#include <MicroOcpp/Debug.h>

const char *level_label [] = {
    "",         //MO_DL_NONE 0x00
    "ERROR",    //MO_DL_ERROR 0x01
    "warning",  //MO_DL_WARN 0x02
    "info",     //MO_DL_INFO 0x03
    "debug",    //MO_DL_DEBUG 0x04
    "verbose"   //MO_DL_VERBOSE 0x05
};

#if MO_DBG_FORMAT == MO_DF_MINIMAL
void mo_dbg_print_prefix(int level, const char *fn, int line) {
    (void)0;
}

#elif MO_DBG_FORMAT == MO_DF_COMPACT
void mo_dbg_print_prefix(int level, const char *fn, int line) {
        size_t l = strlen(fn);
        size_t r = l;
        while (l > 0 && fn[l-1] != '/' && fn[l-1] != '\\') {
            l--;
            if (fn[l] == '.') r = l;
        }
        MO_CONSOLE_PRINTF("%.*s:%i ", (int) (r - l), fn + l, line);
}

#elif MO_DBG_FORMAT == MO_DF_FILE_LINE
void mo_dbg_print_prefix(int level, const char *fn, int line) {
    size_t l = strlen(fn);
    while (l > 0 && fn[l-1] != '/' && fn[l-1] != '\\') {
        l--;
    }
        /*
        * BLACK    30
        * RED      31
        * GREEN    32
        * YELLOW   33
        * BLUE     34
        * PURPLE   35
        * CYAN     36
        * WHITE    37
    */
    const char *color[] = {
        "\033[0m",  // MO_DL_NONE 0x00
        "\033[31m", // MO_DL_ERROR 0x01
        "\033[33m", // MO_DL_WARN 0x02
        "\033[34m", // MO_DL_INFO 0x03
        "\033[35m", // MO_DL_DEBUG 0x04
        "\033[36m"  // MO_DL_VERBOSE 0x05
    };
    // MO_CONSOLE_PRINTF("%s", color[level]);
    MO_CONSOLE_PRINTF("[MO] %s (%s:%i): ", color[level], fn + l, line);
}

#elif MO_DBG_FORMAT == MO_DF_FULL
void mo_dbg_print_prefix(int level, const char *fn, int line) {
    MO_CONSOLE_PRINTF("[MO] %s (%s:%i): ", level_label[level], fn, line);
}

#else
#error invalid MO_DBG_FORMAT definition
#endif

void mo_dbg_print_suffix() {
    MO_CONSOLE_PRINTF("\033[0m\n");
}
