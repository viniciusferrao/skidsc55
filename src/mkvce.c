/* mkvce - build a Sound Canvas voice bank from a text specification.
 *
 * Every instrument record is copied whole out of an existing MT-32 bank, so
 * all the parameters the music engine owns survive exactly as the composer set
 * them, and only the fields the driver reads are overwritten. That is why the
 * banks cannot simply be shipped: they are derived from the game's own data,
 * which this project does not redistribute.
 *
 * Specification syntax, one directive or one instrument per line, # to end of
 * line is a comment:
 *
 *   source <path>            the MT-32 bank the records are copied from
 *   size <bytes>             optional, the record size the source must have
 *   volumescale <factor>     optional, scales every channel volume
 *   <NAME> <FROM> <program> <volume> <pan> <bend> <channel> <bank> [k=v ...]
 *
 * NAME is the four character resource name the songs look up. FROM names the
 * record in the source bank to copy. Any positional value may be "=" to keep
 * whatever the source record already had. Trailing name=value pairs reach the
 * fields that rarely need touching.
 *
 * All three directives describe the whole bank, so all three have to appear
 * before the first instrument. Accepting one later applied it to part of the
 * file and not the rest.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sc15.h"

#define MAX_INSTR 64
#define MAX_LINE 256

/* Positional columns, in the order they appear on an instrument line.
 *
 * The ranges matter. Everything the driver puts on the wire has to be a MIDI
 * data byte, so 127 is the ceiling: a program of 255 would leave as 0FFh
 * immediately behind the 0Cnh program change, and a receiver reads that as a
 * system status byte rather than as a program.
 *
 * channel is the exception, because it is read by the engine and not sent. A
 * value below 10h pins the part to that MIDI channel and anything else leaves
 * the engine to derive one from the track index, which is what the shipped
 * effects do with 11h. Both halves of that encoding are legal, so the whole
 * byte is. */
/* One entry per line, and formatting off to keep it that way: these are short
 * enough that clang-format packs them two to a row and pads the columns.
 * extra[] below is the same shape and stays a list only because its entries are
 * wider, and two tables read together should not differ in layout by
 * accident. */
/* clang-format off */
static const struct {
    int         ofs;
    const char *name;
    long        lo, hi;
} column[] = {{I_PROGRAM, "program", 0, MIDI_MAX},
              {I_VOLUME, "volume", 0, MIDI_MAX},
              {I_PAN, "pan", 0, MIDI_MAX},
              {I_BENDRANGE, "bend", 0, MIDI_MAX},
              {I_CHANNEL, "channel", 0, 255},
              {I_BANK, "bank", 0, MIDI_MAX}};
/* clang-format on */
#define NCOLUMN (int)(sizeof column / sizeof column[0])

/* Fields reachable by a trailing name=value pair. None of these is transmitted;
 * the engine reads them out of the record, so they carry a raw byte. transpose
 * is signed and may be written either way round, -12 or 244. */
static const struct {
    int         ofs;
    const char *name;
    long        lo, hi;
} extra[] = {{I_TRANSPOSE, "transpose", -128, 255},
             {I_VELSENS, "velsens", 0, 255},
             {I_PORTA, "porta", 0, 1},
             {I_PITCHDIV, "pitchdiv", 0, 255},
             {I_PITCHOFS, "pitchofs", 0, 255},
             {I_RETRIG, "retrigger", 0, 255}};
#define NEXTRA (int)(sizeof extra / sizeof extra[0])

/* The smallest source record this tool can safely write into. The fields it
 * overwrites run up to I_RETRIG, so a record shorter than that would be written
 * past its own allocation. The genuine MT-32 banks carry 93. */
#define REC_MIN (I_RETRIG + 1)

/* Ceiling on the bank this builds. It has to fit a 16-bit size_t with room to
 * spare, and the real banks are around 1.2 KB, so this is generous. */
#define BANK_MAX 32000UL

struct bank {
    struct blob  img;
    unsigned int count;
    unsigned int recsize;
};

