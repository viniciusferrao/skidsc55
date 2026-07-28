/* sc15 - build, configure and inspect the Roland Sound Canvas driver for
 * Stunts 1.1.
 *
 * Four jobs, one binary:
 *
 *   mkvce   generate a .VCE voice bank from a text specification, copying the
 *           instrument records out of the game's own MT-32 bank so that the
 *           envelopes, LFO settings and note transposes the composer wrote
 *           survive untouched, and overwriting only the fields the driver
 *           reads.
 *   cfg     read or change the configuration block inside a built SC15.DRV.
 *   kms     decode a song into its instruments, tracks and events. Read only,
 *           and where the instrument mapping was derived from.
 *   block   show the skidset driver block inside a built driver, and check
 *           it against every limit skidset enforces.
 *
 * Strict C89, no dependencies. It compiles as cleanly on a 1990 16-bit DOS
 * compiler as on a modern one, so the whole workflow runs on a machine of the
 * era.
 *
 * Unlike skidpack, nothing here needs huge pointers. The largest file this
 * tool touches is a voice bank at about 1.2 KB and the driver at 1.7 KB, so
 * every buffer fits inside a 16-bit segment with room to spare. File offsets
 * are still unsigned long, because size_t is 16 bits on those hosts and a
 * container header carries a 32-bit size field.
 */
#ifndef SC15_H
#define SC15_H

#include <stddef.h>

/* A loaded file. Small enough that near pointers are always sufficient. */
struct blob {
    unsigned char *data;
    unsigned long  size;
};

/* --- util.c ------------------------------------------------------------- */

/* Lets a compiler that understands it check the arguments against the format
 * string. Anything else, including a 1988 one, ignores the macro entirely. */
#if defined(__GNUC__)
#    define SC15_PRINTF(fmtarg, firstvar) \
        __attribute__((format(printf, fmtarg, firstvar)))
#else
#    define SC15_PRINTF(fmtarg, firstvar)
#endif

/* Print "sc15: " and the message to stderr, then return 1 so callers can
 * write `return fail("...")` at the point the error is discovered. */
int fail(const char *fmt, ...) SC15_PRINTF(1, 2);

int  blob_load(struct blob *b, const char *path);
int  blob_save(const struct blob *b, const char *path);
void blob_free(struct blob *b);

/* Little-endian accessors. The container format and the driver image are both
 * little-endian regardless of what the host is. */
unsigned int  rd16(const unsigned char *p);
unsigned long rd32(const unsigned char *p);
void          wr16(unsigned char *p, unsigned int v);
void          wr32(unsigned char *p, unsigned long v);

/* Compare two words ignoring case, non-zero when they match. Every keyword read
 * from the command line goes through it, since upper case is what a DOS user
 * types. C90 has no strcasecmp; the one in <strings.h> is POSIX. */
int same_word(const char *a, const char *b);

/* Longest path the tool will write through. DOS allows 80, a modern host far
 * more; this only has to be enough for the sibling temporary blob_save uses. */
#define SC15_PATH_MAX 256

/* Parse a complete decimal token and require it to land inside [lo, hi].
 * Non-zero when it did, and *out is only written then. Rejects an empty token,
 * trailing rubbish and overflow, none of which atoi() can report. */
int parse_num(const char *s, long lo, long hi, long *out);

/* The largest value any MIDI data byte may carry. Anything above it would be
 * read as a status byte by the receiver. */
#define MIDI_MAX 127

/* --- mkvce.c ------------------------------------------------------------ */

int cmd_mkvce(int argc, char **argv);

/* --- cfg.c -------------------------------------------------------------- */

int cmd_cfg(int argc, char **argv);

/* --- block.c ------------------------------------------------------------ */

int cmd_block(int argc, char **argv);

/* --- kms.c --------------------------------------------------------------- */

int cmd_kms(int argc, char **argv);

/* --- The .VCE container --------------------------------------------------
 *
 * Read by audioresource_find() in the game's seg029:
 *
 *   +00  dword  total size
 *   +04  word   chunk count n
 *   +06         n * 4 bytes of chunk names, four ASCII characters each
 *   +06+4n      n * 4 bytes of chunk offsets, relative to the data area
 *   +06+8n      data area
 *
 * Chunk names are matched case insensitively: the game always calls
 * audioresource_compare_chunknames() with its case sensitive flag clear, which
 * is why the containers hold 'HDR1' while the engine looks for "hdr1".
 */
#define VCE_COUNT_OFS 4
#define VCE_NAMES_OFS 6
#define VCE_NAME_LEN 4

/* Offsets inside one instrument record. Only the fields the driver itself
 * reads are named; the rest belong to the music engine and are copied
 * verbatim. See docs/driver.md section 4.1. */
#define I_SIZE 0x00      /* word, record length, 93 for the MT-32 banks */
#define I_PITCHDIV 0x0E  /* divisor applied to the raw engine rev counter */
#define I_PITCHOFS 0x0F  /* offset added to that value, times 16 */
#define I_TRANSPOSE 0x10 /* signed note transpose, added by the engine */
#define I_BENDRANGE 0x12 /* pitch bend sensitivity, sent as RPN 0 */
#define I_VELSENS 0x15   /* 0 plays every note at velocity 127 */
#define I_PORTA 0x35     /* 1 retriggers the note when the pitch offset moves */
#define I_CHANNEL 0x43   /* MIDI channel, below 10h pins the part to it */
#define I_PROGRAM 0x44   /* program number */
#define I_VOLUME 0x45    /* channel volume for CC 7, 0 means do not send it */
#define I_PAN 0x46       /* channel pan for CC 10 */
#define I_BANK 0x47      /* SC15 extension: bank select MSB, the GS variation */
#define I_RETRIG 0x48    /* SC15 extension: restrike period in engine ticks */

/* Offsets inside one voice record, the 2Eh byte structure at 0A2B6h in the
 * game data segment. Only the fields the driver writes are named. Used by the
 * development harness in test/, not by the bank builder. */
#define V_NOTEBASE_OFS 0x03 /* note as written in the song */
#define V_VELOCITY_OFS 0x04 /* velocity used for the note on */
#define V_CURNOTE_OFS 0x06  /* note currently sounding */

/* --- The driver configuration block --------------------------------------
 *
 * Sits at a fixed offset behind an ASCII signature so an installed SC15.DRV
 * can be retargeted without rebuilding it. See docs/driver.md section 7.
 */
#define CFG_OFS 0x45
#define CFG_SIG "SC15CFG"
/* Eight, not seven: the driver writes the terminator too, and comparing only
 * the letters would accept an image whose eighth byte is something else. */
#define CFG_SIG_LEN 8
#define CFG_PORT 0x4D   /* word, MPU-401 data port, status is this plus 1 */
#define CFG_GM 0x4F     /* send GM System On at init */
#define CFG_GS 0x50     /* send GS Reset at init, and enable bank select */
#define CFG_VOLUME 0x51 /* initial master volume, 0 to 100 */

#endif
