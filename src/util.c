#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sc15.h"

int fail(const char *fmt, ...)
{
    va_list ap;

    fputs("sc15: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    return 1;
}

/* Read a whole file. Everything this tool opens is a couple of kilobytes, so
 * the length is checked against a ceiling that a 16-bit host can still
 * allocate rather than being handled in pieces. */
#define BLOB_MAX 60000UL

int blob_load(struct blob *b, const char *path)
{
    FILE  *f;
    long   len;
    size_t got;

    b->data = NULL;
    b->size = 0;

    f = fopen(path, "rb");
    if (f == NULL) {
        return fail("cannot open %s", path);
    }
    if (fseek(f, 0L, SEEK_END) != 0) {
        fclose(f);
        return fail("cannot seek %s", path);
    }
    len = ftell(f);
    if (len < 0) {
        fclose(f);
        return fail("cannot size %s", path);
    }
    if ((unsigned long)len > BLOB_MAX) {
        fclose(f);
        return fail("%s is too large for this tool", path);
    }
    rewind(f);

    b->data = (unsigned char *)malloc((size_t)len + 1);
    if (b->data == NULL) {
        fclose(f);
        return fail("out of memory reading %s", path);
    }
    got = fread(b->data, 1, (size_t)len, f);
    fclose(f);
    if ((long)got != len) {
        blob_free(b);
        return fail("short read on %s", path);
    }
    b->data[got] = '\0'; /* lets a spec file be walked as a string */
    b->size = (unsigned long)len;
    return 0;
}

/* An 8.3 name, so the temporary lands beside its target on a DOS filesystem
 * rather than being rejected or truncated into something else. */
#define TEMP_NAME "SC15TMP.$$$"

static void temp_path(char *out, const char *path)
{
    size_t n = strlen(path);
    size_t cut = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        if (path[i] == '/' || path[i] == '\\' || path[i] == ':') {
            cut = i + 1;
        }
    }
    memcpy(out, path, cut);
    strcpy(out + cut, TEMP_NAME);
}

/* Write through a sibling temporary and rename it into place. Opening the
 * target directly with "wb" empties it before the first byte arrives, so a full
 * disk, a dismounted DOSBox drive or a short write destroyed the file that was
 * already installed. The remaining window is between the remove and the rename,
 * where DOS gives no way to replace a file atomically; nothing is being written
 * in it, and the data is still in the temporary if the rename fails. */
int blob_save(const struct blob *b, const char *path)
{
    char   tmp[SC15_PATH_MAX];
    FILE  *f;
    size_t put;

    if (strlen(path) + sizeof TEMP_NAME > sizeof tmp) {
        return fail("path is too long: %s", path);
    }
    temp_path(tmp, path);
    remove(tmp);

    f = fopen(tmp, "wb");
    if (f == NULL) {
        return fail("cannot create %s", tmp);
    }
    put = fwrite(b->data, 1, (size_t)b->size, f);
    if (fclose(f) != 0 || (unsigned long)put != b->size) {
        remove(tmp);
        return fail("short write on %s", tmp);
    }

    remove(path); /* rename will not overwrite on DOS */
    if (rename(tmp, path) != 0) {
        return fail("cannot rename %s onto %s, the output is left in %s", tmp,
                    path, tmp);
    }
    return 0;
}

/* atoi() cannot report a bad field. It reads "garbage" as 0 and "12x" as 12,
 * and a specification typo became a valid byte that the driver then
 * transmitted: a program of 255 leaves the wire as 0FFh, a MIDI system status
 * byte, directly behind a 0Cnh program change. */
int parse_num(const char *s, long lo, long hi, long *out)
{
    long v = 0;
    int  neg = 0;

    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    if (*s < '0' || *s > '9') {
        return 0;
    }
    while (*s >= '0' && *s <= '9') {
        int d = *s - '0';
        if (v > (LONG_MAX - d) / 10) {
            return 0;
        }
        v = v * 10 + d;
        s++;
    }
    if (*s != '\0') {
        return 0;
    }
    if (neg) {
        v = -v;
    }
    if (v < lo || v > hi) {
        return 0;
    }
    *out = v;
    return 1;
}

void blob_free(struct blob *b)
{
    if (b->data != NULL) {
        free(b->data);
        b->data = NULL;
    }
    b->size = 0;
}

unsigned int rd16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

unsigned long rd32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

void wr16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

void wr32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

/* Folded by hand rather than with tolower(), which takes an int that must be
 * EOF or representable as unsigned char; a plain char with the top bit set is
 * undefined there. Only A to Z fold, which is all a keyword contains. */
int same_word(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        int ca = (unsigned char)*a++;
        int cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') {
            ca += 'a' - 'A';
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb += 'a' - 'A';
        }
        if (ca != cb) {
            return 0;
        }
    }
    return *a == *b;
}