static int ci_equal(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z') {
            ca -= 'a' - 'A';
        }
        if (cb >= 'a' && cb <= 'z') {
            cb -= 'a' - 'A';
        }
        if (ca != cb) {
            return 0;
        }
    }
    return 1;
}

/* Locate a record by name. Returns its offset, or 0 when absent, which is
 * never a valid record offset because the header occupies the start.
 *
 * Offsets into a loaded buffer are size_t, which is 16 bits on a DOS host.
 * That is deliberate: every file this tool opens is well under the 60000 byte
 * ceiling in util.c, and doing the arithmetic in unsigned long instead would
 * truncate silently when the result met a pointer. Only the container's own
 * 32-bit size fields are read and written as long. */
static size_t bank_find(const struct bank *b, const char *name)
{
    unsigned int i;
    size_t       names = VCE_NAMES_OFS;
    size_t       offs = VCE_NAMES_OFS + 4U * b->count;
    size_t       data = VCE_NAMES_OFS + 8U * b->count;

    for (i = 0; i < b->count; i++) {
        const char *have = (const char *)b->img.data + names + 4U * i;
        if (ci_equal(have, name, VCE_NAME_LEN)) {
            /* The stored offset is a 32-bit field in a file this tool did not
             * write. Keeping it long until it has been checked matters on a DOS
             * host, where size_t is 16 bits and the cast alone would wrap a
             * large offset down into a plausible looking small one. */
            unsigned long rel = rd32(b->img.data + offs + 4U * i);
            unsigned long at = (unsigned long)data + rel;
            if (rel > b->img.size || at + b->recsize > b->img.size) {
                return 0;
            }
            return (size_t)at;
        }
    }
    return 0;
}

/* Resolve a source path the way the specification means it. A relative path is
 * written relative to the specification file, not to whatever directory the
 * build happens to run from, so that "source ../../../stunts/MTSKIDMS.VCE"
 * keeps working whether the build is driven from here, from a parent, or by an
 * IDE. An absolute path is taken as given. */
static void resolve_source(char *out, size_t outsz, const char *src,
                           const char *spec)
{
    size_t dirlen = 0;
    size_t i;

    if (src[0] == '/' || src[0] == '\\' || (src[0] != '\0' && src[1] == ':')) {
        /* Bounded by what src holds and then by what out can take, in that
         * order. strncpy bounded only by outsz was reading a 256 byte srcpath
         * through a count derived from the 512 byte destination: it stops at
         * the source NUL so nothing was ever read past the end in practice,
         * but the count said otherwise and the GCC analyzer reported it as a
         * 255 byte over-read. A copy whose length comes from the source is
         * both correct and provably so. */
        size_t n = strlen(src);

        if (n > outsz - 1) {
            n = outsz - 1;
        }
        memcpy(out, src, n);
        out[n] = '\0';
        return;
    }
    for (i = 0; spec[i] != '\0'; i++) {
        if (spec[i] == '/' || spec[i] == '\\') {
            dirlen = i + 1;
        }
    }
    if (dirlen + strlen(src) >= outsz) {
        dirlen = 0; /* no room, fall back to the bare path */
    }
    memcpy(out, spec, dirlen);
    strcpy(out + dirlen, src);
}

/* want is the record size the specification asked for with a size directive, or
 * 0 for whatever the source carries. */
