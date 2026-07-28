/* block - read and check the skidset driver block inside a built driver.
 *
 * skidset scans every *.DRV in the game directory for the magic and grows its
 * setup menu from the blocks it finds. A block it refuses is a row that is not
 * there, and that looks identical to a driver skidset never opened, so the
 * cheapest place to catch a bad block is here, before the driver ships.
 *
 * The limits below are DRVBLOCK.md's, and the refusals are worded the way
 * skidset words its own, so a block that passes here reads the same when it
 * fails there.
 */
#include <stdio.h>
#include <string.h>
#include "sc15.h"

#define BLK_MAGIC "SKIDSETDRV01"
#define BLK_END "SKIDSETEND"

#define BLK_MAX 1024     /* whole block, magic and terminator included */
#define BLK_LINE 448     /* any single line, which is what lets help be one */
#define BLK_SOUND_LBL 31 /* label, sound menu */
#define BLK_VIDEO_LBL 24 /* label, video menu */
#define BLK_BRIEF 21     /* brief, before skidset adds its brackets */
#define BLK_MODE 16      /* mode, keeps line 2 of SETUP.DAT inside its 80 */
#define BLK_WORD 26      /* longest word in help; a longer one cannot wrap */
#define BLK_COLS 26      /* help window interior, screen columns 49 to 74 */
#define BLK_ROWS 15      /* it grows from row 7 until its shadow reaches 24 */

/* help is one key carrying the whole paragraph, like every other key. It used
 * to repeat and the values were joined with a space, which is why BLK_LINE is
 * 448: the largest paragraph the window can hold is BLK_ROWS by BLK_COLS, and
 * all of it has to fit on the one line.
 *
 * Static rather than automatic. A 16-bit build runs on an 8 KB stack and these
 * four buffers are 448 bytes each. */
struct blk {
    char label[BLK_LINE];
    char brief[BLK_LINE];
    char mode[BLK_LINE];
    char help[BLK_LINE];
    int  sound, video;
    int  unknown;
    int  lines;
};

static struct blk b;

