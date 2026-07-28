/* emu86 - just enough 8086 to run SC15.DRV and watch what it does.
 *
 * This is not a general emulator. It implements the instruction subset the
 * driver actually uses and stops with an error on anything else, which is a
 * feature: an unexpected opcode means the assembler emitted something the
 * harness has not seen before, and that is worth failing on.
 *
 * Memory is a sparse page table rather than a flat megabyte, because a flat
 * megabyte cannot exist on a 16-bit host and is wasteful everywhere else. The
 * driver touches three segments, so about six pages are ever allocated.
 *
 * Strict C89, no dependencies.
 */
#ifndef EMU86_H
#define EMU86_H

#define EMU_PAGE_SHIFT 12
#define EMU_PAGE_SIZE (1U << EMU_PAGE_SHIFT)
#define EMU_PAGES 256 /* 256 * 4 KB = the full 1 MB real mode space */

/* Returning to this address ends a call. It is outside anything the driver
 * loads, so it can never be reached by accident. */
#define EMU_DONE_SEG 0xF000
#define EMU_DONE_OFS 0xFFF0

struct emu;

typedef unsigned char (*emu_in_fn)(struct emu *e, unsigned int port);
typedef void (*emu_out_fn)(struct emu *e, unsigned int port,
                           unsigned char value);

struct emu {
    unsigned int ax, cx, dx, bx, sp, bp, si, di;
    unsigned int cs, ds, es, ss, ip;
    int          cf, zf, sf, of, intf;

    unsigned char *page[EMU_PAGES];

    emu_in_fn  in;
    emu_out_fn out;
    void      *host; /* whatever the caller needs on the port callbacks */

    unsigned long steps;    /* instructions executed, a runaway guard */
    unsigned int  sp_start; /* stack pointer when the call began */
    unsigned int  sp_low;   /* deepest it reached, for the stack check */
    const char   *error;    /* NULL while healthy */
};

void emu_init(struct emu *e);
void emu_free(struct emu *e);

unsigned char emu_rd8(struct emu *e, unsigned long lin);
void          emu_wr8(struct emu *e, unsigned long lin, unsigned char v);
unsigned int  emu_rd16(struct emu *e, unsigned long lin);
void          emu_wr16(struct emu *e, unsigned long lin, unsigned int v);

unsigned long emu_lin(unsigned int seg, unsigned int ofs);
void          emu_load(struct emu *e, unsigned int seg, unsigned int ofs,
                       const unsigned char *data, unsigned long len);

void         emu_push(struct emu *e, unsigned int v);
unsigned int emu_pop(struct emu *e);

/* Run until the sentinel return address is reached, e->error is set, or the
 * step limit trips. Returns 0 on a clean finish. */
int emu_run(struct emu *e);

#endif
