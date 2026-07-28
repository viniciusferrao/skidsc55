/* drvtest - run the built SC15.DRV and check the MIDI it emits.
 *
 * Development tooling. Not needed to build the driver, not needed to play the
 * game, and not installed anywhere.
 *
 * It loads the shipped binary at offset 0 of a segment, exactly as the game's
 * mmgr_alloc_pages() does, far-calls the jump table the way the music engine
 * does, models an MPU-401 UART at 330h and 331h, and checks the bytes that
 * come out. Because it runs the real image rather than a re-implementation, it
 * catches assembler and linker faults as well as logic faults.
 *
 * Two of the checks cannot be made by listening. Peak stack depth matters
 * because every slot is called from inside the game's timer interrupt, on
 * whatever stack was current. Protocol validity, that no data byte is ever
 * emitted without a status byte and no status byte appears inside a system
 * exclusive, is the class of fault that produces stuck notes and noises which
 * are indistinguishable by ear from a wrong mapping.
 *
 * Strict C89, no dependencies.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu86.h"
#include "../src/sc15.h"

#define DRV_SEG 0x1000
#define DATA_SEG 0x2000
#define STACK_SEG 0x3000
#define VOICE_OFS 0x0100
#define CHUNK_OFS 0x0200
#define SCRATCH_OFS 0x0400
#define TMP_INSTR 0x0600
#define VCE_OFS 0x1000

#define MPU_DATA 0x330
#define MPU_STAT 0x331

#define MAX_MIDI 512
#define MAX_PEND 16

struct rig {
    unsigned char midi[MAX_MIDI]; /* bytes written to the data port */
    int           midi_n;
    unsigned int  cmd[8]; /* bytes written to the command port */
    int           cmd_n;
    unsigned char pend[MAX_PEND]; /* bytes the interface will hand back */
    int           pend_head, pend_n;
    int           ack_ready;
    long          tx_stall; /* polls for which the transmitter reports busy.
                             * long so a stall longer than MPU_TIMEOUT can be
                             * expressed on a 16-bit host too. */
    int no_hardware;
};

static struct rig   rig;
static int          passed, failed;
static unsigned int sp_worst, sp_worst_slot;
/* The first slot that returned without restoring a register the ABI says the
 * engine owns, or -1 if none did. The direction flag is not among them: emu86
 * does not model it, so it is not claimed here either. */
static int  abi_bad_slot = -1;
static char protocol_error[160];

/* --- the modelled MPU-401 ------------------------------------------------ */

static unsigned char port_in(struct emu *e, unsigned int port)
{
    (void)e;
    if (port == 0x61) {
        return 0; /* the refresh bit, which the driver uses for delays */
    }
    if (rig.no_hardware) {
        return 0xFF; /* an empty slot floats the bus high */
    }
    if (port == MPU_STAT) {
        unsigned char s = 0x00;
        if (rig.tx_stall > 0) {
            rig.tx_stall--;
            s = (unsigned char)(s | 0x40); /* DRR set: cannot accept yet */
        }
        if (!rig.ack_ready && rig.pend_n == 0) {
            s = (unsigned char)(s | 0x80); /* DSR set: nothing to read */
        }
        return s;
    }
    if (port == MPU_DATA) {
        if (rig.ack_ready) {
            rig.ack_ready = 0;
            return 0xFE;
        }
        if (rig.pend_n > 0) {
            unsigned char v = rig.pend[rig.pend_head];
            rig.pend_head = (rig.pend_head + 1) % MAX_PEND;
            rig.pend_n--;
            return v;
        }
        return 0;
    }
    return 0xFF;
}

static void port_out(struct emu *e, unsigned int port, unsigned char v)
{
    (void)e;
    if (port == MPU_DATA) {
        if (rig.midi_n < MAX_MIDI) {
            rig.midi[rig.midi_n++] = v;
        }
        return;
    }
    if (port == MPU_STAT) {
        if (rig.cmd_n < 8) {
            rig.cmd[rig.cmd_n++] = v;
        }
        rig.ack_ready = 1;
    }
}

/* --- checking ------------------------------------------------------------ */

static void check(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want) == 0) {
        passed++;
        printf("  ok    %s\n", what);
    } else {
        failed++;
        printf("  FAIL  %s\n        got  %s\n        want %s\n", what, got,
               want);
    }
}

