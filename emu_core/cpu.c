
#include "gnuboy.h"
#include "defs.h"
#include "regs.h"
#include "hw.h"
#include "lcd.h"
#include "cpu.h"
#include "mem.h"
#include "fastmem.h"
#include "cpuregs.h"
#include "cpucore.h"

#ifdef USE_ASM
#include "asm.h"
#endif

#include "../cpu_tables/zflags.h"
#include "../cpu_tables/rlca.h"
#include "../cpu_tables/rrca.h"
#include "../cpu_tables/rra.h"
#include "../cpu_tables/rla.h"
#include "../cpu_tables/scf.h"
#include "../cpu_tables/ccf.h"
#include "../cpu_tables/incdec.h"
#include "../cpu_tables/and_zflags.h"
#include "../cpu_tables/bit_states.h"

struct cpu cpu;


#if 1
#define LB_ACCUMULATOR lb_acc[0]
#define HB_ACCUMULATOR hb_acc[0]
#else
#define LB_ACCUMULATOR LB(acc)
#define HB_ACCUMULATOR HB(acc)
#endif

#if 1
//generates sltu a,b,c sll a,a,7
#define ZFLAG(n) (((un32)n < (un32)1) << (un32)7)
#else
#define ZFLAG(n) r_zf[n]
#endif

#define PUSH(w) ( (SP += -2), (writew(xSP, (w))) )
#define POP(w) ( ((w) = readw(xSP)), (SP += 2) )


#define FETCH_OLD ( mbc.rmap[PC>>12] \
? mbc.rmap[PC>>12][PC++] \
: mem_read(PC++) )

#define FETCH (readb(PC++))


#define INC(r) { ((r)++); \
F = (F & (FL|FC)) | incflag_table[(r)]; }

#define DEC(r) { ((r) += -1); \
F = (F & (FL|FC)) | decflag_table[(r)]; }

#define INCW(r) ( (r)++ )

#define DECW(r) ( (r) += -1 )

#define ADD(n) { \
W(acc) = (un16)A + (un16)(n); \
F = (ZFLAG(LB_ACCUMULATOR)) \
| (FH & ((A ^ (n) ^ LB_ACCUMULATOR) << 1)) \
| (HB_ACCUMULATOR << 4); \
A = LB_ACCUMULATOR; }

#define ADC(n) { \
W(acc) = (un16)A + (un16)(n) + (un16)((F&FC)>>4); \
F = (ZFLAG(LB_ACCUMULATOR)) \
| (FH & ((A ^ (n) ^ LB_ACCUMULATOR) << 1)) \
| (HB_ACCUMULATOR << 4); \
A = LB_ACCUMULATOR; }

#define ADDW(n) { \
DW(acc) = (un32)HL + (un32)(n); \
F = (F & (FZ)) \
| (FH & ((H ^ ((n)>>8) ^ HB_ACCUMULATOR) << 1)) \
| (acc.b[HI][LO] << 4); \
HL = W(acc); }

#define ADDSP(n) { \
DW(acc) = (un32)SP + (un32)(n8)(n); \
F = (FH & (((SP>>8) ^ ((n)>>8) ^ HB_ACCUMULATOR) << 1)) \
| (acc.b[HI][LO] << 4); \
SP = W(acc); }

#define LDHLSP(n) { \
DW(acc) = (un32)SP + (un32)(n8)(n); \
F = (FH & (((SP>>8) ^ ((n)>>8) ^ HB_ACCUMULATOR) << 1)) \
| (acc.b[HI][LO] << 4); \
HL = W(acc); }

#define CP(n) { \
W(acc) = (un16)A - (un16)(n); \
F = FN \
| (ZFLAG(LB_ACCUMULATOR)) \
| (FH & ((A ^ (n) ^ LB_ACCUMULATOR) << 1)) \
| ((un8)(-(n8)HB_ACCUMULATOR) << 4); }

#define SUB(n) { CP((n)); A = LB_ACCUMULATOR; }

#define SBC(n) { \
W(acc) = (un16)A - (un16)(n) - (un16)((F&FC)>>4); \
F = FN \
| (ZFLAG((n8)LB_ACCUMULATOR)) \
| (FH & ((A ^ (n) ^ LB_ACCUMULATOR) << 1)) \
| ((un8)(-(n8)HB_ACCUMULATOR) << 4); \
A = LB_ACCUMULATOR; }