static void trim(char *s)
{
    size_t n;
    size_t i = 0;

    while (s[i] == ' ' || s[i] == '\t') {
        i++;
    }
    if (i > 0) {
        memmove(s, s + i, strlen(s + i) + 1);
    }
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

/* Wrap help to BLK_COLS and return the rows it comes to, or -1 for a word that
 * cannot be made to fit however it is broken. With show set it prints them the
 * width skidset draws them, so what you see here is what lands in the window.
 *
 * This follows skidset's drv_blk_wrap() step for step rather than wrapping the
 * text some other correct-looking way, because the row count is a limit and two
 * wrappers that merely agree on ordinary prose do not agree on a limit. The one
 * that used to be here tokenised into words and rebuilt each row with a single
 * space between them, and so measured a run of spaces as one column: sixteen
 * one-character words separated by twenty-five spaces each came to 2 rows here
 * and 16 in skidset, which refused it for overflowing a fifteen-row window this
 * reported as fitting twice over.
 *
 * The rules that matter: spaces are skipped only at the start of a row, an
 * internal run is copied out whole and costs its own columns, and a row breaks
 * at the last space rather than mid-word unless the word is the whole row. */
static int wrap(const char *s, int show)
{
    int i = 0;
    int rows = 0;

    for (;;) {
        int start, end, last;

        /* Newlines as well as spaces. A help value is one line of the block so
         * it can hold neither, and skidset skips only spaces here; taking a
         * newline too costs nothing on any value that can exist and keeps an
         * impossible one from standing still forever. */
        while (s[i] == ' ' || s[i] == '\n') {
            i++;
        }
        if (s[i] == '\0') {
            return rows;
        }
        start = i;
        last = -1;
        end = i;
        while (s[end] != '\0' && s[end] != '\n' && end - start < BLK_COLS) {
            if (s[end] == ' ') {
                last = end;
            }
            end++;
        }
        /* The row is full and the next character continues a word, so break at
         * the last space instead. A space there is not part of a word, which is
         * what lets a word of exactly BLK_COLS characters stand. */
        if (s[end] != '\0' && s[end] != '\n' && s[end] != ' ' &&
            end - start == BLK_COLS) {
            if (last < 0) {
                return -1;
            }
            end = last;
        }
        if (show) {
            printf("          |%-*.*s|\n", BLK_COLS, end - start, s + start);
        }
        rows++;
        i = end;
    }
}

/* The window skidset reads a candidate through: BLK_MAX bytes from the magic,
 * NUL terminated, and walked as a C string. Both halves matter. The length is
 * why a terminator past the first kilobyte does not count, and the C string is
 * why one behind an embedded NUL does not either.
 *
 * Static because a 16-bit build runs on an 8 KB stack. */
static char win[BLK_MAX + 1];

static void window(const struct blob *drv, unsigned long at)
{
    unsigned long n = drv->size - at;

    if (n > BLK_MAX) {
        n = BLK_MAX;
    }
    memcpy(win, drv->data + at, (size_t)n);
    win[n] = '\0';
}

/* One line out of the window, skidset's next_line() rather than a similar one:
 * the CR of a CRLF is dropped before the length is counted, any other CR is
 * left in as content, and a line longer than the limit is a malformed block
 * rather than a line to truncate. Returns 0 at the end, -1 for too long. */
static int blk_next_line(const char **p, char *buf, int max)
{
    const char *s = *p;
    int         n = 0;

    if (*s == '\0') {
        return 0;
    }
    while (*s != '\0' && *s != '\n') {
        if (*s == '\r' && s[1] == '\n') {
            s++;
            break;
        }
        if (n >= max - 1) {
            buf[n] = '\0';
            return -1;
        }
        buf[n++] = *s++;
    }
    buf[n] = '\0';
    *p = (*s == '\n') ? s + 1 : s;
    return 1;
}

/* How many bytes the candidate at `at` occupies, or 0 for a candidate that is
 * not a delimited block. skidset's drv_blk_span(), step for step.
 *
 * This decides two things at once: which candidate is the block, and how many
 * blocks the file holds. It used to be a raw search for a newline followed by
 * SKIDSETEND and a newline, which is not the same question and got three
 * answers wrong. A CRLF terminator has a CR in the byte that search demanded be
 * an LF, so a second block written by a DOS editor was invisible here and
 * counted there. A terminator at the end of the image has no byte after it at
 * all, and still gives a span. And a terminator behind an embedded NUL gives
 * none, because the window ends at the NUL, where the raw search read straight
 * past it.
 *
 * A span is not a promise the block parses. skidset counts it toward the
 * one-block rule either way, so this has to as well. */
static long blk_span(void)
{
    static char line[BLK_LINE + 2];
    const char *p = win;
    int         first = 1;
    int         got;

    while ((got = blk_next_line(&p, line, (int)sizeof line)) > 0) {
        long at = (long)(p - win);

        if (first) {
            first = 0;
            /* Untrimmed. The grammar gives the magic a line of its own, so a
             * candidate whose first line says anything more has no span, and
             * the caller goes back to searching and finds the real block. */
            if (strcmp(line, BLK_MAGIC) != 0) {
                return 0;
            }
            continue;
        }
        /* The magic anywhere in a later line, not only as the whole of one:
         * nothing requires a newline before a block, so a valid one can begin
         * inside what this candidate sees as a line. */
        if (strstr(line, BLK_MAGIC) != NULL) {
            return 0;
        }
        if (strcmp(line, BLK_END) == 0) {
            return at;
        }
        if (at > BLK_MAX) {
            break;
        }
    }
    return 0;
}

/* The next raw twelve-byte match at or after `from`, or -1. Raw, like
 * skidset's drv_blk_find: whether it is a block is blk_span's question. */
static long magic_raw_from(const struct blob *drv, unsigned long from)
{
    unsigned long at;

    if (from >= drv->size) {
        return -1;
    }
    for (at = from; at + (sizeof BLK_MAGIC - 1) <= drv->size; at++) {
        if (memcmp(drv->data + at, BLK_MAGIC, sizeof BLK_MAGIC - 1) == 0) {
            return (long)at;
        }
    }
    return -1;
}

/* Whether the first word of s is a name DOS could put on a disk: one to eight
 * characters, letters or digits or the punctuation 8.3 permits. The first word
 * of a mode names a file, NAME.COD, which LOAD.EXE goes looking for, so without
 * this a block could ask for a mode whose ninth character is silently dropped.
 * The rest of the sixteen is room for the flags that may follow, as the
 * Hercules row's "CGA /h" does. */
static int dos_name(const char *s)
{
    int n = 0;

    while (s[n] != '\0' && s[n] != ' ') {
        char c = s[n];

        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              strchr("!#$%&'()-@^_`{}~", c) != NULL)) {
            return 0;
        }
        n++;
    }
    return n >= 1 && n <= 8;
}