static int bank_open(struct bank *b, const char *path, unsigned int want)
{
    unsigned long first;

    if (blob_load(&b->img, path) != 0) {
        return 1;
    }
    if (b->img.size < VCE_NAMES_OFS) {
        blob_free(&b->img);
        return fail("%s is too short to be a voice bank", path);
    }
    b->count = rd16(b->img.data + VCE_COUNT_OFS);
    if (b->count == 0 || b->count > MAX_INSTR) {
        blob_free(&b->img);
        return fail("%s has an implausible instrument count", path);
    }
    first = (unsigned long)VCE_NAMES_OFS + 8UL * b->count;
    if (first + 2 > b->img.size) {
        blob_free(&b->img);
        return fail("%s is truncated", path);
    }
    b->recsize = rd16(b->img.data + (size_t)first);

    /* Everything below this writes fields at fixed offsets into a copy of a
     * source record, so a record shorter than the last of those offsets would
     * be written past its own allocation. The check has to come before any
     * record is copied, not when one is used. */
    if (b->recsize < REC_MIN) {
        unsigned int got = b->recsize;
        blob_free(&b->img);
        return fail("%s has %u byte records, too short to hold the fields this "
                    "writes, which need %d",
                    path, got, REC_MIN);
    }
    if (want != 0 && b->recsize != want) {
        unsigned int got = b->recsize;
        blob_free(&b->img);
        return fail("%s has %u byte records, but size asked for %u", path, got,
                    want);
    }
    return 0;
}

/* Split a line into whitespace separated fields, in place. */
static int split(char *line, char **field, int max)
{
    int   n = 0;
    char *p = line;

    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (*p == '\0') {
            return n;
        }
        if (n == max) {
            return -1;
        }
        field[n++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' &&
               *p != '\n') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
}

/* A volume of 0 is the "do not send CC 7" flag rather than a level, so it must
 * never be scaled into or out of existence. */
static int scale_volume(int v, long num, long den)
{
    long s;
    long q, r;
    if (v <= 0 || num == den) {
        return v;
    }
    /* Split the factor into its whole part and its remainder before
     * multiplying. v * num overflows a 32-bit long for a factor with six
     * decimal places: 127 * 999999999 is 59 times LONG_MAX, and long is 32 bits
     * on the DOS build this targets. Taken apart, v * q reaches 127000 and
     * v * r + den / 2 reaches 128 million, both of which fit.
     *
     * The rounding is the same as the single expression gave, because
     * v * num + den/2 over den equals v*q plus (v*r + den/2) over den whenever
     * num is q * den + r. */
    q = num / den;
    r = num % den;
    s = (long)v * q;
    s += ((long)v * r + den / 2) / den;
    if (s < 1) {
        s = 1;
    }
    if (s > 127) {
        s = 127;
    }
    return (int)s;
}

/* Parse a decimal factor such as 0.7 into a numerator over 1000. C89 has
 * strtod, but keeping this integer avoids dragging the floating point library
 * into a 16-bit DOS build for one multiplication. */
static int parse_factor(const char *s, long *num, long *den)
{
    long whole = 0, frac = 0, scale = 1;
    int  digits = 0;

    /* Bounded at 1000. Every scaled volume is clamped to 127 anyway, so a
     * larger factor cannot mean anything, and without a bound the two
     * multiplications below overflow on a long that is 32 bits here and on the
     * DOS build alike. */
    while (*s >= '0' && *s <= '9') {
        whole = whole * 10 + (*s++ - '0');
        digits++;
        if (whole > 1000L) {
            return 1;
        }
    }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9' && scale < 1000000L) {
            frac = frac * 10 + (*s++ - '0');
            scale *= 10;
            digits++;
        }
    }
    if (*s != '\0' || digits == 0) {
        return 1; /* trailing rubbish, or no digit at all, such as "." */
    }
    *num = whole * scale + frac;
    *den = scale;
    return 0;
}