#define AND(n) { A &= (n); \
F = ZFLAG(A) | FH; }

#define XOR(n) { A ^= (n); \
F = ZFLAG(A); }

#define OR(n) { A |= (n); \
F = ZFLAG(A); }

#define RLCA(r) { (r) = ((r)>>7) | ((r)<<1); \
F = (((r)&0x01)<<4); }

#define RRCA(r) { (r) = ((r)<<7) | ((r)>>1); \
F = (((r)&0x80)>>3); }

#define RLA(r) { \
LB_ACCUMULATOR = (((r)&0x80)>>3); \
(r) = ((r)<<1) | ((F&FC)>>4); \
F = LB_ACCUMULATOR; }

#define RRA(r) { \
LB_ACCUMULATOR = (((r)&0x01)<<4); \
(r) = ((r)>>1) | ((F&FC)<<3); \
F = LB_ACCUMULATOR; }

#define RLC(r) { RLCA(r); F |= ZFLAG(r); }
#define RRC(r) { RRCA(r); F |= ZFLAG(r); }
#define RL(r) { RLA(r); F |= ZFLAG(r); }
#define RR(r) { RRA(r); F |= ZFLAG(r); }

#define SLA(r) { \
LB_ACCUMULATOR = (((r)&0x80)>>3); \
(r) <<= 1; \
F = ZFLAG((r)) | LB_ACCUMULATOR; }

#define SRA(r) { \
LB_ACCUMULATOR = (((r)&0x01)<<4); \
(r) = (un8)(((n8)(r))>>1); \
F = ZFLAG((r)) | LB_ACCUMULATOR; }

#define SRL(r) { \
LB_ACCUMULATOR = (((r)&0x01)<<4); \
(r) >>= 1; \
F = ZFLAG((r)) | LB_ACCUMULATOR; }

#define CPL(r) { \
(r) = ~(r); \
F |= (FH|FN); }

#define SCF { F = (F & (FZ)) | FC; }

#define CCF { F = (F & (FZ|FC)) ^ FC; }

#define DAA { \
A += (LB_ACCUMULATOR = daa_table[((((int)F)&0x70)<<4) | A]); \
F = (F & (FN)) | ZFLAG(A) | daa_carry_table[LB_ACCUMULATOR>>2]; }

#define SWAP(r) { \
(r) = swap_table[(r)]; \
F = ZFLAG((r)); }

#define BIT(n,r) { F = (F & FC) | ZFLAG(((r) & (1 << (n)))) | FH; }
#define RES(n,r) { (r) &= ~(1 << (n)); }
#define SET(n,r) { (r) |= (1 << (n)); }

#define CB_REG_CASES(r, n) \
case 0x00|(n): RLC(r); break; \
case 0x08|(n): RRC(r); break; \
case 0x10|(n): RL(r); break; \
case 0x18|(n): RR(r); break; \
case 0x20|(n): SLA(r); break; \
case 0x28|(n): SRA(r); break; \
case 0x30|(n): SWAP(r); break; \
case 0x38|(n): SRL(r); break; \
case 0x40|(n): BIT(0, r); break; \
case 0x48|(n): BIT(1, r); break; \
case 0x50|(n): BIT(2, r); break; \
case 0x58|(n): BIT(3, r); break; \
case 0x60|(n): BIT(4, r); break; \
case 0x68|(n): BIT(5, r); break; \
case 0x70|(n): BIT(6, r); break; \
case 0x78|(n): BIT(7, r); break; \
case 0x80|(n): RES(0, r); break; \
case 0x88|(n): RES(1, r); break; \
case 0x90|(n): RES(2, r); break; \
case 0x98|(n): RES(3, r); break; \
case 0xA0|(n): RES(4, r); break; \
case 0xA8|(n): RES(5, r); break; \
case 0xB0|(n): RES(6, r); break; \
case 0xB8|(n): RES(7, r); break; \
case 0xC0|(n): SET(0, r); break; \
case 0xC8|(n): SET(1, r); break; \
case 0xD0|(n): SET(2, r); break; \
case 0xD8|(n): SET(3, r); break; \
case 0xE0|(n): SET(4, r); break; \
case 0xE8|(n): SET(5, r); break; \
case 0xF0|(n): SET(6, r); break; \
case 0xF8|(n): SET(7, r); break;

