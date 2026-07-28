/* sc15's self-check.
 *
 *     selfcheck
 *
 * mkvce copies instrument records out of the game's own MT-32 voice banks,
 * which this project does not redistribute, so there is no shipped input to
 * test against. This synthesises one instead: a two instrument bank with a
 * distinctive value in every field mkvce is supposed to copy, overwrite or
 * leave alone, and then reads those fields back out of the result.
 *
 * It also checks that cfg finds the configuration block inside a driver image
 * and patches the right bytes, and that a spread of malformed input is refused
 * rather than half-processed.
 *
 * It is C89 like the rest and builds with the same compiler, so a period
 * machine can check its own build of the tooling.
 *
 * It links cmd_mkvce() and cmd_cfg() and calls them in this process, so a
 * refusal is read from the return value the code produced. That leaves main()'s
 * dispatch uncovered: twelve lines that pick a subcommand by name and print
 * usage. The argument handling behind it is exercised here; the strcmp is not.
 *
 * Every file it writes is removed before it returns, and the names are 8.3 so
 * DOS keeps them intact.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sc15.h"
#include "version.h"

/* The synthetic bank. Two records of the size the MT-32 banks use, behind the
 * container header audioresource_find() expects: a 32-bit total, a 16-bit
 * count, then one four character name and one 32-bit offset per instrument. */
#define NINST 2
#define REC 93
#define HDR (6 + 8 * NINST)
#define BANKSIZE (HDR + REC * NINST)

/* Written into the source bank, read back out of mkvce's output. The point of
 * each one is that no other field carries the same value, so a record copied
 * to the wrong offset cannot pass by coincidence. */
#define SRC_TRANSPOSE 12
#define SRC_BENDRANGE 2
#define SRC_CHANNEL 3 /* plus the instrument index */
#define SRC_PROGRAM 30
#define SRC_VOLUME 100
#define SRC_PAN 70

static const char *BANK = "SCTEST.VCE";
static const char *DRV = "SCTEST.DRV";
static const char *SPEC = "SCTEST.TXT";
static const char *OUT = "SCTEST.OUT";
static const char *SONG = "SCTEST.KMS";

static int failures = 0;

/* -------------------------------------------------------------- plumbing -- */

static void put16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static unsigned long get32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static int write_bytes(const char *path, const unsigned char *p, size_t n)
{
    FILE  *f = fopen(path, "wb");
    size_t put;

    if (f == NULL) {
        return 1;
    }
    put = fwrite(p, 1, n, f);
    return fclose(f) != 0 || put != n;
}

static int write_text(const char *path, const char *s)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL) {
        return 1;
    }
    fputs(s, f);
    return fclose(f) != 0;
}

/* A refused conversion leaves no output file, which is the only signal that
 * reaches us on every host. */
static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

static long read_bytes(const char *path, unsigned char *buf, long max)
{
    FILE *f = fopen(path, "rb");
    long  n;

    if (f == NULL) {
        return -1;
    }
    n = (long)fread(buf, 1, (size_t)max, f);
    fclose(f);
    return n;
}

/* The two subcommands take char ** because that is the shape main() has to
 * hand them. Rather than casting const away from a string literal, copy each
 * argument into writable storage, which is what a real argv points at. */
#define ARGMAX 3
#define ARGLEN 64

static char argbuf[ARGMAX][ARGLEN];

static char *arg(int slot, const char *s)
{
    strncpy(argbuf[slot], s, ARGLEN - 1);
    argbuf[slot][ARGLEN - 1] = '\0';
    return argbuf[slot];
}

/* Called the way main() calls them: argv without the program name or the
 * subcommand, and a return of 0 for success. */
static int mkvce(const char *spec, const char *out)
{
    char *argv[2];

    argv[0] = arg(0, spec);
    argv[1] = arg(1, out);
    return cmd_mkvce(2, argv);
}

static int cfg(const char *drv, const char *a, const char *b)
{
    char *argv[ARGMAX];
    int   n = 1;

    argv[0] = arg(0, drv);
    if (a != NULL) {
        argv[n] = arg(n, a);
        n++;
    }
    if (b != NULL) {
        argv[n] = arg(n, b);
        n++;
    }
    return cmd_cfg(n, argv);
}

/* int, not long: every value compared here is a single byte or the 208 byte
 * bank size. Widening them would cost a conversion at all thirty call sites,
 * which a 16-bit compiler reports one by one. */
static void expect(const char *what, int got, int want)
{
    if (got != want) {
        printf("FAIL  %s is %d, expected %d\n", what, got, want);
        failures++;
    } else {
        printf("ok    %s is %d\n", what, got);
    }
}

/* ------------------------------------------------------------ the inputs -- */