int cmd_mkvce(int argc, char **argv)
{
    struct bank    src;
    struct blob    out;
    char           names[MAX_INSTR][VCE_NAME_LEN];
    unsigned char *rec[MAX_INSTR];
    unsigned int   count = 0;
    long           vnum = 1, vden = 1;
    unsigned int   wantsize = 0;
    char           srcpath[MAX_LINE];
    struct blob    spec;
    char          *cursor;
    unsigned long  total;
    unsigned int   i;
    int            rc = 1;

    if (argc != 2) {
        return fail("usage: sc15 mkvce <spec.txt> <out.vce>");
    }
    if (blob_load(&spec, argv[0]) != 0) {
        return 1;
    }
    srcpath[0] = '\0';
    src.img.data = NULL;
    memset(rec, 0, sizeof rec);

    cursor = (char *)spec.data;
    while (*cursor != '\0') {
        char *line = cursor;
        char *hash;
        char *field[16];
        int   n;

        while (*cursor != '\0' && *cursor != '\n') {
            cursor++;
        }
        if (*cursor == '\n') {
            *cursor++ = '\0';
        }
        hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }

        n = split(line, field, 16);
        if (n <= 0) {
            if (n < 0) {
                fail("too many fields on one line");
                goto done;
            }
            continue;
        }

        /* Directives describe the whole bank, so they have to be settled before
         * any record is copied. Accepting one afterwards silently applied it to
         * part of the file: a second source never reopened the bank already
         * loaded, and a later volumescale reached only the records below it. */
        if (strcmp(field[0], "source") == 0 || strcmp(field[0], "size") == 0 ||
            strcmp(field[0], "volumescale") == 0) {
            if (n != 2) {
                fail("%s takes one value", field[0]);
                goto done;
            }
            if (src.img.data != NULL) {
                fail("%s must come before the first instrument", field[0]);
                goto done;
            }
            if (strcmp(field[0], "source") == 0) {
                strncpy(srcpath, field[1], sizeof srcpath - 1);
                srcpath[sizeof srcpath - 1] = '\0';
            } else if (strcmp(field[0], "size") == 0) {
                long v;
                if (parse_num(field[1], REC_MIN, 4096, &v) == 0) {
                    fail("bad size %s", field[1]);
                    goto done;
                }
                wantsize = (unsigned int)v;
            } else {
                if (parse_factor(field[1], &vnum, &vden) != 0) {
                    fail("bad volumescale factor %s", field[1]);
                    goto done;
                }
            }
            continue;
        }

        /* An instrument line. */
        if (src.img.data == NULL) {
            char resolved[MAX_LINE * 2];
            if (srcpath[0] == '\0') {
                fail("no source directive before the first instrument");
                goto done;
            }
            resolve_source(resolved, sizeof resolved, srcpath, argv[0]);
            if (bank_open(&src, resolved, wantsize) != 0) {
                goto done;
            }
        }
        if (n < 2 + NCOLUMN) {
            fail("instrument line %s has too few columns", field[0]);
            goto done;
        }
        if (strlen(field[0]) != VCE_NAME_LEN) {
            fail("name %s is not four characters", field[0]);
            goto done;
        }
        /* bank_find compares four bytes whatever the argument's length, so a
         * short source name read past its own terminator. */
        if (strlen(field[1]) != VCE_NAME_LEN) {
            fail("source name %s is not four characters", field[1]);
            goto done;
        }
        /* Case insensitively, because that is how the game resolves a name:
         * audioresource_compare_chunknames() is always called with its case
         * sensitive flag clear, so BASS and bass are one resource to it even
         * though they are two lines here. */
        for (i = 0; i < count; i++) {
            if (ci_equal(names[i], field[0], VCE_NAME_LEN)) {
                fail("%s appears twice; a song would only ever find the first",
                     field[0]);
                goto done;
            }
        }
        if (count == MAX_INSTR) {
            fail("too many instruments");
            goto done;
        }
        {
            size_t         off = bank_find(&src, field[1]);
            unsigned char *r;
            int            c;

            if (off == 0 || (unsigned long)off + src.recsize > src.img.size) {
                fail("%s is not in the source bank", field[1]);
                goto done;
            }
            /* Only the first record established recsize. A record whose own
             * length disagrees fits in the file but is not the size it claims,
             * so copying recsize bytes takes part of whatever follows it and
             * rewrites that as though it were fields of this instrument. */
            if (rd16(src.img.data + off) != src.recsize) {
                fail("%s is %u bytes where the bank's records are %u", field[1],
                     rd16(src.img.data + off), src.recsize);
                goto done;
            }
            r = (unsigned char *)malloc(src.recsize);
            if (r == NULL) {
                fail("out of memory");
                goto done;
            }
            memcpy(r, src.img.data + off, src.recsize);

            for (c = 0; c < NCOLUMN; c++) {
                const char *v = field[2 + c];
                long        val;
                if (v[0] == '=' && v[1] == '\0') {
                    val = r[column[c].ofs];
                } else if (parse_num(v, column[c].lo, column[c].hi, &val) ==
                           0) {
                    free(r);
                    fail("%s: %s must be %ld to %ld, not %s", field[0],
                         column[c].name, column[c].lo, column[c].hi, v);
                    goto done;
                }
                /* After both branches, not only after the parse. "=" keeps
                 * whatever the source record holds, and a source that holds 255
                 * where a program belongs would put a MIDI status byte on the
                 * wire just as surely as a specification that asked for it. */
                if (val < column[c].lo || val > column[c].hi) {
                    free(r);
                    fail("%s: %s is %ld in the source bank, outside %ld to %ld",
                         field[0], column[c].name, val, column[c].lo,
                         column[c].hi);
                    goto done;
                }
                if (column[c].ofs == I_VOLUME) {
                    val = scale_volume((int)val, vnum, vden);
                }
                r[column[c].ofs] = (unsigned char)val;
            }

            for (c = 2 + NCOLUMN; c < n; c++) {
                char *eq = strchr(field[c], '=');
                int   e;
                long  val;
                if (eq == NULL) {
                    free(r);
                    fail("stray field %s", field[c]);
                    goto done;
                }
                *eq = '\0';
                for (e = 0; e < NEXTRA; e++) {
                    if (strcmp(field[c], extra[e].name) == 0) {
                        break;
                    }
                }
                if (e == NEXTRA) {
                    free(r);
                    fail("unknown field %s", field[c]);
                    goto done;
                }
                if (parse_num(eq + 1, extra[e].lo, extra[e].hi, &val) == 0) {
                    free(r);
                    fail("%s: %s must be %ld to %ld, not %s", field[0],
                         extra[e].name, extra[e].lo, extra[e].hi, eq + 1);
                    goto done;
                }
                if (val < 0) {
                    val += 256; /* a signed transpose written as -15 */
                }
                r[extra[e].ofs] = (unsigned char)val;
            }

            wr16(r + I_SIZE, src.recsize);
            memcpy(names[count], field[0], VCE_NAME_LEN);
            rec[count] = r;
            count++;
        }
    }

    if (count == 0) {
        fail("no instruments in the specification");
        goto done;
    }

    /* Long until it has been checked. Several instruments may name the same
     * source record, so a source comfortably inside the input ceiling can still
     * describe an output past 64 KiB, and on a DOS host that multiplication
     * wrapped into a small size_t: a short allocation followed by writes at the
     * unwrapped offsets. */
    total = (unsigned long)VCE_NAMES_OFS + 8UL * count +
            (unsigned long)src.recsize * count;
    if (total > BANK_MAX) {
        fail("%u instruments of %u bytes would make a %lu byte bank, over the "
             "%lu this builds",
             count, src.recsize, total, (unsigned long)BANK_MAX);
        goto done;
    }
    out.data = (unsigned char *)malloc((size_t)total);
    out.size = total;
    if (out.data == NULL) {
        fail("out of memory");
        goto done;
    }
    wr32(out.data, total);
    wr16(out.data + VCE_COUNT_OFS, count);
    for (i = 0; i < count; i++) {
        memcpy(out.data + VCE_NAMES_OFS + 4U * i, names[i], VCE_NAME_LEN);
        wr32(out.data + VCE_NAMES_OFS + 4U * count + 4U * i,
             (unsigned long)src.recsize * i);
        memcpy(out.data + VCE_NAMES_OFS + 8U * count + (size_t)src.recsize * i,
               rec[i], src.recsize);
    }
    rc = blob_save(&out, argv[1]);
    if (rc == 0) {
        printf("%s: %u instruments, %lu bytes, records from %s\n", argv[1],
               count, (unsigned long)total, srcpath);
    }
    free(out.data);

done:
    for (i = 0; i < count; i++) {
        free(rec[i]);
    }
    blob_free(&src.img);
    blob_free(&spec);
    return rc;
}