#define ALU_CASES(base, imm, op, label) \
case (imm): b = FETCH;  op(b); break; \
case (base): b = B;  op(b); break; \
case (base)+1: b = C;  op(b); break; \
case (base)+2: b = D;  op(b); break; \
case (base)+3: b = E;  op(b); break; \
case (base)+4: b = H;  op(b); break; \
case (base)+5: b = L;  op(b); break; \
case (base)+6: b = readb(HL);  op(b); break; \
case (base)+7: b = A;  op(b); break;








#define JR ( PC += 1+(n8)readb(PC) )
#define JP ( PC = readw(PC) )

#define CALL ( PUSH(PC+2), JP )

#define NOJR ( clen += -1, PC++ )
#define NOJP ( clen += -1, PC+=2 )
#define NOCALL ( clen+=-3, PC+=2 )
#define NORET ( clen+=-3 )

#define RST(n) { PUSH(PC); PC = (n); }

#define RET ( POP(PC) )

#define EI ( IMA = 1 )
#define DI ( cpu.halt = IMA = IME = 0 )



#define PRE_INT ( DI, PUSH(PC) )
#define THROW_INT(n) ( (IF &= ~(1<<(n))), (PC = 0x40+((n)<<3)) )





/* A:
	Set lcdc ahead of cpu by 19us (matches minimal hblank duration according
	to some docs). Value from cpu.lcdc (when positive) is used to drive CPU,
	setting some ahead-time at startup is necessary to begin emulation.
*/


void cpu_reset()
{
	cpu.speed = 0;
	cpu.halt = 0;
	cpu.div = 0;
	cpu.tim = 0;
	/* set lcdc ahead of cpu by 19us; see A */
	/* FIXME: leave value at 0, use lcdc_trans() to actually send lcdc ahead */
	cpu.lcdc = 40;

	IME = 0;
	IMA = 0;
	
	PC = 0x0100;
	SP = 0xFFFE;
	AF = 0x01B0;
	BC = 0x0013;
	DE = 0x00D8;
	HL = 0x014D;
	
	if (hw.cgb) A = 0x11;
	if (hw.gba) B = 0x01;
}

/* cnt - time to emulate, expressed in 2MHz units in
	single-speed and 4MHz units in double speed mode
*/
/* FIXME: employ common unit to drive whatever_advance(),
	(double-speed machine cycles (2MHz) is a good candidate)
	handle differences in place */
void div_advance(int cnt)
{
	cpu.div += (cnt<<1);
	if (cpu.div >= 256)
	{
		R_DIV += (cpu.div >> 8);
		cpu.div &= 0xff;
	}
}

/* cnt - time to emulate, expressed in 2MHz units in
	single-speed and 4MHz units in double speed mode
*/
/* FIXME: employ common unit to drive whatever_advance(),
	(double-speed machine cycles (2MHz) is a good candidate)
	handle differences in place */
void timer_advance(int cnt)
{
	int unit, tima;
	
	if (!(R_TAC & 0x04)) return;

	unit = ((-R_TAC) & 3) << 1;
	cpu.tim += (cnt<<unit);

	if (cpu.tim >= 512)
	{
		tima = R_TIMA + (cpu.tim >> 9);
		cpu.tim &= 0x1ff;
#if 0
		if (tima >= 256)
		{
			hw_interrupt(IF_TIMER, IF_TIMER);
			hw_interrupt(0, IF_TIMER);
		}

		while (tima >= 256)
			tima = tima - 256 + R_TMA;
#else
		if (tima >= 256) {
			hw_interrupt(IF_TIMER, IF_TIMER);
			hw_interrupt(0, IF_TIMER);
			tima = ((tima>>8) + (tima&0xff)) + R_TMA;
		}
#endif
		R_TIMA = tima;
	}
}