static void build_bank(unsigned char *b)
{
    int r;

    /* The suffixes are not decoration: the container's size and offset fields
     * are 32-bit, and on a 16-bit compiler an unsuffixed constant is an int. */
    memset(b, 0, BANKSIZE);
    put32(b, (unsigned long)BANKSIZE);
    put16(b + 4, NINST);
    memcpy(b + 6, "BASSHRN1", 8);
    put32(b + 14, 0UL);
    put32(b + 18, (unsigned long)REC);

    for (r = 0; r < NINST; r++) {
        unsigned char *p = b + HDR + REC * r;
        put16(p + I_SIZE, REC);
        p[I_TRANSPOSE] = SRC_TRANSPOSE;
        p[I_BENDRANGE] = SRC_BENDRANGE;
        p[I_CHANNEL] = (unsigned char)(SRC_CHANNEL + r);
        p[I_PROGRAM] = (unsigned char)(SRC_PROGRAM + r);
        p[I_VOLUME] = SRC_VOLUME;
        p[I_PAN] = SRC_PAN;
    }
}

/* Enough of a driver image for cfg to work on: the signature at 45h and the
 * block behind it. Not runnable code and not meant to be, since cfg only ever
 * reads and patches those bytes. */
static void build_driver(unsigned char *d, size_t n)
{
    memset(d, 0, n);
    memcpy(d + CFG_OFS, CFG_SIG, CFG_SIG_LEN);
    put16(d + CFG_PORT, 0x0330);
    d[CFG_GM] = 1;
    d[CFG_GS] = 1;
    d[CFG_VOLUME] = 100;
}

/* --------------------------------------------------------------- the run -- */

/* One instrument's byte at one offset, out of the generated bank. */
static int field(const unsigned char *out, int inst, int ofs)
{
    return out[HDR + REC * inst + ofs];
}

static void check_mkvce(void)
{
    unsigned char out[BANKSIZE];
    long          n;
    int           rc;

    /* BASS overrides pan and transpose and asks for a GS bank; HRN1 leaves
     * everything at "=" so the source values have to survive. volumescale
     * applies to both. */
    if (write_text(SPEC, "source SCTEST.VCE\n"
                         "volumescale 0.5\n"
                         "BASS BASS 38 = 64 = = 1 transpose=61\n"
                         "HRN1 HRN1 60 = =  = = 0\n") != 0) {
        printf("FAIL  cannot write %s\n", SPEC);
        failures++;
        return;
    }

    remove(OUT);
    rc = mkvce(SPEC, OUT);
    expect("mkvce reports success", rc, 0);
    n = read_bytes(OUT, out, (long)sizeof out);
    if (n != BANKSIZE) {
        printf("FAIL  mkvce produced %ld bytes, expected %d\n", n, BANKSIZE);
        failures++;
        return;
    }

    /* Taken from the specification. */
    expect("BASS program", field(out, 0, I_PROGRAM), 38);
    expect("HRN1 program", field(out, 1, I_PROGRAM), 60);
    /* The source says 100 and volumescale is 0.5. */
    expect("BASS volume after volumescale", field(out, 0, I_VOLUME), 50);
    expect("HRN1 volume after volumescale", field(out, 1, I_VOLUME), 50);
    /* Explicit on one, "=" keeps the source value on the other. */
    expect("BASS pan, explicit", field(out, 0, I_PAN), 64);
    expect("HRN1 pan, kept from source", field(out, 1, I_PAN), SRC_PAN);
    /* "=" on both, so the two different source channels have to survive. */
    expect("BASS channel, kept from source", field(out, 0, I_CHANNEL),
           SRC_CHANNEL);
    expect("HRN1 channel, kept from source", field(out, 1, I_CHANNEL),
           SRC_CHANNEL + 1);
    expect("BASS bend range, kept from source", field(out, 0, I_BENDRANGE),
           SRC_BENDRANGE);
    /* An SC15 extension: absent from an MT-32 record, written from the spec. */
    expect("BASS bank", field(out, 0, I_BANK), 1);
    expect("HRN1 bank", field(out, 1, I_BANK), 0);
    /* Overridden by a trailing name=value on one, kept on the other. */
    expect("BASS transpose, overridden", field(out, 0, I_TRANSPOSE), 61);
    expect("HRN1 transpose, kept from source", field(out, 1, I_TRANSPOSE),
           SRC_TRANSPOSE);

    /* The container header has to describe what follows. Both narrowed to int
     * only after the length was checked against BANKSIZE above, so neither can
     * be larger than 208. */
    expect("instrument count", out[VCE_COUNT_OFS], NINST);
    expect("size field in the header", (int)get32(out), BANKSIZE);
    expect("file size matches the header", (int)n, BANKSIZE);
}

