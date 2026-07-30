/* refjudge - whether the real skidset would draw a menu row for a driver file.
 *
 *     refjudge <file.drv>
 *
 * Exit 0 and print "accept", exit 1 and print "refuse: <reason>", and exit 2
 * for anything that stopped a judgement being made. Those three are the whole
 * contract: diff.sh treats 2 as a failure of the harness rather than as a
 * verdict, so an unreadable file can never read as agreement.
 *
 * Nothing here is skidsc55 code. It links skidset's own src/drvblk.c unmodified
 * and drives it the way src/drvscan.c drives it.
 *
 * The scan is the whole file, not one candidate. It used to find the first raw
 * magic match, read one window and parse it, which measured whether the two
 * parsers agree about a block and said nothing about whether the two scanners
 * agree about a driver. Those are different promises, and sc15 block makes the
 * second one: a file holding two blocks is refused whole, a false match is
 * passed over, and which candidate is the block is decided by drv_blk_span
 * rather than by whichever twelve bytes turned up first.
 *
 * Each candidate is read through a DRV_BLK_MAX window, NUL terminated, because
 * that is what skidset reads it through. The length is why a terminator past
 * the first kilobyte does not count and the NUL is why one behind a zero byte
 * does not either.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drvblk.h"

/* The one thing drvblk.c wants from skidset.h, defined the way util.c does. */
int sk_is_blank(int c)
{
    return c == ' ' || c == '\t';
}

static char buf[65536];
static long buflen;

/* drv_blk_find from an offset, which is how block_of() resumes. */
static long find_from(long from)
{
    int at;

    if (from >= buflen) {
        return -1;
    }
    at = drv_blk_find(buf + from, (int)(buflen - from));
    return at < 0 ? -1 : from + at;
}

/* The bytes skidset hands the parser for the candidate at `at`. */
static void window(char *out, long at)
{
    long n = buflen - at;

    if (n > DRV_BLK_MAX) {
        n = DRV_BLK_MAX;
    }
    memcpy(out, buf + at, (size_t)n);
    out[n] = '\0';
}

int main(int argc, char **argv)
{
    static char    text[DRV_BLK_MAX + 1];
    struct drv_blk b;
    const char    *why;
    FILE          *f;
    long           from = 0;
    long           first = -1;
    int            blocks = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: refjudge <file.drv>\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    buflen = (long)fread(buf, 1, sizeof buf, f);
    if (ferror(f)) {
        fclose(f);
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    fclose(f);

    /* drvscan.c's loop over one file: find, span, resume, count. */
    for (;;) {
        long at = find_from(from);
        long span;

        if (at < 0) {
            break;
        }
        window(text, at);
        span = drv_blk_span(text);
        from = at + (span > 0 ? span : (long)strlen(DRV_BLK_MAGIC));
        if (span == 0) {
            /* Not a block. A candidate whose first line is the magic is named
             * on screen, but neither sort denies a later block its row, so
             * neither is a refusal. */
            continue;
        }
        blocks++;
        if (blocks == 1) {
            first = at;
            continue;
        }
        printf("refuse: a driver carries one block, this has more\n");
        return 1;
    }
    if (first < 0) {
        printf("refuse: no block\n");
        return 1;
    }

    window(text, first);
    why = drv_blk_parse(&b, text);
    if (why != NULL) {
        printf("refuse: %s\n", why);
        return 1;
    }
    /* Deliberately not drv_scan_offer's carrier rule, which refuses a video
     * block in a .DRV and a sound block in LOAD.EXE. That rule is decided by
     * the filename, and every corpus case is a .drv in a scratch directory, so
     * applying it would refuse each video case for its name rather than for
     * anything in it. sc15 block does not model it either: it reports on a
     * block wherever it finds one, being a block checker rather than a menu
     * builder. The boundary is in README.md. */
    printf("accept\n");
    return 0;
}