/* cnt - time to emulate, expressed in 2MHz units
	Will call lcdc_trans() if CPU emulation catched up or
	went ahead of LCDC, so that lcd never falls	behind
*/
void lcdc_advance(int cnt)
{
	cpu.lcdc -= cnt;
	if (cpu.lcdc <= 0) lcdc_trans();
}

/* cnt - time to emulate, expressed in 2MHz units */
void sound_advance(int cnt)
{
	cpu.snd += cnt;
}

/* cnt - time to emulate, expressed in 2MHz units */
void cpu_timers(int cnt)
{
	div_advance(cnt << cpu.speed);
	timer_advance(cnt << cpu.speed);
	lcdc_advance(cnt);
	sound_advance(cnt);
}

/* cpu_idle() 
	Skip idle phase of CPU operation, if any
	
	max - maximum time to skip expressed in 2MHz units
	returns number of cycles skipped
*/
/* FIXME: bring cpu_timers() out, make caller advance system */
int cpu_idle(int max)
{
	int cnt, unit;

	if (!(cpu.halt && IME)) return 0;
	if (R_IF & R_IE)
	{
		cpu.halt = 0;
		return 0;
	}

	/* Make sure we don't miss lcdc status events! */
	if ((R_IE & (IF_VBLANK | IF_STAT)) && (max > cpu.lcdc))
		max = cpu.lcdc;
	
	/* If timer interrupt cannot happen, this is very simple! */
	if (!((R_IE & IF_TIMER) && (R_TAC & 0x04)))
	{
		cpu_timers(max);
		return max;
	}

	/* Figure out when the next timer interrupt will happen */
	unit = ((-R_TAC) & 3) << 1;
	cnt = (511 - cpu.tim + (1<<unit)) >> unit;
	cnt += (255 - R_TIMA) << (9 - unit);

	if (max < cnt)
		cnt = max;
	
	cpu_timers(cnt);
	return cnt;
}

#ifndef ASM_CPU_EMULATE
 

/* cpu_emulate()
	Emulate CPU for time no less than specified
	
	cycles - time to emulate, expressed in 2MHz units
	returns number of cycles emulated
	
	Might emulate up to cycles+(11) time units (longest op takes 12
	cycles in single-speed mode)
*/

void do_ints() {
		PRE_INT;
		switch ((byte)(IF & IE))
		{
		case 0x01: case 0x03: case 0x05: case 0x07:
		case 0x09: case 0x0B: case 0x0D: case 0x0F:
		case 0x11: case 0x13: case 0x15: case 0x17:
		case 0x19: case 0x1B: case 0x1D: case 0x1F:
			THROW_INT(0); return;;
		case 0x02: case 0x06: case 0x0A: case 0x0E:
		case 0x12: case 0x16: case 0x1A: case 0x1E:
			THROW_INT(1);return;
		case 0x04: case 0x0C: case 0x14: case 0x1C:
			THROW_INT(2); return;
		case 0x08: case 0x18:
			THROW_INT(3); return;
		case 0x10:
			THROW_INT(4); return;
		}
}

	static union reg __attribute__((aligned(16)))  acc;
	static byte  b ;
	static word w;