static void check_cfg(void)
{
    unsigned char drv[128];
    long          n;

    build_driver(drv, sizeof drv);
    if (write_bytes(DRV, drv, sizeof drv) != 0) {
        printf("FAIL  cannot write %s\n", DRV);
        failures++;
        return;
    }

    expect("cfg reports success", cfg(DRV, "port=300", "volume=80"), 0);
    n = read_bytes(DRV, drv, (long)sizeof drv);
    if (n != (long)sizeof drv) {
        printf("FAIL  cfg changed the size of the driver image\n");
        failures++;
        return;
    }
    expect("cfg wrote the port, low byte", drv[CFG_PORT], 0x00);
    expect("cfg wrote the port, high byte", drv[CFG_PORT + 1], 0x03);
    /* cfg reads ports as hex without a prefix, the way DOS documentation
     * does, so "port=300" has to mean 300h and not 300 decimal. */
    expect("cfg wrote the volume", drv[CFG_VOLUME], 80);
    /* Fields nobody named must not move. */
    expect("cfg left the GM flag alone", drv[CFG_GM], 1);
    expect("cfg left the GS flag alone", drv[CFG_GS], 1);

    /* The ranges the driver honours, not the ones a byte can hold. A volume of
     * 255 was stored and reported as 255 while the driver clamped it to 100, so
     * the tool described a configuration that never existed. A port of FFFFh
     * wrapped to 0000h when the driver added one for the status register. */
    expect("cfg refuses a volume above 100", cfg(DRV, "volume=101", NULL) != 0,
           1);
    expect("cfg refuses a GM flag that is not 0 or 1",
           cfg(DRV, "gm=2", NULL) != 0, 1);
    expect("cfg refuses a GS flag that is not 0 or 1",
           cfg(DRV, "gs=2", NULL) != 0, 1);
    expect("cfg refuses a port with no room for its status register",
           cfg(DRV, "port=FFFF", NULL) != 0, 1);

    n = read_bytes(DRV, drv, (long)sizeof drv);
    expect("a refused value left the image alone", n == (long)sizeof drv, 1);
    expect("the volume is still what the accepted call wrote", drv[CFG_VOLUME],
           80);
}

/* ------------------------------------------------- the skidset block ------ */

/* A driver is only a file with the block somewhere inside it, so a few bytes of
 * filler stand in for the code: skidset searches the whole image and stops at
 * the first magic, and so does this. */
static int write_block(const char *body)
{
    char buf[1024];

    strcpy(buf, "\001\002binary\003");
    strcat(buf, body);
    return write_text(DRV, buf);
}

static char *block_argv(void)
{
    return arg(0, DRV);
}

static void block_ok(const char *what, const char *body)
{
    char *argv[1];

    argv[0] = block_argv();
    if (write_block(body) != 0) {
        printf("FAIL  cannot write %s\n", DRV);
        failures++;
        return;
    }
    if (cmd_block(1, argv) != 0) {
        printf("FAIL  %s was refused\n", what);
        failures++;
    } else {
        printf("ok    %s is accepted\n", what);
    }
}

static void block_bad(const char *what, const char *body)
{
    char *argv[1];

    argv[0] = block_argv();
    if (write_block(body) != 0) {
        printf("FAIL  cannot write %s\n", DRV);
        failures++;
        return;
    }
    if (cmd_block(1, argv) == 0) {
        printf("FAIL  %s was accepted\n", what);
        failures++;
    } else {
        printf("ok    %s is refused\n", what);
    }
}

