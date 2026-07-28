/* kms - decode a Stunts .KMS song.
 *
 *     sc15 kms SKIDTITL.KMS            summary only
 *     sc15 kms SKIDTITL.KMS -events    full event stream
 *
 * A read only analysis tool. Nothing in the build depends on it, but the
 * instrument mapping was derived from what the songs actually play, and the
 * reason the music sat wrong for so long is visible only here: the songs send
 * their own CC 7 on some tracks and not others, which no amount of reading the
 * voice bank would ever show. See docs/instruments.md and docs/story.md.
 *
 * Container format, from audioresource_find() in the game's seg029:
 *
 *   +00  dword  total size
 *   +04  word   chunk count n
 *   +06         n * 4 bytes of chunk names
 *   +06+4n      n * 4 bytes of chunk offsets, relative to the data area
 *   +06+8n      data area; every chunk starts with its own dword length
 *
 * A .KMS holds one chunk named after the song, which is itself a container.
 * Inside it, hdr1 lists the instruments and the tracks, and every other chunk
 * is a track.
 *
 * hdr1, offsets from the chunk start so including its length dword:
 *
 *   +04  byte   unused
 *   +05  byte   "already bound" flag, set by the game after loading
 *   +06  byte   instrument count i
 *   +07         i * 4 bytes of instrument names, looked up in the .VCE bank
 *   +07+4i      byte track count t
 *   +08+4i      t * 5 bytes, each a 4 byte track chunk name plus one byte
 *
 * Event stream, from sub_3945A and sub_38702 in seg028. Every event is
 *
 *   <delta VLQ> <opcode> [operands] [<duration VLQ>]
 *
 * where a variable length quantity carries seven bits per byte and the high
 * bit means another byte follows.
 *
 *   op <  0D9h   note. The note number is op & 7Fh; when bit 7 is set an
 *                explicit velocity byte follows, otherwise the track default
 *                is used. Notes, and only notes, carry the trailing duration.
 *   op >= 0D9h   command, see the table below.
 *
 * Chunk names are matched case insensitively, because the game always calls
 * audioresource_compare_chunknames() with its case sensitive flag clear. That
 * is why a container holds 'HDR1' while the engine looks for "hdr1".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sc15.h"

#define KMS_CMD_LO 0xD9
#define KMS_CMD_HI 0xEA
#define NCMD (KMS_CMD_HI - KMS_CMD_LO + 1)

#define MAX_INSTR 32
#define MAX_TRACK 32
#define MAX_TEXT 64

/* Operand bytes consumed after the opcode. -1 means a length prefixed
 * payload: one count byte, then that many bytes of text. */
static const struct {
    const char *name;
    int         operands;
} cmd[NCMD] = {{"track_end", 0},      {"loop_restart", 0}, {"rewind", 0},
               {"set_instrument", 1}, {"set_tempo", 1},    {"set_volume", 1},
               {"controller", 2},     {"set_e0", 1},       {"set_e1", 1},
               {"push_repeat", 1},    {"pop_repeat", 0},   {"set_transpose", 1},
               {"pitch_bend", 2},     {"call_track", 5},   {"skip_block", -1},
               {"raw_midi", -1},      {"set_channel", 1},  {"set_ea", 1}};

struct kms {
    const unsigned char *d;
    unsigned long        len;
    unsigned long        p;     /* the walking cursor */
    unsigned long        limit; /* what the cursor may not pass: the track end
                                 * while walking events, the file otherwise */
    int trunc;                  /* an event ran off that limit */
    int unknown;                /* an opcode with no entry in the table */
};

static unsigned int name_at(const unsigned char *d, unsigned long at, char *out)
{
    int i;
    for (i = 0; i < 4; i++) {
        out[i] = (char)d[at + i];
    }
    out[4] = '\0';
    return 4;
}