int __attribute__((hot)) cpu_emulate(int cycles)
{

	int i;
	byte op, cbop;
	int clen;
 
 	if (cycles<0){return cycles;}

	//register const byte* r_zf  = zflag_table;
	register byte* r_a  asm("$22")  ; r_a = &HB(cpu.af);
	register byte* r_f  asm("$23") ; r_f = &LB(cpu.af);
	register byte* r_b  = &HB(cpu.bc);
	register byte* r_c   = &LB(cpu.bc);
	register byte* r_d  = &HB(cpu.de);
	register byte* r_e   = &LB(cpu.de);
	register byte* r_h  = &HB(cpu.hl);
	register byte* r_l  = &LB(cpu.hl); 
	register word* r_hl  = &W(cpu.hl);
	register word* /*r_sp asm("$19");*/r_sp =&W(cpu.sp);
	register word*  r_pc asm("$20")  ;  r_pc = &W(cpu.pc);
	register un32*  r_xhl asm("$21")  ;  r_xhl = &DW(cpu.hl);
	register un32* r_xsp   = &DW(cpu.sp);
	register un32* r_xpc = &DW(cpu.pc);
 
	register  byte* lb_acc  = &LB(acc);
	register  byte* hb_acc   = &HB(acc);

 
#undef PC
#define PC r_pc[0]
#undef xPC
#define xPC r_xpc[0]

#undef SP
#define SP r_sp[0]

#undef xSP
#define xSP r_xsp[0]

#undef xHL
#define xHL r_xhl[0]

#undef HL
#define HL r_hl[0]

#undef A
#define A r_a[0]

#undef F
#define F r_f[0]
 
#undef B
#define B r_b[0]

#undef C
#define C r_c[0]

#undef D
#define D r_d[0]

#undef E
#define E r_e[0]

#undef H
#define H r_h[0]

#undef L
#define L r_l[0]

	i = cycles;
 
next:
	/* Skip idle cycles */
	if ((clen = cpu_idle(i)))
	{
		i -= clen;
		if (i > 0) goto next;
		return cycles-i;
	}

	/* Handle interrupts */
	if (IME && (IF & IE))
	{
		do_ints();
	}
	IME = IMA;
	
 
	op = FETCH;
	clen = cycles_table[op];

	switch(op)
	{
#if 0
	case 0x00: /* NOP */
	case 0x40: /* LD B,B */
	case 0x49: /* LD C,C */
	case 0x52: /* LD D,D */
	case 0x5B: /* LD E,E */
	case 0x64: /* LD H,H */
	case 0x6D: /* LD L,L */
	case 0x7F: /* LD A,A */
		break;
#endif		
	case 0x41: /* LD B,C */
		B = C; break;
	case 0x42: /* LD B,D */
		B = D; break;
	case 0x43: /* LD B,E */
		B = E; break;
	case 0x44: /* LD B,H */
		B = H; break;
	case 0x45: /* LD B,L */
		B = L; break;
	case 0x46: /* LD B,(HL) */
		B = readb(xHL); break;
	case 0x47: /* LD B,A */
		B = A; break;

	case 0x48: /* LD C,B */
		C = B; break;
	case 0x4A: /* LD C,D */
		C = D; break;
	case 0x4B: /* LD C,E */
		C = E; break;
	case 0x4C: /* LD C,H */
		C = H; break;
	case 0x4D: /* LD C,L */
		C = L; break;
	case 0x4E: /* LD C,(HL) */
		C = readb(xHL); break;
	case 0x4F: /* LD C,A */
		C = A; break;

	case 0x50: /* LD D,B */
		D = B; break;
	case 0x51: /* LD D,C */
		D = C; break;
	case 0x53: /* LD D,E */
		D = E; break;
	case 0x54: /* LD D,H */
		D = H; break;
	case 0x55: /* LD D,L */
		D = L; break;
	case 0x56: /* LD D,(HL) */
		D = readb(xHL); break;
	case 0x57: /* LD D,A */
		D = A; break;

	case 0x58: /* LD E,B */
		E = B; break;
	case 0x59: /* LD E,C */
		E = C; break;
	case 0x5A: /* LD E,D */
		E = D; break;
	case 0x5C: /* LD E,H */
		E = H; break;
	case 0x5D: /* LD E,L */
		E = L; break;
	case 0x5E: /* LD E,(HL) */
		E = readb(xHL); break;
	case 0x5F: /* LD E,A */
		E = A; break;

	case 0x60: /* LD H,B */
		H = B; break;
	case 0x61: /* LD H,C */
		H = C; break;
	case 0x62: /* LD H,D */
		H = D; break;
	case 0x63: /* LD H,E */
		H = E; break;
	case 0x65: /* LD H,L */
		H = L; break;
	case 0x66: /* LD H,(HL) */
		H = readb(xHL); break;
	case 0x67: /* LD H,A */
		H = A; break;
			
	case 0x68: /* LD L,B */
		L = B; break;
	case 0x69: /* LD L,C */
		L = C; break;
	case 0x6A: /* LD L,D */
		L = D; break;
	case 0x6B: /* LD L,E */
		L = E; break;
	case 0x6C: /* LD L,H */
		L = H; break;
	case 0x6E: /* LD L,(HL) */
		L = readb(xHL); break;
	case 0x6F: /* LD L,A */
		L = A; break;
			
	case 0x70: /* LD (HL),B */
		b = B; writeb(xHL,b); break;
	case 0x71: /* LD (HL),C */
		b = C; writeb(xHL,b); break;
	case 0x72: /* LD (HL),D */
		b = D; writeb(xHL,b); break;
	case 0x73: /* LD (HL),E */
		b = E; writeb(xHL,b); break;
	case 0x74: /* LD (HL),H */
		b = H; writeb(xHL,b); break;
	case 0x75: /* LD (HL),L */
		b = L; writeb(xHL,b); break;
	case 0x77: /* LD (HL),A */
		b = A;
		writeb(xHL,b);
		break;
			
	case 0x78: /* LD A,B */
		A = B; break;
	case 0x79: /* LD A,C */
		A = C; break;
	case 0x7A: /* LD A,D */
		A = D; break;
	case 0x7B: /* LD A,E */
		A = E; break;
	case 0x7C: /* LD A,H */
		A = H; break;
	case 0x7D: /* LD A,L */
		A = L; break;
	case 0x7E: /* LD A,(HL) */
		A = readb(xHL); break;

	case 0x01: /* LD BC,imm */
		BC = readw(xPC); PC += 2; break;
	case 0x11: /* LD DE,imm */
		DE = readw(xPC); PC += 2; break;
	case 0x21: /* LD HL,imm */
		HL = readw(xPC); PC += 2; break;
	case 0x31: /* LD SP,imm */
		SP = readw(xPC); PC += 2; break;

	case 0x02: /* LD (BC),A */
		writeb(xBC, A); break;
	case 0x0A: /* LD A,(BC) */
		A = readb(xBC); break;
	case 0x12: /* LD (DE),A */
		writeb(xDE, A); break;
	case 0x1A: /* LD A,(DE) */
		A = readb(xDE); break;

	case 0x22: /* LDI (HL),A */
		writeb(xHL, A); HL++; break;
	case 0x2A: /* LDI A,(HL) */
		A = readb(xHL); HL++; break;
	case 0x32: /* LDD (HL),A */
		writeb(xHL, A); HL+=-1; break;
	case 0x3A: /* LDD A,(HL) */
		A = readb(xHL); HL+=-1; break;

	case 0x06: /* LD B,imm */
		B = FETCH; break;
	case 0x0E: /* LD C,imm */
		C = FETCH; break;
	case 0x16: /* LD D,imm */
		D = FETCH; break;
	case 0x1E: /* LD E,imm */
		E = FETCH; break;
	case 0x26: /* LD H,imm */
		H = FETCH; break;
	case 0x2E: /* LD L,imm */
		L = FETCH; break;
	case 0x36: /* LD (HL),imm */
		b = FETCH; writeb(xHL, b); break;
	case 0x3E: /* LD A,imm */
		A = FETCH; break;

	case 0x08: /* LD (imm),SP */
		writew(readw(xPC), SP); PC += 2; break;
	case 0xEA: /* LD (imm),A */
		writeb(readw(xPC), A); PC += 2; break;

	case 0xE0: /* LDH (imm),A */
		writehi(FETCH, A); break;
	case 0xE2: /* LDH (C),A */
		writehi(C, A); break;
	case 0xF0: /* LDH A,(imm) */
		A = readhi(FETCH); break;
	case 0xF2: /* LDH A,(C) (undocumented) */
		A = readhi(C); break;
			

	case 0xF8: /* LD HL,SP+imm */
		b = FETCH; LDHLSP(b); break;
	case 0xF9: /* LD SP,HL */
		SP = HL; break;
	case 0xFA: /* LD A,(imm) */
		A = readb(readw(xPC)); PC += 2; break;

		ALU_CASES(0x80, 0xC6, ADD, __ADD)
		ALU_CASES(0x88, 0xCE, ADC, __ADC)
		ALU_CASES(0x90, 0xD6, SUB, __SUB)
		ALU_CASES(0x98, 0xDE, SBC, __SBC)
		ALU_CASES(0xA0, 0xE6, AND, __AND)
		ALU_CASES(0xA8, 0xEE, XOR, __XOR)
		ALU_CASES(0xB0, 0xF6, OR, __OR)
		ALU_CASES(0xB8, 0xFE, CP, __CP)

	case 0x09: /* ADD HL,BC */
		w = BC; ADDW(w);break;
	case 0x19: /* ADD HL,DE */
		w = DE; ADDW(w);break;
	case 0x39: /* ADD HL,SP */
		w = SP; ADDW(w);break;
	case 0x29: /* ADD HL,HL */
		w = HL;
		ADDW(w);
		break;

	case 0x04: /* INC B */
		INC(B); break;
	case 0x0C: /* INC C */
		INC(C); break;
	case 0x14: /* INC D */
		INC(D); break;
	case 0x1C: /* INC E */
		INC(E); break;
	case 0x24: /* INC H */
		INC(H); break;
	case 0x2C: /* INC L */
		INC(L); break;
	case 0x34: /* INC (HL) */
		b = readb(xHL);
		INC(b);
		writeb(xHL, b);
		break;
	case 0x3C: /* INC A */
		INC(A); break;
			
	case 0x03: /* INC BC */
		INCW(BC); break;
	case 0x13: /* INC DE */
		INCW(DE); break;
	case 0x23: /* INC HL */
		INCW(HL); break;
	case 0x33: /* INC SP */
		INCW(SP); break;
			
	case 0x05: /* DEC B */
		DEC(B); break;
	case 0x0D: /* DEC C */
		DEC(C); break;
	case 0x15: /* DEC D */
		DEC(D); break;
	case 0x1D: /* DEC E */
		DEC(E); break;
	case 0x25: /* DEC H */
		DEC(H); break;
	case 0x2D: /* DEC L */
		DEC(L); break;
	case 0x35: /* DEC (HL) */
		b = readb(xHL);
		DEC(b);
		writeb(xHL, b);
		break;
	case 0x3D: /* DEC A */
		DEC(A); break;

	case 0x0B: /* DEC BC */
		DECW(BC); break;
	case 0x1B: /* DEC DE */
		DECW(DE); break;
	case 0x2B: /* DEC HL */
		DECW(HL); break;
	case 0x3B: /* DEC SP */
		DECW(SP); break;

	case 0x07: /* RLCA */
		RLCA(A); break;
	case 0x0F: /* RRCA */
		RRCA(A); break;
	case 0x17: /* RLA */
		RLA(A); break;
	case 0x1F: /* RRA */
		RRA(A); break;

	case 0x27: /* DAA */
		DAA; break;
	case 0x2F: /* CPL */
		CPL(A); break;

	case 0x18: /* JR */
		JR; break;
	case 0x20: /* JR NZ */
		if (!(F&FZ)) {JR;break;} NOJR; break;
	case 0x28: /* JR Z */
		if (F&FZ) {JR;break;} NOJR; break;
	case 0x30: /* JR NC */
		if (!(F&FC)) {JR;break;} NOJR; break;
	case 0x38: /* JR C */
		if (F&FC) {JR;break;} NOJR; break;

	case 0xC3: /* JP */
		JP; break;
	case 0xC2: /* JP NZ */
		if (!(F&FZ)) {JP;break;} NOJP; break;
	case 0xCA: /* JP Z */
		if (F&FZ) {JP;break;} NOJP; break;
	case 0xD2: /* JP NC */
		if (!(F&FC)) {JP;break;} NOJP; break;
	case 0xDA: /* JP C */
		if (F&FC) {JP;break;} NOJP; break;
	case 0xE9: /* JP HL */
		PC = HL; break;

	case 0xC9: /* RET */
		RET; break;
	case 0xC0: /* RET NZ */
		if (!(F&FZ)) {RET;break;} NORET; break;
	case 0xC8: /* RET Z */
		if (F&FZ) {RET;break;} NORET; break;
	case 0xD0: /* RET NC */
		if (!(F&FC)) {RET;break;} NORET; break;
	case 0xD8: /* RET C */
		if (F&FC) {RET;break;} NORET; break;
	case 0xD9: /* RETI */
		IME = IMA = 1; {RET;break;}

	case 0xCD: /* CALL */

		CALL; break;
	case 0xC4: /* CALL NZ */
		if (!(F&FZ)) {CALL;break;}; NOCALL; break;
	case 0xCC: /* CALL Z */
		if (F&FZ) {CALL;break;}; NOCALL; break;
	case 0xD4: /* CALL NC */
		if (!(F&FC)) {CALL;break;}; NOCALL; break;
	case 0xDC: /* CALL C */
		if (F&FC) {CALL;break;}; NOCALL; break;

	case 0xC7: /* RST 0 */
		b = 0x00; {RST(b);break;}
	case 0xCF: /* RST 8 */
		b = 0x08; {RST(b);break;}
	case 0xD7: /* RST 10 */
		b = 0x10; {RST(b);break;}
	case 0xDF: /* RST 18 */
		b = 0x18; {RST(b);break;}
	case 0xE7: /* RST 20 */
		b = 0x20; {RST(b);break;}
	case 0xEF: /* RST 28 */
		b = 0x28; {RST(b);break;}
	case 0xF7: /* RST 30 */
		b = 0x30; {RST(b);break;}
	case 0xFF: /* RST 38 */
		b = 0x38;
		RST(b); break;
			
	case 0xC1: /* POP BC */
		POP(BC); break;
	case 0xC5: /* PUSH BC */
		PUSH(BC); break;
	case 0xD1: /* POP DE */
		POP(DE); break;
	case 0xD5: /* PUSH DE */
		PUSH(DE); break;
	case 0xE1: /* POP HL */
		POP(HL); break;
	case 0xE5: /* PUSH HL */
		PUSH(HL); break;
	case 0xF1: /* POP AF */
		POP(AF); break;
	case 0xF5: /* PUSH AF */
		PUSH(AF); break;

	case 0xE8: /* ADD SP,imm */
		b = FETCH; ADDSP(b); break;

	case 0xF3: /* DI */
		DI; break;
	case 0xFB: /* EI */
		EI; break;

	case 0x37: /* SCF */
		SCF; break;
	case 0x3F: /* CCF */
		CCF; break;

	case 0x10: /* STOP */
		PC++;
		if (R_KEY1 & 1)
		{
			cpu.speed = cpu.speed ^ 1;
			R_KEY1 = (R_KEY1 & 0x7E) | (cpu.speed << 7);
			break;
		}
		/* NOTE - we do not implement dmg STOP whatsoever */
		break;
			
	case 0x76: /* HALT */
		cpu.halt = 1;
		break;

	case 0xCB: /* CB prefix */
		cbop = FETCH;
		clen = cb_cycles_table[cbop];
		switch (cbop)
		{
			CB_REG_CASES(B, 0);
			CB_REG_CASES(C, 1);
			CB_REG_CASES(D, 2);
			CB_REG_CASES(E, 3);
			CB_REG_CASES(H, 4);
			CB_REG_CASES(L, 5);
			CB_REG_CASES(A, 7);
		default:
			b = readb(xHL);
			switch(cbop)
			{
				CB_REG_CASES(b, 6);
			}
			if ((cbop & 0xC0) != 0x40) /* exclude BIT */
				writeb(xHL, b);
			break;
		}
		break;
#if 0
	default:
		gnuboy_die(
			"invalid opcode 0x%02X at address 0x%04X, rombank = %d\n",
			op, (PC-1) & 0xffff, mbc.rombank);
		break;
#endif
	}

	/* Advance time counters */
	/* FIXME: make use of cpu_timers() */
	clen <<= 1;
	cpu.div += (clen<<1);
	if (cpu.div >= 256)
	{
		R_DIV += (cpu.div >> 8);
		cpu.div &= 0xff;
	}
	if ((R_TAC & 0x04)) { timer_advance(clen); }
	clen >>= cpu.speed;
	cpu.lcdc -= clen;
	if (cpu.lcdc <= 0) lcdc_trans();
	cpu.snd += clen ;
	i -= clen;
	if (i > 0) goto next;

	return cycles-i;
}

#endif /* ASM_CPU_EMULATE */


#ifndef ASM_CPU_STEP
/* Outdated equivalent of emu.c:emu_step() probably? Doesn't seem to be used. */
int cpu_step(int max)
{
	int cnt;
	if ((cnt = cpu_idle(max))) return cnt;
	return cpu_emulate(1);
}

#endif /* ASM_CPU_STEP */