static void check_block(void)
{
    block_ok("a minimal sound block",
             "SKIDSETDRV01\nsound\nlabel Roland SC-55\nbrief SC-55\n"
             "SKIDSETEND\n");
    block_ok("a block with help and comments",
             "SKIDSETDRV01\n; who made it\nsound\nlabel Roland SC-55\n"
             "brief SC-55\nhelp One paragraph on one key.\nSKIDSETEND\n");
    block_ok("a video block with a mode",
             "SKIDSETDRV01\nvideo\nlabel SVGA graphics\nbrief SVGA\n"
             "mode SVGA\nSKIDSETEND\n");
    /* A bracket in a brief is no longer refused. skidset draws its own around
     * whatever it is handed, so this comes out with both, which is the writer's
     * business rather than the format's. */
    block_ok("a brief holding a bracket",
             "SKIDSETDRV01\nsound\nlabel A\nbrief (X)\nSKIDSETEND\n");
    /* The 0Ah after the terminator is not optional either, though this one
     * fails loudly. It was taken for a while on the grounds that a block ending
     * the file has no room for it, and skidset cannot tell that case from any
     * other: it reads a chunk into a buffer, so a NUL inside the binary and a
     * terminator landing on the last byte of the 1024 both look like the end of
     * the file. Requiring the byte costs a driver nothing. */
    block_bad("no newline after the terminator",
              "SKIDSETDRV01\nsound\nlabel A\nbrief X\nSKIDSETEND");
    /* The 0Ah before it is not optional, and this is the one that fails
     * quietly. Without it the value swallows the terminator, the block has no
     * SKIDSETEND, and a driver built that way is passed over with nothing on
     * screen naming it. In a db build it is the difference between
     *
     *     db 'help ...', 'SKIDSETEND', 10
     *     db 'help ...', 10, 'SKIDSETEND', 10
     */
    block_bad("no newline before the terminator",
              "SKIDSETDRV01\nsound\nlabel A\nbrief X\n"
              "help One.SKIDSETEND\n");
    /* CR before the LF is accepted and ignored, so a block written by a DOS
     * editor still reads. Nothing this project builds emits one. */
    block_ok("a block with CRLF line endings",
             "SKIDSETDRV01\r\nsound\r\nlabel Roland SC-55\r\nbrief SC-55\r\n"
             "SKIDSETEND\r\n");

    block_bad("a driver with no block", "no magic anywhere in here\n");
    block_bad("a block with no terminator",
              "SKIDSETDRV01\nsound\nlabel A\nbrief X\n");
    block_bad("a block with neither sound nor video",
              "SKIDSETDRV01\nlabel A\nbrief X\nSKIDSETEND\n");
    block_bad("a block with both sound and video",
              "SKIDSETDRV01\nsound\nvideo\nlabel A\nbrief X\nmode S\n"
              "SKIDSETEND\n");
    block_bad("a block with no label",
              "SKIDSETDRV01\nsound\nbrief X\nSKIDSETEND\n");
    block_bad("a block with no brief",
              "SKIDSETDRV01\nsound\nlabel A\nSKIDSETEND\n");
    block_bad("a label over 31 characters",
              "SKIDSETDRV01\nsound\nlabel 01234567890123456789012345678901\n"
              "brief X\nSKIDSETEND\n");
    block_bad("a brief over 21 characters",
              "SKIDSETDRV01\nsound\nlabel A\nbrief 0123456789012345678901\n"
              "SKIDSETEND\n");
    block_bad("a key given twice",
              "SKIDSETDRV01\nsound\nlabel A\nlabel B\nbrief X\nSKIDSETEND\n");
    /* help is one key now, carrying the whole paragraph, so a second one is a
     * duplicate like any other. It used to repeat and the values were joined.
     */
    block_bad("help given twice",
              "SKIDSETDRV01\nsound\nlabel A\nbrief X\nhelp One.\nhelp Two.\n"
              "SKIDSETEND\n");
    block_bad("a video block with no mode",
              "SKIDSETDRV01\nvideo\nlabel A\nbrief X\nSKIDSETEND\n");
    block_bad("a sound block carrying a mode",
              "SKIDSETDRV01\nsound\nlabel A\nbrief X\nmode SVGA\nSKIDSETEND\n");
    /* disk left the format. It was video only and always refused on a sound
     * block, so it is an unknown key now and passed over like any other. */
    block_ok("a disk key, which the format no longer has",
             "SKIDSETDRV01\nvideo\nlabel A\nbrief X\nmode S\ndisk C\n"
             "SKIDSETEND\n");
    /* Tab is outside 20h to 7Eh, which is the rule for a value. */
    block_bad("a tab inside a value",
              "SKIDSETDRV01\nsound\nlabel A\tB\nbrief X\nSKIDSETEND\n");
    /* 27 characters, one past what the 26 column window can wrap. */
    block_bad("an unwrappable help word",
              "SKIDSETDRV01\nsound\nlabel A\nbrief X\n"
              "help 012345678901234567890123456\nSKIDSETEND\n");
    /* 26 exactly, which fits and must not be refused. */
    block_ok("a help word of exactly 26 characters",
             "SKIDSETDRV01\nsound\nlabel A\nbrief X\n"
             "help 01234567890123456789012345\nSKIDSETEND\n");
    /* A key skidset does not know is passed over rather than refused, so that a
     * later format can add one without stranding a driver already shipped. */
    block_ok("an unknown key, which is ignored",
             "SKIDSETDRV01\nsound\nlabel A\nbrief X\ncolour red\n"
             "SKIDSETEND\n");
    /* Which is also why a misspelt required key reads as a missing one. */
    block_bad("a misspelt label, refused as a missing one",
              "SKIDSETDRV01\nsound\nlabe1 A\nbrief X\nSKIDSETEND\n");

    /* Each token is a whole line, appears once, and its letters appear nowhere
     * else in the block, a comment no more than a value. Nothing in a reader
     * that compares whole lines needs the rule; it is for readers that find a
     * block by searching bytes, which is what sc15 and skidset both do to
     * locate it in the first place. Such a reader handed a comment with the
     * terminator inside it reads a block that stops early, and cannot tell that
     * is what happened, because a truncated block looks exactly like one that
     * ends there. */
    block_bad("the terminator indented",
              "SKIDSETDRV01\nsound\nlabel A\nbrief X\n  SKIDSETEND\n");
    block_bad("the terminator inside a comment",
              "SKIDSETDRV01\nsound\n; it ends with SKIDSETEND\nlabel A\n"
              "brief X\nSKIDSETEND\n");
    block_bad("the terminator inside a value",
              "SKIDSETDRV01\nsound\nlabel A SKIDSETEND\nbrief X\n"
              "SKIDSETEND\n");
    block_bad("the magic a second time",
              "SKIDSETDRV01\nsound\nSKIDSETDRV01\nlabel A\nbrief X\n"
              "SKIDSETEND\n");
    /* A CR is dropped immediately before the LF and refused anywhere else, and
     * a comment is not exempt from that one the way it is from the printable
     * rule. Without this a CR buried in a comment is taken here and refused by
     * a reader that believed the specification. */
    block_bad("a CR inside a comment",
              "SKIDSETDRV01\nsound\n; one\rtwo\nlabel A\nbrief X\n"
              "SKIDSETEND\n");
    /* The line naming the menu takes no value, so something after it is a key
     * somebody meant to write and did not. */
    block_bad("a value on sound",
              "SKIDSETDRV01\nsound yes\nlabel A\nbrief X\nSKIDSETEND\n");
    /* The first word of a mode names NAME.COD. Nine characters is one too many
     * and the ninth would go missing where the file is looked for rather than
     * where the block is read, which is a long way from the fault. */
    block_bad("a mode whose first word is nine characters",
              "SKIDSETDRV01\nvideo\nlabel A\nbrief X\nmode 123456789\n"
              "SKIDSETEND\n");
    block_bad("a mode whose first word holds a path separator",
              "SKIDSETDRV01\nvideo\nlabel A\nbrief X\nmode A\\B\n"
              "SKIDSETEND\n");
    /* Blanks come off both ends of a line before anything looks at it, and a
     * tab is a blank. Only the two token lines are compared as they stand. This
     * was refused while the printable rule was applied to the whole line rather
     * than to what was left of it. */
    block_ok("a value indented with a tab",
             "SKIDSETDRV01\nsound\n\tlabel A\n\tbrief X\nSKIDSETEND\n");
    /* One block to a driver: its switch is its filename, so a second could only
     * claim the same one, and skidset refuses the file whole. Reading the first
     * and stopping, which is the obvious thing for a checker to do, reports a
     * driver as sound that will not appear on the menu at all. */
    block_bad("a driver carrying two blocks",
              "SKIDSETDRV01\nsound\nlabel A\nbrief X\nSKIDSETEND\n"
              "SKIDSETDRV01\nsound\nlabel B\nbrief Y\nSKIDSETEND\n");
    /* And the magic alone is not a second block. Twelve bytes of it can turn up
     * in a driver's code, and refusing a file over that would be worse than the
     * fault it looks for. */
    block_ok("the magic again with nothing terminating it",
             "SKIDSETDRV01\nsound\nlabel A\nbrief X\nSKIDSETEND\n"
             "SKIDSETDRV01\nand then some code\n");
    /* Twelve matching bytes and a CR is not the magic on a line of its own. A
     * CR is dropped only when an LF follows it, so the first line here is
     * fourteen characters and skidset answers "not a driver block". This was
     * accepted while the search took either delimiter and the parse trusted the
     * search: the first line was skipped unread, and the valid keys behind it
     * carried the block. */
    block_bad("the magic followed by a bare CR and more text",
              "SKIDSETDRV01\rX\nsound\nlabel A\nbrief X\nSKIDSETEND\n");

    /* The two cases text cannot express: a NUL inside a line, and a block that
     * lands exactly on the size limit. Written as bytes, because write_text
     * builds its buffer with strcat and would stop at the NUL. */
    {
        static unsigned char raw[4096];
        char                *argv[1];
        size_t               n;
        int                  pad;

        argv[0] = block_argv();

        /* A value whose text hides a NUL. Walking the copied line as a string
         * stops there and would leave the rest of the physical line unexamined,
         * so the check reads the raw bytes. */
        n = 0;
        memcpy(raw + n, "SKIDSETDRV01\nsound\n", 19);
        n += 19;
        memcpy(raw + n, "label A\0B\n", 10);
        n += 10;
        memcpy(raw + n, "brief X\nSKIDSETEND\n", 19);
        n += 19;
        if (write_bytes(DRV, raw, n) == 0) {
            expect("a NUL inside a value is refused", cmd_block(1, argv) != 0,
                   1);
        }

        /* And the same NUL in a comment, which this used to accept on the
         * grounds that a comment is exempt from the printable rule. It is
         * exempt from that one, which is about what the screen can draw, and
         * not from this one, which is about where the text ends: skidset reads
         * the block into a buffer and walks it as a C string, so everything
         * past the NUL, SKIDSETEND included, is invisible and the block is
         * refused for having no terminator. Checked against skidset's own
         * reader, which says exactly that. */
        n = 0;
        memcpy(raw + n, "SKIDSETDRV01\n", 13);
        n += 13;
        memcpy(raw + n, "; a\0b\n", 6);
        n += 6;
        memcpy(raw + n, "sound\nlabel A\nbrief X\nSKIDSETEND\n", 33);
        n += 33;
        if (write_bytes(DRV, raw, n) == 0) {
            expect("the same NUL inside a comment is refused",
                   cmd_block(1, argv) != 0, 1);
        }

        /* Help whose words are separated by runs of spaces. Sixteen one
         * character words, twenty-five spaces between each: 391 characters, so
         * the 448 line limit does not reach it, and every word is one character
         * so the unwrappable-word rule cannot either. Nothing but the wrapping
         * decides it.
         *
         * skidset skips spaces only at the start of a row and copies an
         * internal run out whole, so each run fills a row on its own and the
         * paragraph needs sixteen, one past the window. The wrapper here used
         * to tokenise into words and rebuild each row with one space between
         * them, which measured the whole thing as 2 rows and reported a block
         * skidset refuses as fitting the window twice over. */
        {
            static const char head[] =
                "SKIDSETDRV01\nsound\nlabel A\nbrief X\nhelp ";
            static const char tail[] = "\nSKIDSETEND\n";
            int               w, k;

            n = sizeof head - 1;
            memcpy(raw, head, n);
            for (w = 0; w < 16; w++) {
                if (w > 0) {
                    for (k = 0; k < 25; k++) {
                        raw[n++] = ' ';
                    }
                }
                raw[n++] = 'a';
            }
            memcpy(raw + n, tail, sizeof tail - 1);
            n += sizeof tail - 1;
            if (write_bytes(DRV, raw, n) == 0) {
                expect("help whose spaces fill sixteen rows is refused",
                       cmd_block(1, argv) != 0, 1);
            }
        }

        /* The line limit, at 448 and one past it. Measured on a comment rather
         * than on help, because a 448 character paragraph wraps to far more
         * than the fifteen rows the window has and would be refused for that
         * instead, which would prove nothing about the length. */
        for (pad = 0; pad < 2; pad++) {
            size_t want = 448 + (size_t)pad;
            size_t k;

            n = 0;
            memcpy(raw + n, "SKIDSETDRV01\nsound\nlabel A\nbrief X\n", 35);
            n += 35;
            raw[n++] = ';';
            for (k = 1; k < want; k++) {
                raw[n++] = 'x';
            }
            raw[n++] = '\n';
            memcpy(raw + n, "SKIDSETEND\n", 11);
            n += 11;
            if (write_bytes(DRV, raw, n) != 0) {
                continue;
            }
            if (pad == 0) {
                expect("a line of exactly 448 characters is accepted",
                       cmd_block(1, argv), 0);
            } else {
                expect("a line of 449 is refused", cmd_block(1, argv) != 0, 1);
            }
        }

        /* No limit on the number of lines. The byte limit already bounds the
         * block, and a second one only decided how many comments a driver was
         * allowed, by a number with no reason recorded for it. sc15 capped it
         * at 64 and so refused blocks skidset takes. Eighty comments, two bytes
         * each, well inside the size limit. */
        {
            static const char head[] =
                "SKIDSETDRV01\nsound\nlabel A\nbrief X\n";
            static const char tail[] = "SKIDSETEND\n";
            int               k;

            n = sizeof head - 1;
            memcpy(raw, head, n);
            for (k = 0; k < 80; k++) {
                raw[n++] = ';';
                raw[n++] = '\n';
            }
            memcpy(raw + n, tail, sizeof tail - 1);
            n += sizeof tail - 1;
            if (write_bytes(DRV, raw, n) == 0) {
                expect("a block of eighty comment lines is accepted",
                       cmd_block(1, argv) == 0, 1);
            }
        }

        /* Exactly BLK_MAX, then one byte more. The limit counts every byte the
         * block occupies, so the padding is measured with its line endings. */
        for (pad = 0; pad < 2; pad++) {
            static const char head[] =
                "SKIDSETDRV01\nsound\nlabel A\nbrief X\n";
            static const char tail[] = "SKIDSETEND\n";
            size_t            want = 1024 + (size_t)pad;
            size_t            fill;
            size_t            k;

            n = sizeof head - 1;
            memcpy(raw, head, n);
            /* 100 byte comment lines until fewer than two lines' worth remains,
             * then one sized to land on the target exactly. The shortest a line
             * can be is two bytes, a semicolon and its LF. */
            fill = want - n - (sizeof tail - 1);
            while (fill >= 102) {
                raw[n++] = ';';
                for (k = 1; k < 99; k++) {
                    raw[n++] = ' ';
                }
                raw[n++] = '\n';
                fill -= 100;
            }
            raw[n++] = ';';
            for (k = 2; k < fill; k++) {
                raw[n++] = ' ';
            }
            raw[n++] = '\n';
            memcpy(raw + n, tail, sizeof tail - 1);
            n += sizeof tail - 1;
            if (write_bytes(DRV, raw, n) != 0) {
                continue;
            }
            if (pad == 0) {
                expect("a block of exactly 1024 bytes is accepted",
                       cmd_block(1, argv) == 0, 1);
            } else {
                expect("a block of 1025 bytes is refused",
                       cmd_block(1, argv) != 0, 1);
            }
        }
    }

    if (cmd_block(0, NULL) == 0) {
        printf("FAIL  block with no arguments returned success\n");
        failures++;
    } else {
        printf("ok    block with no arguments is refused\n");
    }
    remove(DRV);
}

