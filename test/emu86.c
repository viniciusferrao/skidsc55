/* stddef.h for NULL: a 1988 stdlib.h does not necessarily define it. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "emu86.h"

#define STEP_LIMIT 20000000UL

/* Register numbers, as the opcode encoding orders them. Written as macros
 * rather than an untagged enum, which Microsoft C 5.10 warns about. */
#define R_AX 0
#define R_CX 1
#define R_DX 2
#define R_BX 3
#define R_SP 4
#define R_BP 5
#define R_SI 6
#define R_DI 7

static unsigned int *reg16(struct emu *e, int i)
{
    switch (i) {
    case R_AX:
        return &e->ax;
    case R_CX:
        return &e->cx;
    case R_DX:
        return &e->dx;
    case R_BX:
        return &e->bx;
    case R_SP:
        return &e->sp;
    case R_BP:
        return &e->bp;
    case R_SI:
        return &e->si;
    default:
        return &e->di;
    }
}

static unsigned int get8(struct emu *e, int i)
{
    unsigned int v = *reg16(e, i & 3);
    return (i < 4) ? (v & 0xFF) : ((v >> 8) & 0xFF);
}

static void set8(struct emu *e, int i, unsigned int v)
{
    unsigned int *r = reg16(e, i & 3);
    v &= 0xFF;
    *r = (i < 4) ? ((*r & 0xFF00) | v) : ((*r & 0x00FF) | (v << 8));
}

void emu_init(struct emu *e)
{
    memset(e, 0, sizeof *e);
    e->intf = 1;
}

void emu_free(struct emu *e)
{
    int i;
    for (i = 0; i < EMU_PAGES; i++) {
        if (e->page[i] != NULL) {
            free(e->page[i]);
            e->page[i] = NULL;
        }
    }
}

unsigned long emu_lin(unsigned int seg, unsigned int ofs)
{
    return (((unsigned long)seg << 4) + ofs) & 0xFFFFFUL;
}

static unsigned char *cell(struct emu *e, unsigned long lin)
{
    unsigned int p = (unsigned int)((lin & 0xFFFFFUL) >> EMU_PAGE_SHIFT);
    if (e->page[p] == NULL) {
        e->page[p] = (unsigned char *)calloc(EMU_PAGE_SIZE, 1);
        if (e->page[p] == NULL) {
            e->error = "out of memory";
            return NULL;
        }
    }
    return e->page[p] +
           (unsigned int)(lin & (unsigned long)(EMU_PAGE_SIZE - 1));
}

unsigned char emu_rd8(struct emu *e, unsigned long lin)
{
    unsigned char *c = cell(e, lin);
    return (c == NULL) ? (unsigned char)0 : *c;
}

void emu_wr8(struct emu *e, unsigned long lin, unsigned char v)
{
    unsigned char *c = cell(e, lin);
    if (c != NULL) {
        *c = v;
    }
}

unsigned int emu_rd16(struct emu *e, unsigned long lin)
{
    return (unsigned int)emu_rd8(e, lin) |
           ((unsigned int)emu_rd8(e, lin + 1) << 8);
}

void emu_wr16(struct emu *e, unsigned long lin, unsigned int v)
{
    emu_wr8(e, lin, (unsigned char)(v & 0xFF));
    emu_wr8(e, lin + 1, (unsigned char)((v >> 8) & 0xFF));
}

void emu_load(struct emu *e, unsigned int seg, unsigned int ofs,
              const unsigned char *data, unsigned long len)
{
    unsigned long i;
    for (i = 0; i < len; i++) {
        emu_wr8(e, emu_lin(seg, 0) + ofs + i, data[i]);
    }
}

void emu_push(struct emu *e, unsigned int v)
{
    e->sp = (e->sp - 2) & 0xFFFF;
    if (e->sp < e->sp_low) {
        e->sp_low = e->sp;
    }
    emu_wr16(e, emu_lin(e->ss, e->sp), v);
}

unsigned int emu_pop(struct emu *e)
{
    unsigned int v = emu_rd16(e, emu_lin(e->ss, e->sp));
    e->sp = (e->sp + 2) & 0xFFFF;
    return v;
}

/* --- flags --------------------------------------------------------------- */