static void check_int(const char *what, long got, long want)
{
    char g[32], w[32];
    sprintf(g, "%ld", got);
    sprintf(w, "%ld", want);
    check(what, g, w);
}

/* Render the captured bytes as hex, so an expectation reads like a MIDI
 * stream rather than an array comparison. */
static const char *midi_hex(void)
{
    static char buf[MAX_MIDI * 3 + 16];
    int         i;
    char       *p = buf;
    if (rig.midi_n == 0) {
        strcpy(buf, "(nothing)");
        return buf;
    }
    for (i = 0; i < rig.midi_n; i++) {
        sprintf(p, i ? " %02X" : "%02X", rig.midi[i]);
        p += strlen(p);
    }
    return buf;
}

/* Every byte must be a legal MIDI byte in a legal position. */
static void check_protocol(const char *what)
{
    int i, status = -1, in_sysex = 0;
    if (protocol_error[0] != '\0') {
        return;
    }
    for (i = 0; i < rig.midi_n; i++) {
        unsigned char b = rig.midi[i];
        if (b >= 0x80) {
            if (b == 0xF7) {
                in_sysex = 0;
            } else if (b == 0xF0) {
                in_sysex = 1;
            } else if (b < 0xF8) {
                if (in_sysex) {
                    sprintf(protocol_error, "%s: %02X inside a SysEx", what, b);
                    return;
                }
                status = b;
            }
        } else if (status < 0 && !in_sysex) {
            sprintf(protocol_error, "%s: data byte %02X with no status", what,
                    b);
            return;
        }
    }
}

/* --- driving the driver -------------------------------------------------- */

static struct emu emu;

/* Call a jump table slot with cdecl arguments, pushed right to left. */
static unsigned int call_slot(unsigned int slot, const unsigned int *args,
                              int nargs)
{
    int          i;
    unsigned int depth;
    /* Distinctive values, so a slot that corrupts one of the registers it must
     * preserve is caught by what it left behind rather than by luck. The ABI
     * gives the driver AX, BX, CX, DX and ES; everything else belongs to the
     * engine and is checked on return. */
    unsigned int si0 = 0x5151, di0 = 0xD1D1, bp0 = 0xB9B9;

    rig.midi_n = 0;
    rig.cmd_n = 0;
    emu.ax = emu.bx = emu.cx = emu.dx = 0;
    emu.si = si0;
    emu.di = di0;
    emu.bp = bp0;
    emu.sp = 0xFF00;
    emu.cs = DRV_SEG;
    emu.ds = DATA_SEG;
    emu.es = 0;
    emu.ss = STACK_SEG;
    emu.ip = slot;
    emu.cf = emu.zf = emu.sf = emu.of = 0;
    emu.intf = 1;
    emu.steps = 0;
    emu.error = NULL;

    for (i = nargs - 1; i >= 0; i--) {
        emu_push(&emu, args[i]);
    }
    emu_push(&emu, EMU_DONE_SEG);
    emu_push(&emu, EMU_DONE_OFS);

    if (emu_run(&emu) != 0) {
        const char *why = emu.error;
        if (why == NULL) {
            why = "stopped unexpectedly";
        }
        failed++;
        printf("  FAIL  slot %02Xh: %s\n", slot, why);
        return 0;
    }
    depth = emu.sp_start - emu.sp_low;
    if (depth > sp_worst) {
        sp_worst = depth;
        sp_worst_slot = slot;
    }
    /* A driver can emit exactly the right MIDI and still corrupt the engine.
     * Recorded here and asserted once at the end rather than adding a check per
     * call, which would bury the interesting ones. */
    if (emu.ds != DATA_SEG || emu.ss != STACK_SEG || emu.si != si0 ||
        emu.di != di0 || emu.bp != bp0) {
        if (abi_bad_slot < 0) {
            abi_bad_slot = (int)slot;
        }
    }
    return emu.ax;
}

/* Locate an instrument record inside the loaded voice bank, as an offset
 * within the data segment. */