/* A song, synthesised. kms was the one subcommand the self-check could not
 * reach, on the grounds that it needs a song and songs are game data this
 * repository does not carry. That is true of a real song and not of a made up
 * one: the container is a header, two chunk tables and a track, and the walker
 * does not care that no instrument in it exists.
 *
 * `declared` is the track's own length word, which is the whole point. Set it
 * past the end of the file and the reader has to say so, because nothing else
 * will: the events that are there end cleanly, so neither the truncation flag
 * nor the unknown-opcode flag is set, and the clamp that keeps the walk inside
 * the file is invisible from the outside.
 *
 * Returns the length written. */
static size_t build_song(unsigned char *d, unsigned long declared)
{
    memset(d, 0, 59);
    put32(d, 59UL);  /* outer: total, which nothing reads */
    put16(d + 4, 1); /* one chunk */
    memcpy(d + 6, "SONG", 4);
    put32(d + 10, 0UL); /* at 0 past the outer tables, so offset 14 */

    put32(d + 14, 45UL); /* the song chunk, likewise unread */
    put16(d + 18, 2);    /* hdr1 and one track */
    memcpy(d + 20, "hdr1", 4);
    memcpy(d + 24, "TRK1", 4);
    put32(d + 28, 0UL);  /* hdr1 at 36 */
    put32(d + 32, 17UL); /* TRK1 at 53 */

    d[42] = 1; /* one instrument */
    memcpy(d + 43, "INS1", 4);
    d[47] = 1; /* one track */
    memcpy(d + 48, "TRK1", 4);

    put32(d + 53, declared);
    d[57] = 0x00; /* delta 0 */
    d[58] = 0xD9; /* track_end, which ends the walk cleanly */
    return 59;
}