static int longest_word(const char *s)
{
    int best = 0;
    int n = 0;

    for (; *s != '\0'; s++) {
        if (*s == ' ' || *s == '\n') {
            n = 0;
        } else if (++n > best) {
            best = n;
        }
    }
    return best;
}

/* One key=value assignment, refusing a second helping of any key, help
 * included. Giving one twice is an error rather than an override, so that a
 * block cannot mean two things depending on which line skidset read last. */
static int set_once(char *dst, const char *val, const char *key,
                    const char *file)
{
    if (dst[0] != '\0') {
        return fail("%s: %s given twice", file, key);
    }
    if (val[0] == '\0') {
        return fail("%s: %s has no value", file, key);
    }
    strcpy(dst, val);
    return 0;
}

int cmd_block(int argc, char **argv)
{
    struct blob   drv;
    const char   *file;
    unsigned long at, end, i;
    int           rows, lim;

    if (argc < 1) {
        return fail("usage: sc15 block <driver.drv>");
    }
    file = argv[0];
    if (blob_load(&drv, file) != 0) {
        return 1;
    }

    memset(&b, 0, sizeof b);

    /* The whole-file scan, in the shape skidset's drvscan.c runs it: search raw
     * bytes for the magic, ask blk_span whether that candidate is a delimited
     * block, resume by its span or by a magic length, and count every block
     * found. Whatever precedes a candidate is code, so there is no fixed offset
     * and no left context to check.
     *
     * Counting here rather than in a second pass afterwards is the point. The
     * one-block rule and the choice of which candidate to report on are the
     * same walk, and running them separately meant two searches that could
     * disagree about what a block is, which is exactly what they did. */
    {
        long found = -1;
        long from = 0;
        int  blocks = 0;

        for (;;) {
            long cand = magic_raw_from(&drv, (unsigned long)from);
            long span;

            if (cand < 0) {
                break;
            }
            window(&drv, (unsigned long)cand);
            span = blk_span();
            from = cand + (span > 0 ? span : (long)(sizeof BLK_MAGIC - 1));
            if (span == 0) {
                /* Twelve bytes that happened to line up, or a candidate with
                 * nothing terminating it. skidset names the second sort and
                 * passes over the first, and neither denies a later block its
                 * row, so neither is a refusal here. */
                continue;
            }
            blocks++;
            if (blocks == 1) {
                found = cand;
                continue;
            }
            blob_free(&drv);
            return fail("%s: a driver carries one block, this has more, the "
                        "second at %ld",
                        file, cand);
        }
        if (found < 0) {
            blob_free(&drv);
            return fail("%s: no " BLK_MAGIC, file);
        }
        at = (unsigned long)found;
    }
    end = 0;

    /* Walk the block a line at a time. Blank lines and comments are ignored but
     * still count against the size and line limits, so a block cannot get
     * around them by padding. */
    i = at;
    while (i < drv.size) {
        char          line[BLK_LINE + 2];
        char         *val;
        unsigned long base = i; /* where this line starts in the image */
        size_t        len = 0;  /* characters, with any CR and the LF removed */
        size_t        raw = 0;  /* bytes this line occupies in the image */
        size_t        s, e, j;  /* the span left once the blanks come off */
        int           has_lf = 0;

        while (base + raw < drv.size && drv.data[base + raw] != '\n') {
            raw++;
        }
        len = raw;
        if (base + raw < drv.size) {
            raw++; /* the LF is part of the block too */
            has_lf = 1;
        }
        if (len > 0 && drv.data[base + len - 1] == '\r') {
            len--; /* a CR before the LF is accepted and ignored */
        }
        if (len > BLK_LINE) {
            blob_free(&drv);
            return fail("%s: a line is %lu characters, the limit is %d", file,
                        (unsigned long)len, BLK_LINE);
        }
        /* Against every byte the line occupies, delimiter included, and against
         * the end of the line rather than its start. Measuring from the start
         * let a line that began just inside the limit run past it; measuring
         * the characters rather than the bytes let it run one further with an
         * LF and two with a CRLF. BLK_MAX is what skidset will read, so this
         * has to count what is there, not what is left after trimming. */
        if (base + raw - at > BLK_MAX) {
            blob_free(&drv);
            return fail("%s: no " BLK_END " in the first %d bytes", file,
                        BLK_MAX);
        }
        /* A NUL, before anything else looks at the line and before comments are
         * let off the printable rule, because this one is not about what the
         * screen can draw. skidset reads the block into a buffer and walks it
         * as a C string, so a NUL is the end of the text: everything after it,
         * SKIDSETEND included, is invisible and the block is refused for having
         * no terminator. Exempting comments from 20h..7Eh let one hide a NUL
         * here and be accepted, which is the byte most likely to be there by
         * accident, since the block sits in a binary. */
        for (j = 0; j < len; j++) {
            if (drv.data[base + j] == '\0') {
                blob_free(&drv);
                return fail("%s: a NUL ends the block where skidset reads it",
                            file);
            }
        }
        memcpy(line, drv.data + base, len);
        line[len] = '\0';
        /* By what the line actually occupied. Advancing by len + 1 assumed a
         * delimiter that an unterminated final line does not have, and put the
         * cursor one byte past the image. */
        i = base + raw;
        b.lines++;

        /* The terminator before anything is trimmed, because it is a whole
         * line. One with a space in front of it is not the terminator and falls
         * through to the rule below, which names the fault rather than letting
         * the block run on to its real end. */
        if (strcmp(line, BLK_END) == 0) {
            /* And its newline, always. A parser handed a NUL terminated copy of
             * a chunk cannot tell the end of the file from a NUL inside the
             * binary, nor from a terminator landing on the last byte it read,
             * so skidset requires the byte rather than guessing. A block
             * accepted without it here is one skidset refuses. */
            if (!has_lf) {
                blob_free(&drv);
                return fail("%s: " BLK_END " needs its newline", file);
            }
            end = i;
            break;
        }
        /* The magic, compared here as a whole line rather than taken on the
         * strength of the search. The search matches twelve bytes and a
         * delimiter; that is not the same claim as the line saying only the
         * magic, and treating it as though it were accepted SKIDSETDRV01\rX,
         * where the CR is content and the first line is fourteen characters
         * long. Untrimmed, because the grammar gives the token a line of its
         * own and anything beside it, space or otherwise, means this is not a
         * block. */
        if (b.lines == 1) {
            if (strcmp(line, BLK_MAGIC) != 0) {
                blob_free(&drv);
                return fail("%s: not a driver block, the first line is not "
                            "exactly " BLK_MAGIC,
                            file);
            }
            continue;
        }
        /* Neither token appears anywhere else in the block, in a comment no
         * more than in a value. Nothing here needs the rule, since the compare
         * above is a whole line: it is for readers that find a block by
         * searching bytes rather than by comparing lines, which get a block
         * carrying a token wrong, and for the terminator get it wrong silently,
         * because a truncated block looks exactly like one that ends early. */
        if (strstr(line, BLK_END) != NULL) {
            blob_free(&drv);
            return fail("%s: " BLK_END " must be a line of its own", file);
        }
        if (strstr(line, BLK_MAGIC) != NULL) {
            blob_free(&drv);
            return fail("%s: " BLK_MAGIC " must be the block's first line and "
                        "appear once",
                        file);
        }
        /* A CR that is not part of a line ending, comments included. The
         * printable rule below lets a comment through and this must not: the
         * specification drops a CR immediately before the LF and refuses it
         * anywhere else, so one buried in a comment would be taken here and
         * refused by a reader that believed the sentence. */
        for (j = 0; j < len; j++) {
            if (drv.data[base + j] == '\r') {
                blob_free(&drv);
                return fail("%s: a CR belongs only immediately before the LF",
                            file);
            }
        }
        /* The blanks off both ends, as skidset trims them, so the rule below
         * sees the line the way it does. A tab at either end is trimmed and
         * legal; one in the middle is not, and the check catches it there. */
        s = 0;
        while (s < len &&
               (drv.data[base + s] == ' ' || drv.data[base + s] == '\t')) {
            s++;
        }
        e = len;
        while (e > s && (drv.data[base + e - 1] == ' ' ||
                         drv.data[base + e - 1] == '\t')) {
            e--;
        }
        /* 20h to 7Eh, and comments are exempt. The rule is about what reaches
         * the screen, so a comment may hold whatever the author's editor put
         * there; DEL and the control codes are refused, tab among them, which
         * "seven-bit ASCII" would have allowed.
         *
         * Decided from the raw bytes rather than the copy, and the copy is why:
         * an embedded NUL ends it as a C string and would leave the rest of the
         * physical line unexamined. That byte is the one most likely to be
         * there by accident, because the block sits in a binary. */
        if (s == e || drv.data[base + s] != ';') {
            for (j = s; j < e; j++) {
                if (drv.data[base + j] < 0x20 || drv.data[base + j] > 0x7E) {
                    blob_free(&drv);
                    return fail("%s: a value holds a byte outside 20h to 7Eh",
                                file);
                }
            }
        }

        trim(line);
        if (line[0] == '\0' || line[0] == ';') {
            continue;
        }

        val = strchr(line, ' ');
        if (val != NULL) {
            *val++ = '\0';
            trim(val);
        } else {
            val = line + strlen(line);
        }

        if (strcmp(line, "sound") == 0 || strcmp(line, "video") == 0) {
            /* Neither takes one. A value on the line that says which menu the
             * driver belongs to is a key somebody meant to write and did not,
             * and skidset says so rather than passing over it. */
            if (val[0] != '\0') {
                blob_free(&drv);
                return fail("%s: sound and video take no value", file);
            }
            if (line[0] == 's') {
                b.sound++;
            } else {
                b.video++;
            }
        } else if (strcmp(line, "label") == 0) {
            if (set_once(b.label, val, "label", file) != 0) {
                blob_free(&drv);
                return 1;
            }
        } else if (strcmp(line, "brief") == 0) {
            if (set_once(b.brief, val, "brief", file) != 0) {
                blob_free(&drv);
                return 1;
            }
        } else if (strcmp(line, "mode") == 0) {
            if (set_once(b.mode, val, "mode", file) != 0) {
                blob_free(&drv);
                return 1;
            }
        } else if (strcmp(line, "help") == 0) {
            /* Once, like every other key. It used to repeat and the values were
             * joined; a second one is a duplicate now, and skidset says so. */
            if (set_once(b.help, val, "help", file) != 0) {
                blob_free(&drv);
                return 1;
            }
        } else {
            /* A key skidset does not know is ignored, not refused, so a later
             * format can add one without stranding drivers already shipped.
             * The cost is that a misspelt key is silent here too, which the
             * format accepts: misspell a required one and the block is refused
             * for that key being missing, and misspell help and the row says
             * No Help Available. */
            b.unknown++;
        }
    }

    if (end == 0) {
        blob_free(&drv);
        return fail("%s: no " BLK_END " in the first %d bytes", file, BLK_MAX);
    }
    /* The one-block rule is settled by the scan above, which counted every
     * delimited block in the file before this one was read. */
    if (b.sound + b.video != 1) {
        blob_free(&drv);
        return fail("%s: needs exactly one of sound or video", file);
    }
    if (b.label[0] == '\0') {
        blob_free(&drv);
        return fail("%s: no label", file);
    }
    if (b.brief[0] == '\0') {
        blob_free(&drv);
        return fail("%s: no brief", file);
    }
    lim = b.sound ? BLK_SOUND_LBL : BLK_VIDEO_LBL;
    if ((int)strlen(b.label) > lim) {
        blob_free(&drv);
        return fail("%s: label is %lu characters, the limit is %d", file,
                    (unsigned long)strlen(b.label), lim);
    }
    if ((int)strlen(b.brief) > BLK_BRIEF) {
        blob_free(&drv);
        return fail("%s: brief is %lu characters, the limit is %d", file,
                    (unsigned long)strlen(b.brief), BLK_BRIEF);
    }
    /* brief is no longer checked for brackets. skidset draws its own around
     * whatever it is given, so a brief carrying one comes out with both. */
    if (b.video && b.mode[0] == '\0') {
        blob_free(&drv);
        return fail("%s: a video block needs a mode", file);
    }
    if (b.sound && b.mode[0] != '\0') {
        blob_free(&drv);
        return fail("%s: mode belongs to a video block", file);
    }
    if (b.mode[0] != '\0' && (int)strlen(b.mode) > BLK_MODE) {
        blob_free(&drv);
        return fail("%s: mode is %lu characters, the limit is %d", file,
                    (unsigned long)strlen(b.mode), BLK_MODE);
    }
    if (b.mode[0] != '\0' && !dos_name(b.mode)) {
        blob_free(&drv);
        return fail("%s: the first word of mode is not a name DOS can hold",
                    file);
    }
    if (longest_word(b.help) > BLK_WORD) {
        blob_free(&drv);
        return fail("%s: a help word is %d characters, the limit is %d", file,
                    longest_word(b.help), BLK_WORD);
    }
    rows = wrap(b.help, 0);
    /* -1 is a word no break can fit, which longest_word above has already
     * named. Reaching here with one would mean the two disagree about what
     * fits, so it is a refusal rather than a row count of minus one. */
    if (rows < 0) {
        blob_free(&drv);
        return fail("%s: a help word cannot be broken to fit %d columns", file,
                    BLK_COLS);
    }
    if (rows > BLK_ROWS) {
        blob_free(&drv);
        return fail("%s: help wraps to %d lines, the limit is %d", file, rows,
                    BLK_ROWS);
    }

    printf("%s\n", file);
    printf("  block   at %lu, %lu bytes, %d lines\n", at,
           (unsigned long)(end - at), b.lines);
    printf("  menu    %s\n", b.sound ? "sound" : "video");
    /* Named rather than merely tolerated: skidset passes over a key it does not
     * know, so a typo is invisible on the setup screen and this is the one
     * place it can be seen. */
    if (b.unknown > 0) {
        printf("  ignored %d key%s skidset does not know\n", b.unknown,
               b.unknown == 1 ? "" : "s");
    }
    printf("  label   %-24s %lu of %d\n", b.label,
           (unsigned long)strlen(b.label), lim);
    printf("  brief   %-24s %lu of %d\n", b.brief,
           (unsigned long)strlen(b.brief), BLK_BRIEF);
    if (b.mode[0] != '\0') {
        printf("  mode    %-24s %lu of %d\n", b.mode,
               (unsigned long)strlen(b.mode), BLK_MODE);
    }
    if (b.help[0] == '\0') {
        printf("  help    none, skidset shows No Help Available\n");
    } else {
        /* The line, not the value, because BLK_LINE is a line limit and the
         * key and its space are part of it. "help " is five characters. */
        printf("  help    %lu character line of %d, %d of %d lines at %d "
               "columns\n",
               (unsigned long)strlen(b.help) + 5, BLK_LINE, rows, BLK_ROWS,
               BLK_COLS);
        wrap(b.help, 1);
    }
    blob_free(&drv);
    return 0;
}