static unsigned int instr_ofs(const unsigned char *vce, const char *name)
{
    unsigned int n = (unsigned int)vce[4] | ((unsigned int)vce[5] << 8);
    unsigned int i;
    for (i = 0; i < n; i++) {
        const unsigned char *have = vce + VCE_NAMES_OFS + 4U * i;
        if (have[0] == (unsigned char)name[0] &&
            have[1] == (unsigned char)name[1] &&
            have[2] == (unsigned char)name[2] &&
            have[3] == (unsigned char)name[3]) {
            const unsigned char *o = vce + VCE_NAMES_OFS + 4U * n + 4U * i;
            unsigned long        rel =
                (unsigned long)o[0] | ((unsigned long)o[1] << 8) |
                ((unsigned long)o[2] << 16) | ((unsigned long)o[3] << 24);
            return (unsigned int)(VCE_OFS + VCE_NAMES_OFS + 8U * n + rel);
        }
    }
    fprintf(stderr, "drvtest: %s is not in the voice bank\n", name);
    exit(2);
    return 0; /* not reached; some compilers want it anyway */
}

/* Build the program change bytes the driver should emit for a record, read
 * out of the bank rather than hardcoded, so retuning a mapping cannot break a
 * driver test. */
static void expect_program(char *out, unsigned int ch, const unsigned char *r,
                           int gs)
{
    char        *p = out;
    unsigned int st = 0xB0 | (ch & 0x0F);
    if (gs) {
        /* Including bank 0. Anything else leaves the channel on whatever
         * variation the last effect on it selected. */
        sprintf(p, "%02X 00 %02X %02X 20 00 ", st, r[I_BANK], st);
        p += strlen(p);
    }
    sprintf(p, "%02X %02X", 0xC0 | (ch & 0x0F), r[I_PROGRAM]);
    p += strlen(p);
    sprintf(p, " %02X 65 00 %02X 64 00", st, st);
    p += strlen(p);
    sprintf(p, " %02X 06 %02X", st, r[I_BENDRANGE]);
    p += strlen(p);
    sprintf(p, " %02X 65 7F %02X 64 7F", st, st);
    p += strlen(p);
    if (r[I_VOLUME] != 0) {
        sprintf(p, " %02X 07 %02X", st, r[I_VOLUME]);
        p += strlen(p);
    }
    sprintf(p, " %02X 0A %02X", st, r[I_PAN]);
}

static unsigned char *slurp(const char *path, unsigned long *len)
{
    FILE          *f = fopen(path, "rb");
    unsigned char *b;
    long           n;
    if (f == NULL) {
        fprintf(stderr, "drvtest: cannot open %s\n", path);
        exit(2);
    }
    fseek(f, 0L, SEEK_END);
    n = ftell(f);
    rewind(f);
    b = (unsigned char *)malloc((size_t)n);
    if (b == NULL || fread(b, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "drvtest: cannot read %s\n", path);
        exit(2);
    }
    fclose(f);
    *len = (unsigned long)n;
    return b;
}