static void check_kms(void)
{
    unsigned char song[59];
    char         *argv[1];
    size_t        n;

    argv[0] = arg(0, SONG);

    /* Six is exactly what is there: the four byte length word and the two byte
     * event. Nothing is wrong with this one and it has to pass, or the case
     * below would prove only that kms refuses songs. */
    n = build_song(song, 6UL);
    if (write_bytes(SONG, song, n) == 0) {
        expect("a well formed song is read", cmd_kms(1, argv), 0);
    }

    /* A thousand bytes claimed out of a fifty-nine byte file. The walk is
     * clamped to what exists and finds a clean track_end inside it, so the
     * report reads as though nothing were wrong; only the status says
     * otherwise, and it used to say nothing at all. */
    n = build_song(song, 1000UL);
    if (write_bytes(SONG, song, n) == 0) {
        expect("a track declaring more than the file holds is refused",
               cmd_kms(1, argv) != 0, 1);
    }
}

/* Malformed input has to be refused, and refused cleanly: a non-zero return
 * and no half-written output file. Both are checked, because either alone can
 * pass while the other is wrong. */
static void reject(const char *what, const char *spec)
{
    int rc;

    if (spec != NULL && write_text(SPEC, spec) != 0) {
        printf("FAIL  cannot write %s\n", SPEC);
        failures++;
        return;
    }
    remove(OUT);
    rc = mkvce(SPEC, OUT);
    if (rc == 0) {
        printf("FAIL  %s returned success\n", what);
        failures++;
    } else if (file_exists(OUT)) {
        printf("FAIL  %s left an output file behind\n", what);
        failures++;
    } else {
        printf("ok    %s is refused\n", what);
    }
    remove(OUT);
}