static unsigned int logic_flags(struct emu *e, unsigned long v, int w)
{
    unsigned int mask = w ? 0xFFFF : 0xFF;
    unsigned int sign = w ? 0x8000 : 0x80;
    unsigned int r = (unsigned int)(v & mask);
    e->zf = (r == 0);
    e->sf = ((r & sign) != 0);
    e->cf = 0;
    e->of = 0;
    return r;
}

static unsigned int add_flags(struct emu *e, unsigned int a, unsigned int b,
                              int w, int sub)
{
    unsigned long mask = w ? 0xFFFFUL : 0xFFUL;
    unsigned int  sign = w ? 0x8000 : 0x80;
    unsigned long res = sub ? (unsigned long)a - b : (unsigned long)a + b;
    unsigned int  r, sa, sb;

    e->cf = sub ? (b > a) : ((res & ~mask) != 0);
    r = (unsigned int)(res & mask);
    e->zf = (r == 0);
    e->sf = ((r & sign) != 0);
    sa = a & sign;
    sb = b & sign;
    if (sub) {
        sb = sb ? 0 : sign;
    }
    e->of = (((sa ^ r) & (sb ^ r) & sign) != 0);
    return r;
}

/* --- modrm --------------------------------------------------------------- */

struct operand {
    int           is_reg;
    int           reg;
    unsigned long addr;
};

static unsigned int fetch8(struct emu *e)
{
    unsigned int v = emu_rd8(e, emu_lin(e->cs, e->ip));
    e->ip = (e->ip + 1) & 0xFFFF;
    return v;
}

static unsigned int fetch16(struct emu *e)
{
    unsigned int v = emu_rd16(e, emu_lin(e->cs, e->ip));
    e->ip = (e->ip + 2) & 0xFFFF;
    return v;
}

static int sx8(unsigned int v)
{
    return (v & 0x80) ? (int)v - 256 : (int)v;
}

static unsigned int seg_value(struct emu *e, int which)
{
    switch (which) {
    case 0:
        return e->es;
    case 1:
        return e->cs;
    case 2:
        return e->ss;
    default:
        return e->ds;
    }
}

static void modrm(struct emu *e, int seg_override, int *reg, struct operand *o)
{
    unsigned int m = fetch8(e);
    int          mod = (m >> 6) & 3;
    int          rm = m & 7;
    unsigned int base = 0;
    int          defseg = 3; /* ds */

    *reg = (m >> 3) & 7;
    if (mod == 3) {
        o->is_reg = 1;
        o->reg = rm;
        return;
    }
    o->is_reg = 0;
    if (mod == 0 && rm == 6) {
        base = fetch16(e);
    } else {
        switch (rm) {
        case 0:
            base = e->bx + e->si;
            break;
        case 1:
            base = e->bx + e->di;
            break;
        case 2:
            base = e->bp + e->si;
            defseg = 2;
            break;
        case 3:
            base = e->bp + e->di;
            defseg = 2;
            break;
        case 4:
            base = e->si;
            break;
        case 5:
            base = e->di;
            break;
        case 6:
            base = e->bp;
            defseg = 2;
            break;
        default:
            base = e->bx;
            break;
        }
        if (mod == 1) {
            base += (unsigned int)sx8(fetch8(e));
        } else if (mod == 2) {
            base += fetch16(e);
        }
    }
    o->addr = emu_lin(seg_value(e, seg_override >= 0 ? seg_override : defseg),
                      base & 0xFFFF);
}

static unsigned int op_get(struct emu *e, const struct operand *o, int w)
{
    if (o->is_reg) {
        return w ? *reg16(e, o->reg) : get8(e, o->reg);
    }
    return w ? emu_rd16(e, o->addr) : emu_rd8(e, o->addr);
}

static void op_set(struct emu *e, const struct operand *o, int w,
                   unsigned int v)
{
    if (o->is_reg) {
        if (w) {
            *reg16(e, o->reg) = v & 0xFFFF;
        } else {
            set8(e, o->reg, v);
        }
    } else if (w) {
        emu_wr16(e, o->addr, v);
    } else {
        emu_wr8(e, o->addr, (unsigned char)v);
    }
}