int main(int argc, char **argv)
{
    unsigned char *drv, *vce;
    unsigned long  drvlen, vcelen;
    /* Zeroed rather than left to the stack. Slot 00h takes no arguments, so
     * call_slot pushes nothing and never reads this, but cppcheck reports the
     * call as passing an uninitialized array and CI runs it with
     * --error-exitcode=1. Defining the value is cheaper than arguing, and a
     * harness whose inputs are all deliberate is the better shape anyway. */
    unsigned int  args[8] = {0};
    unsigned int  bass, drum, rt;
    unsigned char rec[128];
    char          want[256], got[256];
    unsigned int  i;

    if (argc != 3) {
        fputs("usage: drvtest <SC15.DRV> <SCSKIDMS.VCE>\n", stderr);
        return 2;
    }
    drv = slurp(argv[1], &drvlen);
    vce = slurp(argv[2], &vcelen);

    emu_init(&emu);
    emu.in = port_in;
    emu.out = port_out;
    emu_load(&emu, DRV_SEG, 0, drv, drvlen);
    emu_load(&emu, DATA_SEG, VCE_OFS, vce, vcelen);

    printf("SC15.DRV test harness\n  driver %s (%lu bytes), voices %s\n\n",
           argv[1], drvlen, argv[2]);

    bass = instr_ofs(vce, "BASS");
    drum = instr_ofs(vce, "DRUM");

    /* --- slot 00h, init -------------------------------------------------- */
    {
        unsigned int ax = call_slot(0x00, args, 0);
        char         cmds[64];
        int          silenced = 0;
        const char  *h;

        check_protocol("init");
        printf("init returned AX=%04X, %d bytes emitted\n", ax, rig.midi_n);
        check("init reports a MIDI class driver",
              ((ax & 0xFF) > 0x7F && (ax & 0xFF) != 0xFF) ? "yes" : "no",
              "yes");
        sprintf(cmds, "%u %u", rig.cmd_n >= 1 ? rig.cmd[0] : 0,
                rig.cmd_n >= 2 ? rig.cmd[1] : 0);
        check("init resets the MPU-401 and selects UART mode", cmds, "255 63");

        h = midi_hex();
        for (i = 0; i < 16; i++) {
            char pat[32];
            sprintf(pat, "B%X 7B 00 B%X 79 00", i, i);
            if (strstr(h, pat) != NULL) {
                silenced++;
            }
        }
        check_int("init silences all 16 channels", (long)silenced, 16L);
        check("init sends GM System On",
              strstr(h, "F0 7E 7F 09 01 F7") ? "yes" : "no", "yes");
        if (drv[CFG_GS] != 0) {
            const char *gm = strstr(h, "F0 7E 7F 09 01 F7");
            const char *gs = strstr(h, "F0 41 10 42 12 40 00 7F 00 41 F7");
            check("with GS on, init sends GS Reset", gs ? "yes" : "no", "yes");
            check("GS Reset comes after GM System On, so GS wins",
                  (gm != NULL && gs != NULL && gm < gs) ? "yes" : "no", "yes");
        } else {
            check("with GS off, init sends no Roland exclusive",
                  strstr(h, "F0 41") ? "found" : "none", "none");
        }
        check("init sets master volume to full",
              strstr(h, "F0 7F 7F 04 01 7F 7F F7") ? "yes" : "no", "yes");
    }

    /* --- slot 21h, program change ---------------------------------------- */
    for (i = 0; i < 2; i++) {
        unsigned int ofs = i ? drum : bass;
        unsigned int ch = i ? 9 : 2;
        unsigned int j;
        for (j = 0; j < 128; j++) {
            rec[j] = emu_rd8(&emu, emu_lin(DATA_SEG, ofs + j));
        }
        args[0] = ch;
        args[1] = 0;
        args[2] = CHUNK_OFS;
        args[3] = ofs;
        args[4] = DATA_SEG;
        call_slot(0x21, args, 5);
        check_protocol("program change");
        expect_program(want, ch, rec, drv[CFG_GS] != 0);
        check(i ? "program change for DRUM selects a kit on channel 10"
                : "program change for BASS on channel 2",
              midi_hex(), want);
        if (drv[CFG_GS] != 0) {
            char pat[32];
            sprintf(pat, "B%X 00 %02X", ch & 0x0F, rec[I_BANK]);
            /* The bank is checked to be 0 as well, so that repointing the
             * instrument at a variation cannot quietly retire the guard. */
            check(i ? "DRUM selects a bank even though it is bank 0"
                    : "BASS selects a bank even though it is bank 0",
                  (rec[I_BANK] == 0 && strstr(midi_hex(), pat) != NULL) ? "yes"
                                                                        : "no",
                  "yes");
        }
    }
    check("DRUM is pinned to the percussion channel and panned centre",
          (emu_rd8(&emu, emu_lin(DATA_SEG, drum + I_CHANNEL)) == 9 &&
           emu_rd8(&emu, emu_lin(DATA_SEG, drum + I_PAN)) == 64)
              ? "yes"
              : "no",
          "yes");

    /* --- slot 09h and 0Ch, note on and note off -------------------------- */
    args[0] = 2;
    args[1] = VOICE_OFS;
    args[2] = CHUNK_OFS;
    args[3] = 37;
    args[4] = 93;
    args[5] = bass;
    args[6] = DATA_SEG;
    call_slot(0x09, args, 7);
    check_protocol("note on");
    check("note on forces velocity 127 for a non velocity sensitive instrument",
          midi_hex(), "92 25 7F");
    sprintf(got, "%02X %02X",
            emu_rd8(&emu, emu_lin(DATA_SEG, VOICE_OFS + V_NOTEBASE_OFS)),
            emu_rd8(&emu, emu_lin(DATA_SEG, VOICE_OFS + V_CURNOTE_OFS)));
    check("note on records the sounding note in the voice record", got,
          "25 25");

    args[0] = 2;
    args[1] = VOICE_OFS;
    call_slot(0x0C, args, 2);
    check_protocol("note off");
    check("note off releases the note the voice recorded", midi_hex(),
          "82 25 00");

    args[0] = 9;
    args[1] = VOICE_OFS;
    args[2] = CHUNK_OFS;
    args[3] = 36;
    args[4] = 93;
    args[5] = drum;
    args[6] = DATA_SEG;
    call_slot(0x09, args, 7);
    check_protocol("drum note");
    check("a drum note plus the bank transpose lands on GM Bass Drum 1",
          midi_hex(), "99 24 7F");

    /* --- controllers ------------------------------------------------------ */
    args[0] = 2;
    args[1] = 0;
    args[2] = 100;
    call_slot(0x12, args, 3);
    check_protocol("volume");
    check("channel volume", midi_hex(), "B2 07 64");

    args[0] = 2;
    args[1] = 0;
    args[2] = 0x5B;
    args[3] = 40;
    call_slot(0x15, args, 4);
    check_protocol("controller");
    check("arbitrary controller", midi_hex(), "B2 5B 28");

    args[0] = 0;
    args[1] = 0x1000;
    args[2] = 2;
    call_slot(0x1B, args, 3);
    check_protocol("bend");
    check("pitch bend of +1000h", midi_hex(), "E2 00 60");

    args[0] = 0;
    args[1] = 0x0000;
    args[2] = 2;
    call_slot(0x1B, args, 3);
    check_protocol("bend centre");
    check("pitch bend centre", midi_hex(), "E2 00 40");

    args[0] = 5;
    call_slot(0x1E, args, 1);
    check_protocol("channel off");
    check("single channel all notes off", midi_hex(), "B5 7B 00");

    args[0] = 1;
    args[1] = VOICE_OFS;
    args[2] = 100;
    call_slot(0x24, args, 3);
    check_protocol("pitch set");
    sprintf(want, "E1 %02X %02X", (100 * 60) & 0x7F, ((100 * 60) >> 7) & 0x7F);
    check("pitch set scales its argument by 60", midi_hex(), want);

    /* --- slot 3Fh, the MT-32 master volume write -------------------------- */
    emu_wr8(&emu, emu_lin(DATA_SEG, SCRATCH_OFS + 0), 0x10);
    emu_wr8(&emu, emu_lin(DATA_SEG, SCRATCH_OFS + 1), 0x00);
    emu_wr8(&emu, emu_lin(DATA_SEG, SCRATCH_OFS + 2), 0x16);
    emu_wr8(&emu, emu_lin(DATA_SEG, SCRATCH_OFS + 3), 50);
    args[0] = 4;
    args[1] = SCRATCH_OFS;
    args[2] = DATA_SEG;
    call_slot(0x3F, args, 3);
    check_protocol("master volume");
    check("MT-32 master volume 50 becomes a GM master volume message",
          midi_hex(), "F0 7F 7F 04 01 3F 3F F7");

    emu_wr8(&emu, emu_lin(DATA_SEG, SCRATCH_OFS + 0), 0x03);
    call_slot(0x3F, args, 3);
    check("any other MT-32 address is dropped", midi_hex(), "(nothing)");

    args[0] = SCRATCH_OFS;
    args[1] = DATA_SEG;
    call_slot(0x42, args, 2);
    check("MT32.PLB upload emits nothing", midi_hex(), "(nothing)");

    /* --- shutdown --------------------------------------------------------- */
    {
        int         silenced = 0;
        const char *h;
        call_slot(0x03, args, 0);
        check_protocol("shutdown");
        h = midi_hex();
        for (i = 0; i < 16; i++) {
            char pat[32];
            sprintf(pat, "B%X 7B 00 B%X 79 00", i, i);
            if (strstr(h, pat) != NULL) {
                silenced++;
            }
        }
        check_int("shutdown silences all 16 channels", (long)silenced, 16L);
        check("shutdown covers channel 0, which MT15.DRV skipped",
              strstr(h, "B0 7B 00 B0 79 00") ? "yes" : "no", "yes");
    }

    /* --- MIDI input ------------------------------------------------------- */
    sprintf(got, "%04X", call_slot(0x3C, args, 0));
    check("MIDI input returns -1 when the queue is empty", got, "FFFF");

    rig.pend[0] = 0xFE;
    rig.pend[1] = 0xFE;
    rig.pend[2] = 0x90;
    rig.pend[3] = 0x40;
    rig.pend_head = 0;
    rig.pend_n = 4;
    rig.tx_stall = 12;
    args[0] = 3;
    args[1] = 0;
    args[2] = 64;
    call_slot(0x12, args, 3);
    rig.tx_stall = 0;
    check("output completes while the interface is feeding bytes back",
          midi_hex(), "B3 07 40");
    check_int("the inbound bytes were consumed rather than left blocking",
              (long)rig.pend_n, 0L);
    got[0] = '\0';
    for (i = 0; i < 5; i++) {
        char one[8];
        sprintf(one, i ? " %04X" : "%04X", call_slot(0x3C, args, 0));
        strcat(got, one);
    }
    check("the received bytes were buffered and can be read back", got,
          "00FE 00FE 0090 0040 FFFF");

    /* --- a transmitter that never becomes ready ---------------------------
     * The stall above is transient and the driver rides it out. This one lasts
     * longer than the timeout, which is the case that used to drop one byte and
     * carry on: a status byte lost that way leaves its data bytes to be folded
     * into the message before it. */
    {
        rig.tx_stall = 40000L; /* longer than MPU_TIMEOUT can poll */
        args[0] = 3;
        args[1] = 0;
        args[2] = 64;
        call_slot(0x12, args, 3);
        check("a transmit timeout emits nothing rather than a partial message",
              midi_hex(), "(nothing)");

        rig.tx_stall = 0; /* the interface recovers, the driver must not */
        call_slot(0x12, args, 3);
        check("after a timeout the driver stays silent instead of sending the "
              "rest",
              midi_hex(), "(nothing)");

        /* Init clears the flag. What proves the recovery is ordinary traffic
         * afterwards, not that init itself emitted something: midi_hex()
         * returns "(nothing)" when nothing was sent, which is still a non-empty
         * string, so testing its first character asserted nothing at all. */
        call_slot(0x00, args, 0);
        args[0] = 3;
        args[1] = 0;
        args[2] = 64;
        call_slot(0x12, args, 3);
        check("a re-init brings output back", midi_hex(), "B3 07 40");
    }

    /* --- slot 27h with the retrigger extension ---------------------------- */
    rt = TMP_INSTR;
    for (i = 0; i < 0x60; i++) {
        emu_wr8(&emu, emu_lin(DATA_SEG, rt + i), 0);
    }
    emu_wr8(&emu, emu_lin(DATA_SEG, rt + I_RETRIG), 3);
    args[0] = 4;
    args[1] = VOICE_OFS;
    args[2] = CHUNK_OFS;
    args[3] = 60;
    args[4] = 100;
    args[5] = rt;
    args[6] = DATA_SEG;
    call_slot(0x09, args, 7);
    check("note on arms the retrigger and plays normally", midi_hex(),
          "94 3C 7F");
    got[0] = '\0';
    for (i = 0; i < 6; i++) {
        args[0] = 4;
        args[1] = VOICE_OFS;
        args[2] = 0;
        args[3] = rt;
        args[4] = DATA_SEG;
        call_slot(0x27, args, 5);
        if (i) {
            strcat(got, " | ");
        }
        strcat(got, midi_hex());
    }
    check("the note is restruck every 3 ticks and is silent in between", got,
          "(nothing) | (nothing) | 84 3C 00 94 3C 7F | (nothing) | (nothing) "
          "| 84 3C 00 94 3C 7F");

    emu_wr8(&emu, emu_lin(DATA_SEG, rt + I_RETRIG), 0);
    args[0] = 4;
    args[1] = VOICE_OFS;
    args[2] = CHUNK_OFS;
    args[3] = 60;
    args[4] = 100;
    args[5] = rt;
    args[6] = DATA_SEG;
    call_slot(0x09, args, 7);
    got[0] = '\0';
    for (i = 0; i < 6; i++) {
        args[0] = 4;
        args[1] = VOICE_OFS;
        args[2] = 0;
        args[3] = rt;
        args[4] = DATA_SEG;
        call_slot(0x27, args, 5);
        if (i) {
            strcat(got, " ");
        }
        strcat(got, midi_hex());
    }
    check("with the period at 0 nothing is restruck, which is how both banks "
          "ship",
          got, "(nothing) (nothing) (nothing) (nothing) (nothing) (nothing)");

    /* --- the slots the engine never calls --------------------------------- */
    {
        static const unsigned int stub[4] = {0x2A, 0x2D, 0x30, 0x33};
        for (i = 0; i < 4; i++) {
            char what[48];
            args[0] = 0;
            args[1] = 0;
            args[2] = 0;
            args[3] = 0;
            call_slot(stub[i], args, 4);
            sprintf(what, "slot %02Xh is an inert stub", stub[i]);
            check(what, midi_hex(), "(nothing)");
        }
    }
    sprintf(got, "%04X", call_slot(0x36, args, 0));
    check("slot 36h answers 0FFh", got, "00FF");

    /* --- no interface at 330h --------------------------------------------- */
    rig.no_hardware = 1;
    sprintf(got, "%04X", call_slot(0x00, args, 0));
    check("init with no MPU-401 still reports success", got, "FFF6");
    args[0] = 2;
    args[1] = VOICE_OFS;
    args[2] = CHUNK_OFS;
    args[3] = 60;
    args[4] = 100;
    args[5] = bass;
    args[6] = DATA_SEG;
    call_slot(0x09, args, 7);
    check("with no MPU-401 the driver emits nothing instead of spinning",
          midi_hex(), "(nothing)");
    rig.no_hardware = 0;

    /* --- init a second time ----------------------------------------------
     *
     * The engine calls init once per run of the game, but a DOS session can
     * run the game more than once, and the second init meets an interface the
     * first one left in UART mode rather than a freshly powered up one. Every
     * check above starts from a clean rig, so none of them covered that.
     *
     * The MPU is deliberately left as the previous test left it. What matters
     * is that init still terminates, still reports the same class, and still
     * puts the interface back through reset and UART select rather than
     * assuming it is already there. */
    {
        unsigned int ax;
        char         cmds[64];

        ax = call_slot(0x00, args, 0);
        check_protocol("second init");
        sprintf(got, "%04X", ax);
        check("a second init still reports success", got, "FFF6");
        sprintf(cmds, "%u %u", rig.cmd_n >= 1 ? rig.cmd[0] : 0,
                rig.cmd_n >= 2 ? rig.cmd[1] : 0);
        check("a second init resets the MPU-401 and reselects UART mode", cmds,
              "255 63");

        /* And the driver has to be usable afterwards, not merely alive. */
        args[0] = 2;
        args[1] = VOICE_OFS;
        args[2] = CHUNK_OFS;
        args[3] = 60;
        args[4] = 100;
        args[5] = bass;
        args[6] = DATA_SEG;
        call_slot(0x09, args, 7);
        check("the driver still plays after a second init", midi_hex(),
              "92 3C 7F");
    }

    /* --- the two checks no amount of listening can make ------------------- */
    sprintf(got, "%s", sp_worst <= 64 ? "yes" : "no");
    sprintf(want, "peak stack use stays small (%u bytes, worst slot %02Xh)",
            sp_worst, sp_worst_slot);
    check(want, got, "yes");

    check("every emitted byte was a valid MIDI byte in a valid position",
          protocol_error[0] ? protocol_error : "clean", "clean");

    if (abi_bad_slot < 0) {
        sprintf(got, "clean");
    } else {
        sprintf(got, "slot %02Xh did not restore one", abi_bad_slot);
    }
    check("every slot returned DS, SS, SI, DI and BP as it found them", got,
          "clean");

    printf("\n%d passed, %d failed\n", passed, failed);
    emu_free(&emu);
    free(drv);
    free(vce);
    return failed ? 1 : 0;
}