static void check_rejects(void)
{
    unsigned char drv[128];

    reject("an unreadable source bank",
           "source SCNOPE.VCE\nBASS BASS 38 = = = = 0\n");
    reject("an instrument missing from the source",
           "source SCTEST.VCE\nBASS NOPE 38 = = = = 0\n");
    reject("a name that is not four characters",
           "source SCTEST.VCE\nBA BASS 38 = = = = 0\n");
    reject("a line with too few columns",
           "source SCTEST.VCE\nBASS BASS 38 = =\n");
    reject("a value out of range",
           "source SCTEST.VCE\nBASS BASS 999 = = = = 0\n");
    reject("an unknown trailing field",
           "source SCTEST.VCE\nBASS BASS 38 = = = = 0 nonsense=1\n");
    reject("an instrument before any source directive",
           "BASS BASS 38 = = = = 0\n");

    /* The cases the second review asked for. Each of these produced a file
     * before: a MIDI status byte where a program belonged, a typo read as a
     * number, two resources the game cannot tell apart, or a whole bank
     * directive applied to only part of the bank. */
    reject("a program above the MIDI data byte range",
           "source SCTEST.VCE\nBASS BASS 255 = = = = 0\n");
    reject("a program that is not a number",
           "source SCTEST.VCE\nBASS BASS garbage = = = = 0\n");
    reject("a program with trailing rubbish",
           "source SCTEST.VCE\nBASS BASS 12x = = = = 0\n");
    reject("a bank above the MIDI data byte range",
           "source SCTEST.VCE\nBASS BASS 38 = = = = 200\n");
    reject("a source name that is not four characters",
           "source SCTEST.VCE\nBASS BAS 38 = = = = 0\n");
    reject("two resources whose names differ only in case",
           "source SCTEST.VCE\nBASS BASS 38 = = = = 0\n"
           "bass BASS 38 = = = = 0\n");
    reject("a source directive after the first instrument",
           "source SCTEST.VCE\nBASS BASS 38 = = = = 0\n"
           "source SCTEST.VCE\n");
    reject("a volumescale after the first instrument",
           "source SCTEST.VCE\nBASS BASS 38 = = = = 0\nvolumescale 0.7\n");
    reject("a size that disagrees with the source bank",
           "source SCTEST.VCE\nsize 94\nBASS BASS 38 = = = = 0\n");
    reject("a volumescale that is not a number",
           "source SCTEST.VCE\nvolumescale .\nBASS BASS 38 = = = = 0\n");
    reject("a volumescale too large to scale anything",
           "source SCTEST.VCE\nvolumescale 999999999\n"
           "BASS BASS 38 = = = = 0\n");
    /* Six decimal places, which the whole-part limit does not catch: the factor
     * is representable, and what overflows is v * num, which reaches 59 times
     * LONG_MAX on the 32-bit long the DOS build has.
     *
     * The numbers are chosen, not illustrative. Most large factors wrap to
     * something still above 127 and clamp to the same answer, so they show
     * nothing. Volume 3 scaled by 715.699937 wraps to a value below 1 and
     * clamps to 1, where the correct answer is 127. This case fails against the
     * arithmetic it replaced and passes against the arithmetic that replaced
     * it, which is the only reason it is worth having. */
    {
        unsigned char b[BANKSIZE];
        unsigned char out[BANKSIZE];

        build_bank(b);
        if (write_bytes(BANK, b, sizeof b) == 0 &&
            write_text(SPEC, "source SCTEST.VCE\nvolumescale 715.699937\n"
                             "BASS BASS 38 3 = = = 0\n") == 0) {
            remove(OUT);
            if (mkvce(SPEC, OUT) == 0 &&
                read_bytes(OUT, out, (long)sizeof out) > 0) {
                /* From the output's own header: it holds one instrument, not
                 * the two the source bank has, so HDR is the wrong base. */
                int base = 6 + 8 * (out[4] + (out[5] << 8));
                expect("a volumescale that overflows a 32-bit long is computed "
                       "without one",
                       out[base + I_VOLUME], 127);
            } else {
                printf("FAIL  the overflow volumescale case was refused\n");
                failures++;
            }
            remove(OUT);
        }
    }

    /* The bank-wide size assertion fires before any record is selected, so it
     * cannot reach the per-record check. This corrupts the second record's own
     * size word and then asks for that record. */
    {
        unsigned char b[BANKSIZE];
        build_bank(b);
        put16(b + HDR + REC + I_SIZE, REC - 1);
        if (write_bytes(BANK, b, sizeof b) == 0) {
            reject("a record whose own size word disagrees with the bank",
                   "source SCTEST.VCE\nHRN1 HRN1 38 = = = = 0\n");
        }
        build_bank(b);
        (void)write_bytes(BANK, b, sizeof b);
    }

    /* A source byte too large to transmit, inherited rather than written. The
     * range check used to run only on the parse, so "=" walked a status byte
     * into the output where a program belonged. */
    {
        unsigned char b[BANKSIZE];
        build_bank(b);
        b[HDR + I_PROGRAM] = 255;
        if (write_bytes(BANK, b, sizeof b) == 0) {
            reject("a source program of 255 inherited with =",
                   "source SCTEST.VCE\nBASS BASS = = = = = 0\n");
        }
        build_bank(b);
        (void)write_bytes(BANK, b, sizeof b);
    }

    /* No spec file at all. NULL leaves the one on disk alone; removing it is
     * the point of the case. */
    remove(SPEC);
    reject("a specification that is not there", NULL);

    /* Argument errors, which produce no output file to judge by. */
    if (cmd_mkvce(0, NULL) == 0) {
        printf("FAIL  mkvce with no arguments returned success\n");
        failures++;
    } else {
        printf("ok    mkvce with no arguments is refused\n");
    }
    if (cmd_cfg(0, NULL) == 0) {
        printf("FAIL  cfg with no arguments returned success\n");
        failures++;
    } else {
        printf("ok    cfg with no arguments is refused\n");
    }
    if (cfg("SCNOPE.DRV", NULL, NULL) == 0) {
        printf("FAIL  cfg on a missing file returned success\n");
        failures++;
    } else {
        printf("ok    cfg on a missing file is refused\n");
    }

    /* cfg must recognise a driver by its signature and refuse anything else.
     * The bank is a real file of a plausible size with no signature in it. */
    if (cfg(BANK, "volume=1", NULL) == 0) {
        printf("FAIL  cfg accepted a file with no signature\n");
        failures++;
    } else if (read_bytes(BANK, drv, (long)sizeof drv) < 0) {
        printf("FAIL  cfg removed a file it should not have touched\n");
        failures++;
    } else if (drv[CFG_VOLUME] == 1) {
        printf("FAIL  cfg patched a file with no signature\n");
        failures++;
    } else {
        printf("ok    cfg refuses a file with no signature\n");
    }
}

int main(void)
{
    unsigned char bank[BANKSIZE];

    /* Shipped as SCCHECK.EXE, so it says which build is doing the checking. */
    fputs(SC_BANNER "\n", stdout);

    build_bank(bank);
    if (write_bytes(BANK, bank, sizeof bank) != 0) {
        fprintf(stderr, "selfcheck: cannot write %s\n", BANK);
        return 2;
    }

    check_mkvce();
    check_cfg();
    check_block();
    check_kms();
    check_rejects();

    remove(BANK);
    remove(DRV);
    remove(SPEC);
    remove(OUT);
    remove(SONG);

    if (failures != 0) {
        printf("\n%d failed\n", failures);
        return 1;
    }
    printf("\nself-check passed\n");
    return 0;
}
