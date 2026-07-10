/*
 * print.c — Manios Plugin: print() nhu Python
 *
 * Compile: gcc -shared -fPIC -I/usr/local/share/manios/include -o print.so print.c
 * Dung:    print("hello")        → hello
 *          print("a", 1, true)   → a 1 true
 *          print(3.14)           → 3.14
 */

#include "mnos_ext.h"
#include <stdio.h>

static Val np_print(Val *a, int n) {
    for (int i = 0; i < n; i++) {
        if (i > 0) fprintf(stderr, " ");

        switch (a[i].type) {
            case V_NONE:  fprintf(stderr, "none"); break;
            case V_INT:   fprintf(stderr, "%lld", (long long)a[i].ival); break;
            case V_FLOAT: fprintf(stderr, "%g", a[i].fval); break;
            case V_STR:   fprintf(stderr, "%s", a[i].sval); break;
            case V_BOOL:  fprintf(stderr, "%s", a[i].bval ? "true" : "false"); break;
            case V_LIST: {
                fprintf(stderr, "[");
                for (int j = 0; j < (int)a[i].llen; j++) {
                    if (j > 0) fprintf(stderr, ", ");
                    Val v = a[i].li[j];
                    switch (v.type) {
                        case V_STR:  fprintf(stderr, "\"%s\"", v.sval); break;
                        case V_INT:  fprintf(stderr, "%lld", (long long)v.ival); break;
                        case V_FLOAT: fprintf(stderr, "%g", v.fval); break;
                        case V_BOOL: fprintf(stderr, "%s", v.bval ? "true" : "false"); break;
                        case V_NONE: fprintf(stderr, "none"); break;
                        default: fprintf(stderr, "?"); break;
                    }
                }
                fprintf(stderr, "]");
                break;
            }
            default: fprintf(stderr, "?"); break;
        }
    }
    fprintf(stderr, "\n");
    return val_none();
}

MNOS_EXT_BEGIN(print_cmd)
    MNOS_EXT_FUNC("print", np_print, "In noi dung ra man hinh (nhu Python)")
MNOS_EXT_END