static int same_name(const char *a, const char *b)
{
    int i;
    for (i = 0; i < 4; i++) {
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

/* Every offset below arrives from the file, so nothing may be dereferenced
 * before it has been shown to lie inside it. The addition is done in unsigned
 * long and checked for wrap first, because a 32-bit field can name an offset
 * that overflows the sum. */
static int fits_to(unsigned long limit, unsigned long at, unsigned long count)
{
    if (at > limit) {
        return 0;
    }
    if (count > limit - at) {
        return 0;
    }
    return 1;
}

static int fits(const struct kms *k, unsigned long at, unsigned long count)
{
    return fits_to(k->len, at, count);
}

/* Locate a chunk by name inside the container at base. Returns its absolute
 * offset, or 0 if there is no such chunk or the container does not fit; 0 is
 * never a valid chunk offset because the header occupies the first bytes. */
static unsigned long find_chunk(const struct kms *k, unsigned long base,
                                const char *want)
{
    unsigned int  n;
    unsigned int  i;
    unsigned long tables;
    char          nm[5];

    if (!fits(k, base, 6)) {
        return 0;
    }
    n = rd16(k->d + base + 4);
    tables = 6 + 8UL * n;
    if (!fits(k, base, tables)) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        name_at(k->d, base + 6 + 4 * i, nm);
        if (same_name(nm, want)) {
            unsigned long rel = rd32(k->d + base + 6 + 4UL * n + 4UL * i);
            unsigned long at = base + tables;
            if (rel > k->len - at) {
                return 0;
            }
            return at + rel;
        }
    }
    return 0;
}

/* Sets k->trunc when the quantity ran off the end, so a caller can stop rather
 * than carry on with a value that was never fully read. */
static unsigned long vlq(struct kms *k)
{
    unsigned long v = 0;
    unsigned char b;
    do {
        if (k->p >= k->limit) {
            k->trunc = 1;
            return v;
        }
        b = k->d[k->p++];
        v = (v << 7) + (b & 0x7F);
    } while (b & 0x80);
    return v;
}

struct tstat {
    unsigned int  notes[128];
    unsigned int  ncmds[NCMD];
    unsigned long total;
    int           instr;
    int           lo, hi;
    char          text[MAX_TEXT];
};

/* Walk one track. With print clear it fills s; with print set it emits the
 * event log and leaves s alone. Splitting it this way keeps the summary above
 * the events in the output without buffering the log. */
static void walk(struct kms *k, unsigned long start, unsigned long end,
                 int print, struct tstat *s)
{
    if (!print) {
        memset(s->notes, 0, sizeof s->notes);
        memset(s->ncmds, 0, sizeof s->ncmds);
        s->total = 0;
        s->instr = -1;
        s->lo = -1;
        s->hi = -1;
        s->text[0] = '\0';
    }
    k->p = start + 4;
    k->limit = end; /* events belong to this track, not to whatever follows */

    while (k->p < end) {
        unsigned long at = k->p;
        unsigned long delta = vlq(k);
        unsigned int  op;

        if (k->p >= end) {
            break;
        }
        op = k->d[k->p++];

        if (op >= KMS_CMD_LO && op <= KMS_CMD_HI) {
            int          slot = (int)op - KMS_CMD_LO;
            int          n = cmd[slot].operands;
            unsigned int a[5];
            char         text[MAX_TEXT];
            int          j, na = 0;

            text[0] = '\0';
            if (n < 0) {
                unsigned int payload;
                int          w = 0;
                if (!fits_to(end, k->p, 1)) {
                    k->trunc = 1;
                    break;
                }
                payload = k->d[k->p];
                if (!fits_to(end, k->p + 1, payload)) {
                    k->trunc = 1;
                    break;
                }
                a[0] = payload;
                na = 1;
                for (j = 0; j < (int)payload && w < MAX_TEXT - 1; j++) {
                    unsigned char c = k->d[k->p + 1 + j];
                    if (c >= 0x20 && c < 0x7F) {
                        text[w++] = (char)c;
                    }
                }
                text[w] = '\0';
                k->p += 1 + payload;
            } else {
                if (!fits_to(end, k->p, (unsigned long)n)) {
                    k->trunc = 1;
                    break;
                }
                for (j = 0; j < n; j++) {
                    a[j] = k->d[k->p + j];
                }
                na = n;
                k->p += (unsigned long)n;
            }

            if (print) {
                printf("      %06lX  +%-5lu %-14s", at, delta, cmd[slot].name);
                for (j = 0; j < na; j++) {
                    printf(" %02X", a[j]);
                }
                if (text[0] != '\0') {
                    printf("  \"%s\"", text);
                }
                printf("\n");
            } else {
                s->ncmds[slot]++;
                if (op == 0xDC && na > 0) {
                    s->instr = (int)a[0];
                }
                if (text[0] != '\0') {
                    strcpy(s->text, text);
                }
            }
            if (op == KMS_CMD_LO) { /* track_end */
                break;
            }
        } else if (op > KMS_CMD_HI) {
            /* The format puts every opcode from KMS_CMD_LO up in the command
             * range, so this is a command with no entry in the table and an
             * operand count nothing here knows. Carrying on would decode the
             * operands as events. */
            if (print) {
                printf("      %06lX  +%-5lu unknown opcode %02X\n", at, delta,
                       op);
            }
            k->unknown = 1;
            break;
        } else {
            unsigned int  note = op & 0x7F;
            int           vel = -1;
            unsigned long dur;

            /* Bit 7, as the format describes it. Testing op > 80h instead made
             * opcode 80h, note 0 with an explicit velocity, the one note that
             * read its velocity as the start of the duration. */
            if (op & 0x80) {
                if (!fits_to(end, k->p, 1)) {
                    k->trunc = 1;
                    break;
                }
                vel = (int)k->d[k->p++];
            }
            dur = vlq(k);
            if (k->trunc) {
                break;
            }

            if (print) {
                printf("      %06lX  +%-5lu note %-3u vel ", at, delta, note);
                if (vel < 0) {
                    printf("%-4s", "-");
                } else {
                    printf("%-4d", vel);
                }
                printf(" dur %lu\n", dur);
            } else {
                s->notes[note]++;
                s->total++;
                if (s->lo < 0 || (int)note < s->lo) {
                    s->lo = (int)note;
                }
                if ((int)note > s->hi) {
                    s->hi = (int)note;
                }
            }
        }
    }
}

/* Alphabetically, matching the order the original Perl tool printed, so the
 * examples in the documentation stay accurate. */
static void print_commands(const struct tstat *s)
{
    int used[NCMD], n = 0, i, j;

    for (i = 0; i < NCMD; i++) {
        if (s->ncmds[i]) {
            used[n++] = i;
        }
    }
    if (n == 0) {
        return;
    }
    for (i = 1; i < n; i++) {
        int key = used[i];
        for (j = i - 1; j >= 0 && strcmp(cmd[used[j]].name, cmd[key].name) > 0;
             j--) {
            used[j + 1] = used[j];
        }
        used[j + 1] = key;
    }
    printf("      commands:");
    for (i = 0; i < n; i++) {
        printf(" %s x%u", cmd[used[i]].name, s->ncmds[used[i]]);
    }
    printf("\n");
}
int cmd_kms(int argc, char **argv)
{
    struct blob   f;
    struct kms    k;
    unsigned long outer, hdr, tp;
    unsigned int  ninstr, ntrack, i, t;
    char          instr[MAX_INSTR][5];
    char          track[MAX_TRACK][5];
    int           events = 0;
    /* Any track that could not be read whole. The per-track faults below are
     * printed and walked past, because a song with one bad track still has
     * useful things to say about the rest, but the exit status has to say that
     * something was wrong: a partial report leaving 0 behind reads to a script
     * exactly like a clean one. */
    int  bad = 0;
    char nm[5];

    if (argc < 1) {
        return fail("usage: sc15 kms <song.kms> [-events]");
    }
    for (i = 1; i < (unsigned int)argc; i++) {
        if (same_word(argv[i], "-events")) {
            events = 1;
        } else {
            return fail("unknown option %s", argv[i]);
        }
    }
    if (blob_load(&f, argv[0]) != 0) {
        return 1;
    }
    k.d = f.data;
    k.len = f.size;
    k.p = 0;
    k.limit = f.size;
    k.unknown = 0;
    k.trunc = 0;

    if (f.size < 16) {
        blob_free(&f);
        return fail("%s is too short to be a song", argv[0]);
    }

    /* The outer container holds exactly one chunk, named after the song. The
     * game passes that name in from the caller of init_audio_resources(). */
    if (rd16(k.d + 4) < 1) {
        blob_free(&f);
        return fail("%s: empty container", argv[0]);
    }
    name_at(k.d, 6UL, nm);
    outer = find_chunk(&k, 0UL, nm);
    if (outer == 0) {
        blob_free(&f);
        return fail("%s: the outer chunk does not fit the file", argv[0]);
    }
    hdr = find_chunk(&k, outer, "hdr1");
    if (hdr == 0) {
        blob_free(&f);
        return fail("%s: no hdr1 chunk", argv[0]);
    }

    /* Clamping the counts below bounds the loops but not the reads that reach
     * the tables, so the tables themselves have to be shown to fit first. */
    if (!fits(&k, hdr, 7)) {
        blob_free(&f);
        return fail("%s: hdr1 is truncated", argv[0]);
    }
    ninstr = k.d[hdr + 6];
    if (!fits(&k, hdr + 7, 4UL * ninstr + 1)) {
        blob_free(&f);
        return fail("%s: the instrument table does not fit the file", argv[0]);
    }
    tp = hdr + 7 + 4 * (unsigned long)ninstr;
    ntrack = k.d[tp++];
    if (!fits(&k, tp, 5UL * ntrack)) {
        blob_free(&f);
        return fail("%s: the track table does not fit the file", argv[0]);
    }
    if (ninstr > MAX_INSTR) {
        ninstr = MAX_INSTR;
    }
    for (i = 0; i < ninstr; i++) {
        name_at(k.d, hdr + 7 + 4 * i, instr[i]);
    }
    if (ntrack > MAX_TRACK) {
        ntrack = MAX_TRACK;
    }
    for (t = 0; t < ntrack; t++) {
        name_at(k.d, tp + 5 * t, track[t]);
    }

    printf("%s\n", argv[0]);
    printf("  instruments (%u):", ninstr);
    for (i = 0; i < ninstr; i++) {
        printf(" %s", instr[i]);
    }
    printf("\n  tracks      (%u):", ntrack);
    for (t = 0; t < ntrack; t++) {
        printf(" %s", track[t]);
    }
    printf("\n  MIDI channel for track i defaults to ((i & 0Fh) + 1) & 0Fh"
           " unless the\n  instrument pins one via its +43h field.\n\n");

    for (t = 0; t < ntrack; t++) {
        struct tstat  s;
        unsigned long start, end;

        start = find_chunk(&k, outer, track[t]);
        if (start == 0) {
            printf("  track %2u '%s': chunk missing\n", t, track[t]);
            bad = 1;
            continue;
        }
        if (!fits(&k, start, 4)) {
            printf("  track %2u '%s': chunk header is truncated\n", t,
                   track[t]);
            bad = 1;
            continue;
        }
        /* Subtraction, so a length that would carry the end past the file
         * cannot wrap the addition instead. */
        {
            unsigned long declared = rd32(k.d + start);
            /* A track is its own four byte length followed by its events, so a
             * length below four does not describe a track at all. Walked, it
             * read as an empty one and said nothing: the cursor starts at
             * start + 4, which such an end is already behind, so no event is
             * read and no truncation is recorded. */
            if (declared < 4) {
                printf("  track %2u '%s': declared length %lu is shorter than "
                       "its own header\n",
                       t, track[t], declared);
                bad = 1;
                continue;
            }
            /* A length that runs past the file is clamped so the walk stays
             * inside it, and reported, because the clamp is the only reason
             * nothing else notices. A track can declare more bytes than exist,
             * put a clean track_end in the bytes that do, and set neither trunc
             * nor unknown: the header is the defect and the events never see
             * it. */
            if (declared > k.len - start) {
                printf("  track %2u '%s': declared length %lu runs past the "
                       "end of the file\n",
                       t, track[t], declared);
                bad = 1;
                end = k.len;
            } else {
                end = start + declared;
            }
        }

        k.trunc = 0;
        k.unknown = 0;
        walk(&k, start, end, 0, &s);
        if (k.trunc) {
            printf("  track %2u '%s': an event runs past the end of the track "
                   "at %06lX\n",
                   t, track[t], k.p);
            bad = 1;
        }
        if (k.unknown) {
            printf("  track %2u '%s': an opcode with no operand count at "
                   "%06lX\n",
                   t, track[t], k.p);
            bad = 1;
        }

        printf("  track %2u '%s'  default MIDI ch %u  instrument slot ", t,
               track[t], ((t & 0x0F) + 1) & 0x0F);
        if (s.instr >= 0 && s.instr < (int)ninstr) {
            printf("%d (%s)", s.instr, instr[s.instr]);
        } else if (s.instr >= 0) {
            printf("%d (?)", s.instr);
        } else {
            printf("none");
        }
        if (s.text[0] != '\0') {
            printf("  \"%s\"", s.text);
        }
        printf("\n      %lu notes, range ", s.total);
        if (s.lo < 0) {
            printf("-..-\n");
        } else {
            printf("%d..%d\n", s.lo, s.hi);
        }
        if (s.lo >= 0) {
            printf("      note histogram:");
            for (i = 0; i < 128; i++) {
                if (s.notes[i]) {
                    printf(" %u x%u", i, s.notes[i]);
                }
            }
            printf("\n");
        }
        print_commands(&s);

        /* The event log follows the summary, so the track is walked twice
         * rather than buffered. A song is a few kilobytes and this is an
         * analysis tool; the second pass costs nothing worth an allocation. */
        if (events) {
            walk(&k, start, end, 1, &s);
        }
        printf("\n");
    }

    blob_free(&f);
    /* The report above is still printed in full. Only the status changes, and
     * only when a track could not be read as one. */
    return bad ? 1 : 0;
}
