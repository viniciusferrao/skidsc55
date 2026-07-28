/* cfg - read or change the configuration block inside a built SC15.DRV.
 *
 * The block sits behind an ASCII signature at a fixed offset so an installed
 * driver can be pointed at a different MPU-401 port, or have its resets and
 * master volume adjusted, without rebuilding it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sc15.h"

/* The ranges are the ones the driver actually honours, so what this reports
 * back is what the driver will do. Accepting anything a byte can hold did not:
 * a volume of 255 was stored and displayed as 255 while the driver clamped it
 * to 100, and a port of FFFFh wrapped to 0000h when the driver added one to
 * reach the status register. */
static const struct {
    const char *name;
    int         ofs;
    int         width;
    long        lo, hi;
    const char *help;
} field[] = {
    {"port", CFG_PORT, 2, 0, 0xFFFEL,
     "MPU-401 data port, status and command port is this plus one"},
    {"gm", CFG_GM, 1, 0, 1, "send GM System On at init"},
    {"gs", CFG_GS, 1, 0, 1, "send GS Reset at init, and enable bank select"},
    {"volume", CFG_VOLUME, 1, 0, 100, "initial master volume, 0 to 100"},
};
#define NFIELD (int)(sizeof field / sizeof field[0])

/* Ports are written in hex without a prefix, the way DOS documentation does.
 * Everything else is decimal. */
static long parse_value(const char *s, int hex)
{
    long v = 0;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        hex = 1;
    }
    if (*s == '\0') {
        return -1;
    }
    while (*s != '\0') {
        int d;
        if (*s >= '0' && *s <= '9') {
            d = *s - '0';
        } else if (hex && *s >= 'a' && *s <= 'f') {
            d = *s - 'a' + 10;
        } else if (hex && *s >= 'A' && *s <= 'F') {
            d = *s - 'A' + 10;
        } else {
            return -1;
        }
        v = v * (hex ? 16 : 10) + d;
        if (v > 0xFFFFL) {
            return -1;
        }
        s++;
    }
    return v;
}

int cmd_cfg(int argc, char **argv)
{
    struct blob drv;
    int         dirty = 0;
    int         i, f;

    if (argc < 1) {
        return fail("usage: sc15 cfg <SC15.DRV> [field=value ...]");
    }
    if (blob_load(&drv, argv[0]) != 0) {
        return 1;
    }
    /* CFG_VOLUME is the last byte of the block and is already an absolute
     * offset, so the file only has to reach past it. Adding CFG_OFS to it was
     * a bug: it demanded 150 bytes where 82 are needed, which no real driver
     * ever fell short of but any stub would. */
    if (drv.size < (unsigned long)CFG_VOLUME + 1 ||
        memcmp(drv.data + CFG_OFS, CFG_SIG, CFG_SIG_LEN) != 0) {
        blob_free(&drv);
        return fail("%s has no " CFG_SIG " signature, is it an SC15.DRV?",
                    argv[0]);
    }

    for (i = 1; i < argc; i++) {
        char   name[32];
        char  *eq = strchr(argv[i], '=');
        long   v;
        size_t len;

        if (eq == NULL) {
            blob_free(&drv);
            return fail("expected field=value, got %s", argv[i]);
        }
        len = (size_t)(eq - argv[i]);
        if (len == 0 || len >= sizeof name) {
            blob_free(&drv);
            return fail("bad field name in %s", argv[i]);
        }
        memcpy(name, argv[i], len);
        name[len] = '\0';

        for (f = 0; f < NFIELD; f++) {
            if (same_word(name, field[f].name)) {
                break;
            }
        }
        if (f == NFIELD) {
            blob_free(&drv);
            return fail("unknown field %s", name);
        }
        v = parse_value(eq + 1, field[f].width == 2);
        if (v < field[f].lo || v > field[f].hi) {
            blob_free(&drv);
            return fail("%s must be %ld to %ld, not %s", field[f].name,
                        field[f].lo, field[f].hi, eq + 1);
        }
        if (field[f].width == 2) {
            wr16(drv.data + field[f].ofs, (unsigned int)v);
        } else {
            drv.data[field[f].ofs] = (unsigned char)v;
        }
        dirty = 1;
    }

    if (dirty && blob_save(&drv, argv[0]) != 0) {
        blob_free(&drv);
        return 1;
    }

    printf("%s%s\n", argv[0], dirty ? " (updated)" : "");
    for (f = 0; f < NFIELD; f++) {
        unsigned int v = field[f].width == 2 ? rd16(drv.data + field[f].ofs)
                                             : drv.data[field[f].ofs];
        if (field[f].width == 2) {
            printf("  %-7s %-6X at %02Xh   %s\n", field[f].name, v,
                   field[f].ofs, field[f].help);
        } else {
            printf("  %-7s %-6u at %02Xh   %s\n", field[f].name, v,
                   field[f].ofs, field[f].help);
        }
    }
    blob_free(&drv);
    return 0;
}