static unsigned int alu(struct emu *e, int kind, unsigned int a, unsigned int b,
                        int w)
{
    switch (kind) {
    case 0:
        return add_flags(e, a, b, w, 0); /* add */
    case 1:
        return logic_flags(e, (unsigned long)(a | b), w);
    case 4:
        return logic_flags(e, (unsigned long)(a & b), w);
    case 5:
        return add_flags(e, a, b, w, 1); /* sub */
    case 6:
        return logic_flags(e, (unsigned long)(a ^ b), w);
    case 7:
        return add_flags(e, a, b, w, 1); /* cmp */
    default:
        e->error = "adc or sbb is not modelled";
        return 0;
    }
}

static int cond(struct emu *e, int c)
{
    switch (c) {
    case 0x0:
        return e->of;
    case 0x1:
        return !e->of;
    case 0x2:
        return e->cf;
    case 0x3:
        return !e->cf;
    case 0x4:
        return e->zf;
    case 0x5:
        return !e->zf;
    case 0x6:
        return e->cf || e->zf;
    case 0x7:
        return !(e->cf || e->zf);
    case 0x8:
        return e->sf;
    case 0x9:
        return !e->sf;
    case 0xA:
        return 0; /* parity, unused by the driver */
    case 0xB:
        return 1;
    case 0xC:
        return e->sf != e->of;
    case 0xD:
        return e->sf == e->of;
    case 0xE:
        return e->zf || (e->sf != e->of);
    default:
        return !e->zf && (e->sf == e->of);
    }
}

int emu_run(struct emu *e)
{
    e->sp_start = e->sp;
    e->sp_low = e->sp;

    for (;;) {
        int            seg_override = -1;
        unsigned int   op;
        int            reg;
        struct operand o;

        if (e->error != NULL) {
            return 1;
        }
        if (e->cs == EMU_DONE_SEG && e->ip == EMU_DONE_OFS) {
            return 0;
        }
        if (++e->steps > STEP_LIMIT) {
            e->error = "step limit exceeded, the driver is looping";
            return 1;
        }

        for (;;) {
            op = fetch8(e);
            if (op == 0x26) {
                seg_override = 0;
                continue;
            }
            if (op == 0x2E) {
                seg_override = 1;
                continue;
            }
            if (op == 0x36) {
                seg_override = 2;
                continue;
            }
            if (op == 0x3E) {
                seg_override = 3;
                continue;
            }
            break;
        }

        /* 00..3D, the ALU block, minus the segment pushes that share it. */
        if (op < 0x40 && (op & 7) < 6) {
            int            kind = op >> 3;
            int            lo = op & 7;
            unsigned int   a, b, res;
            struct operand dst;
            int            w;

            if (lo == 4) {
                w = 0;
                a = get8(e, R_AX);
                b = fetch8(e);
                dst.is_reg = 1;
                dst.reg = R_AX;
            } else if (lo == 5) {
                w = 1;
                a = e->ax;
                b = fetch16(e);
                dst.is_reg = 1;
                dst.reg = R_AX;
            } else {
                unsigned int rv;
                w = lo & 1;
                modrm(e, seg_override, &reg, &o);
                rv = w ? *reg16(e, reg) : get8(e, reg);
                if (lo & 2) {
                    a = rv;
                    b = op_get(e, &o, w);
                    dst.is_reg = 1;
                    dst.reg = reg;
                } else {
                    a = op_get(e, &o, w);
                    b = rv;
                    dst = o;
                }
            }
            res = alu(e, kind, a, b, w);
            if (kind != 7) {
                op_set(e, &dst, w, res);
            }
            continue;
        }

        if (op >= 0x40 && op <= 0x4F) {
            int           i = op & 7;
            int           carry = e->cf;
            unsigned int *r = reg16(e, i);
            *r = add_flags(e, *r, 1, 1, (op >= 0x48));
            e->cf = carry; /* inc and dec leave CF alone */
            continue;
        }
        if (op >= 0x50 && op <= 0x57) {
            emu_push(e, *reg16(e, op & 7));
            continue;
        }
        if (op >= 0x58 && op <= 0x5F) {
            *reg16(e, op & 7) = emu_pop(e);
            continue;
        }
        if (op == 0x06) {
            emu_push(e, e->es);
            continue;
        }
        if (op == 0x07) {
            e->es = emu_pop(e);
            continue;
        }
        if (op == 0x0E) {
            emu_push(e, e->cs);
            continue;
        }
        if (op == 0x16) {
            emu_push(e, e->ss);
            continue;
        }
        if (op == 0x17) {
            e->ss = emu_pop(e);
            continue;
        }
        if (op == 0x1E) {
            emu_push(e, e->ds);
            continue;
        }
        if (op == 0x1F) {
            e->ds = emu_pop(e);
            continue;
        }

        if (op >= 0x70 && op <= 0x7F) {
            int d = sx8(fetch8(e));
            if (cond(e, op & 0x0F)) {
                e->ip = (e->ip + d) & 0xFFFF;
            }
            continue;
        }

        if (op >= 0x80 && op <= 0x83) {
            int          w = op & 1;
            unsigned int a, b, res;
            modrm(e, seg_override, &reg, &o);
            if (op == 0x81) {
                b = fetch16(e);
            } else if (op == 0x83) {
                b = (unsigned int)sx8(fetch8(e)) & 0xFFFF;
            } else {
                b = fetch8(e);
            }
            a = op_get(e, &o, w);
            res = alu(e, reg, a, b, w);
            if (reg != 7) {
                op_set(e, &o, w, res);
            }
            continue;
        }

        if (op >= 0x84 && op <= 0x8B) {
            int          w = op & 1;
            unsigned int rv;
            modrm(e, seg_override, &reg, &o);
            rv = w ? *reg16(e, reg) : get8(e, reg);
            if (op < 0x86) {
                logic_flags(e, (unsigned long)(rv & op_get(e, &o, w)), w);
            } else if (op < 0x88) {
                unsigned int t = op_get(e, &o, w);
                op_set(e, &o, w, rv);
                if (w) {
                    *reg16(e, reg) = t;
                } else {
                    set8(e, reg, t);
                }
            } else if (op < 0x8A) {
                op_set(e, &o, w, rv);
            } else {
                unsigned int v = op_get(e, &o, w);
                if (w) {
                    *reg16(e, reg) = v;
                } else {
                    set8(e, reg, v);
                }
            }
            continue;
        }

        if (op == 0x8C) {
            modrm(e, seg_override, &reg, &o);
            op_set(e, &o, 1, seg_value(e, reg & 3));
            continue;
        }
        if (op == 0x8E) {
            unsigned int v;
            modrm(e, seg_override, &reg, &o);
            v = op_get(e, &o, 1);
            switch (reg & 3) {
            case 0:
                e->es = v;
                break;
            case 1:
                e->cs = v;
                break;
            case 2:
                e->ss = v;
                break;
            default:
                e->ds = v;
                break;
            }
            continue;
        }
        if (op == 0x8D) { /* lea */
            unsigned long segbase;
            modrm(e, seg_override, &reg, &o);
            if (o.is_reg) {
                e->error = "lea on a register";
                continue;
            }
            segbase =
                emu_lin(seg_value(e, seg_override >= 0 ? seg_override : 3), 0);
            *reg16(e, reg) = (unsigned int)(o.addr - segbase);
            continue;
        }
        if (op == 0x8F) {
            modrm(e, seg_override, &reg, &o);
            op_set(e, &o, 1, emu_pop(e));
            continue;
        }
        if (op == 0x90) {
            continue;
        }
        if (op >= 0x91 && op <= 0x97) {
            unsigned int *r = reg16(e, op & 7);
            unsigned int  t = e->ax;
            e->ax = *r;
            *r = t;
            continue;
        }
        if (op == 0x98) {
            set8(e, 4, (get8(e, R_AX) & 0x80) ? 0xFF : 0x00);
            continue;
        }
        if (op == 0x99) {
            e->dx = (e->ax & 0x8000) ? 0xFFFF : 0x0000;
            continue;
        }
        if (op == 0x9C) {
            emu_push(e,
                     (unsigned int)((e->cf ? 1 : 0) | (e->zf ? 0x40 : 0) |
                                    (e->sf ? 0x80 : 0) | (e->intf ? 0x200 : 0) |
                                    (e->of ? 0x800 : 0)));
            continue;
        }
        if (op == 0x9D) {
            unsigned int f = emu_pop(e);
            e->cf = (f & 1) != 0;
            e->zf = (f & 0x40) != 0;
            e->sf = (f & 0x80) != 0;
            e->intf = (f & 0x200) != 0;
            e->of = (f & 0x800) != 0;
            continue;
        }

        if (op >= 0xA0 && op <= 0xA3) {
            int           w = op & 1;
            unsigned long a = emu_lin(
                seg_value(e, seg_override >= 0 ? seg_override : 3), fetch16(e));
            if (op < 0xA2) {
                if (w) {
                    e->ax = emu_rd16(e, a);
                } else {
                    set8(e, R_AX, emu_rd8(e, a));
                }
            } else if (w) {
                emu_wr16(e, a, e->ax);
            } else {
                emu_wr8(e, a, (unsigned char)get8(e, R_AX));
            }
            continue;
        }
        if (op == 0xA8) {
            logic_flags(e, (unsigned long)(get8(e, R_AX) & fetch8(e)), 0);
            continue;
        }
        if (op == 0xA9) {
            logic_flags(e, (unsigned long)(e->ax & fetch16(e)), 1);
            continue;
        }

        if (op >= 0xB0 && op <= 0xB7) {
            set8(e, op & 7, fetch8(e));
            continue;
        }
        if (op >= 0xB8 && op <= 0xBF) {
            *reg16(e, op & 7) = fetch16(e);
            continue;
        }

        if (op == 0xC3) {
            e->ip = emu_pop(e);
            continue;
        }
        if (op == 0xCB) {
            e->ip = emu_pop(e);
            e->cs = emu_pop(e);
            continue;
        }

        if (op == 0xC4 || op == 0xC5) {
            modrm(e, seg_override, &reg, &o);
            if (o.is_reg) {
                e->error = "les or lds on a register";
                continue;
            }
            *reg16(e, reg) = emu_rd16(e, o.addr);
            if (op == 0xC4) {
                e->es = emu_rd16(e, o.addr + 2);
            } else {
                e->ds = emu_rd16(e, o.addr + 2);
            }
            continue;
        }
        if (op == 0xC6 || op == 0xC7) {
            int w = op & 1;
            modrm(e, seg_override, &reg, &o);
            op_set(e, &o, w, w ? fetch16(e) : fetch8(e));
            continue;
        }

        if (op >= 0xD0 && op <= 0xD3) {
            int          w = op & 1;
            unsigned int mask = w ? 0xFFFF : 0xFF;
            unsigned int sign = w ? 0x8000 : 0x80;
            unsigned int count, v;
            modrm(e, seg_override, &reg, &o);
            count = (op < 0xD2) ? 1 : (e->cx & 0xFF);
            v = op_get(e, &o, w);
            while (count-- > 0) {
                if (reg == 4 || reg == 6) { /* shl, sal */
                    e->cf = ((v & sign) != 0);
                    v = (v << 1) & mask;
                } else if (reg == 5) { /* shr */
                    e->cf = (v & 1) != 0;
                    v = (v >> 1) & mask;
                } else if (reg == 0) { /* rol */
                    e->cf = ((v & sign) != 0);
                    v = ((v << 1) | (e->cf ? 1 : 0)) & mask;
                } else if (reg == 7) { /* sar */
                    e->cf = (v & 1) != 0;
                    v = ((v >> 1) | (v & sign)) & mask;
                } else {
                    e->error = "rotate through carry is not modelled";
                    break;
                }
            }
            e->zf = (v == 0);
            e->sf = ((v & sign) != 0);
            op_set(e, &o, w, v);
            continue;
        }

        if (op >= 0xE0 && op <= 0xE2) {
            int d = sx8(fetch8(e));
            int take;
            e->cx = (e->cx - 1) & 0xFFFF;
            take = (e->cx != 0);
            if (op == 0xE0) {
                take = take && !e->zf;
            }
            if (op == 0xE1) {
                take = take && e->zf;
            }
            if (take) {
                e->ip = (e->ip + d) & 0xFFFF;
            }
            continue;
        }
        if (op == 0xE3) {
            int d = sx8(fetch8(e));
            if (e->cx == 0) {
                e->ip = (e->ip + d) & 0xFFFF;
            }
            continue;
        }

        if (op == 0xE4) {
            set8(e, R_AX, e->in(e, fetch8(e)));
            continue;
        }
        if (op == 0xE6) {
            unsigned int p = fetch8(e);
            e->out(e, p, (unsigned char)get8(e, R_AX));
            continue;
        }
        if (op == 0xEC) {
            set8(e, R_AX, e->in(e, e->dx));
            continue;
        }
        if (op == 0xEE) {
            e->out(e, e->dx, (unsigned char)get8(e, R_AX));
            continue;
        }

        if (op == 0xE8) {
            unsigned int d = fetch16(e);
            emu_push(e, e->ip);
            e->ip = (e->ip + d) & 0xFFFF;
            continue;
        }
        if (op == 0xE9) {
            unsigned int d = fetch16(e);
            e->ip = (e->ip + d) & 0xFFFF;
            continue;
        }
        if (op == 0xEB) {
            int d = sx8(fetch8(e));
            e->ip = (e->ip + d) & 0xFFFF;
            continue;
        }

        if (op == 0xF8) {
            e->cf = 0;
            continue;
        }
        if (op == 0xF9) {
            e->cf = 1;
            continue;
        }
        if (op == 0xFA) {
            e->intf = 0;
            continue;
        }
        if (op == 0xFB) {
            e->intf = 1;
            continue;
        }
        if (op == 0xFC || op == 0xFD) {
            continue;
        } /* cld, std: no string ops */

        if (op == 0xF6 || op == 0xF7) {
            int          w = op & 1;
            unsigned int v;
            modrm(e, seg_override, &reg, &o);
            v = op_get(e, &o, w);
            if (reg == 0 || reg == 1) {
                unsigned int imm = w ? fetch16(e) : fetch8(e);
                logic_flags(e, (unsigned long)(v & imm), w);
            } else if (reg == 2) {
                op_set(e, &o, w, ~v);
            } else if (reg == 3) {
                op_set(e, &o, w, add_flags(e, 0, v, w, 1));
            } else if (reg == 4) { /* mul */
                if (w) {
                    unsigned long p = (unsigned long)e->ax * v;
                    e->ax = (unsigned int)(p & 0xFFFF);
                    e->dx = (unsigned int)((p >> 16) & 0xFFFF);
                    e->cf = e->of = (e->dx != 0);
                } else {
                    unsigned int p = get8(e, R_AX) * v;
                    e->ax = p & 0xFFFF;
                    e->cf = e->of = (p > 0xFF);
                }
            } else if (reg == 6) { /* div */
                if (v == 0) {
                    e->error = "divide by zero";
                } else if (w) {
                    unsigned long num = ((unsigned long)e->dx << 16) | e->ax;
                    unsigned long q = num / v;
                    if (q > 0xFFFFUL) {
                        e->error = "divide overflow";
                    } else {
                        e->ax = (unsigned int)q;
                        e->dx = (unsigned int)(num % v);
                    }
                } else {
                    unsigned int q = e->ax / v;
                    if (q > 0xFF) {
                        e->error = "divide overflow";
                    } else {
                        set8(e, R_AX, q);
                        set8(e, 4, e->ax % v);
                    }
                }
            } else {
                e->error = "imul or idiv is not modelled";
            }
            continue;
        }

        if (op == 0xFE || op == 0xFF) {
            int w = op & 1;
            modrm(e, seg_override, &reg, &o);
            if (reg == 0 || reg == 1) {
                int carry = e->cf;
                op_set(e, &o, w, add_flags(e, op_get(e, &o, w), 1, w, reg));
                e->cf = carry;
                continue;
            }
            if (op == 0xFF && reg == 2) {
                emu_push(e, e->ip);
                e->ip = op_get(e, &o, 1);
                continue;
            }
            if (op == 0xFF && reg == 6) {
                emu_push(e, op_get(e, &o, 1));
                continue;
            }
            e->error = "unmodelled group 4 or 5 operation";
            continue;
        }

        e->error = "unmodelled opcode";
        return 1;
    }
}
