/*
 * AFUC instruction decoder.
 *
 * Based on the freedreno project's AFUC tools by Rob Clark,
 * Connor Abbott, and the freedreno contributors.
 * https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/freedreno/afuc
 */

#include "afuc.h"
#include <cstring>

/* ─── Register Names ───────────────────────────────────────── */

static const char* s_gpr_names[] = {
	"$00", "$01", "$02", "$03", "$04", "$05", "$06", "$07",
	"$08", "$09", "$0a", "$0b", "$0c", "$0d", "$0e", "$0f",
	"$10", "$11", "$12", "$13", "$14", "$15", "$16", "$17",
	"$18", "$19", "$sp", "$lr",
};

const char* afuc_src_reg_name(uint32_t enc)
{
	if (enc < 0x1c) return s_gpr_names[enc];
	switch (enc) {
	case 0x1c: return "$rem";
	case 0x1d: return "$memdata";
	case 0x1e: return "$regdata";
	case 0x1f: return "$data";
	default:   return "?";
	}
}

const char* afuc_dst_reg_name(uint32_t enc)
{
	if (enc < 0x1c) return s_gpr_names[enc];
	switch (enc) {
	case 0x1c: return "$rem";
	case 0x1d: return "$addr";
	case 0x1e: return "$usraddr";
	case 0x1f: return "$data";
	default:   return "?";
	}
}

const char* afuc_reg_name(AfucReg reg)
{
	if (reg <= REG_R19) return s_gpr_names[reg];
	switch (reg) {
	case REG_SP:      return "$sp";
	case REG_LR:      return "$lr";
	case REG_REM:     return "$rem";
	case REG_MEMDATA: return "$memdata";
	case REG_REGDATA: return "$regdata";
	case REG_DATA:    return "$data";
	case REG_ADDR:    return "$addr";
	case REG_USRADDR: return "$usraddr";
	case REG_CARRY:   return "$carry";
	default:          return "?";
	}
}

/* ─── ALU Opcode Lookup Tables ─────────────────────────────── */

struct AluEntry {
	AfucOp op;
	int nsrc; /* 1 or 2 */
};

/* 2-source register sub-opcodes (bits[0:4]) for a5xx/a6xx */
static const AluEntry s_alu2src_a6[32] = {
	/* 0x00 */ { AFUC_INVALID, 0 },
	/* 0x01 */ { AFUC_ADD, 2 },
	/* 0x02 */ { AFUC_ADDHI, 2 },
	/* 0x03 */ { AFUC_SUB, 2 },
	/* 0x04 */ { AFUC_SUBHI, 2 },
	/* 0x05 */ { AFUC_AND, 2 },
	/* 0x06 */ { AFUC_OR, 2 },
	/* 0x07 */ { AFUC_XOR, 2 },
	/* 0x08 */ { AFUC_NOT, 1 },
	/* 0x09 */ { AFUC_SHL, 2 },
	/* 0x0a */ { AFUC_USHR, 2 },
	/* 0x0b */ { AFUC_ISHR, 2 },
	/* 0x0c */ { AFUC_ROT, 2 },
	/* 0x0d */ { AFUC_MUL8, 2 },
	/* 0x0e */ { AFUC_MIN, 2 },
	/* 0x0f */ { AFUC_MAX, 2 },
	/* 0x10 */ { AFUC_CMP, 2 },
	/* 0x11 */ { AFUC_INVALID, 0 },
	/* 0x12 */ { AFUC_INVALID, 0 },
	/* 0x13 */ { AFUC_INVALID, 0 },
	/* 0x14 */ { AFUC_MSB, 1 },
	/* 0x15 */ { AFUC_INVALID, 0 },
	/* 0x16 */ { AFUC_INVALID, 0 },
	/* 0x17 */ { AFUC_INVALID, 0 },
	/* 0x18 */ { AFUC_INVALID, 0 },
	/* 0x19 */ { AFUC_INVALID, 0 },
	/* 0x1a */ { AFUC_INVALID, 0 },
	/* 0x1b */ { AFUC_INVALID, 0 },
	/* 0x1c */ { AFUC_INVALID, 0 },
	/* 0x1d */ { AFUC_INVALID, 0 },
	/* 0x1e */ { AFUC_INVALID, 0 },
	/* 0x1f */ { AFUC_INVALID, 0 },
};

/* 2-source register sub-opcodes for a7xx */
static const AluEntry s_alu2src_a7[32] = {
	/* 0x00 */ { AFUC_INVALID, 0 },
	/* 0x01 */ { AFUC_ADD, 2 },
	/* 0x02 */ { AFUC_ADDHI, 2 },
	/* 0x03 */ { AFUC_SUB, 2 },
	/* 0x04 */ { AFUC_SUBHI, 2 },
	/* 0x05 */ { AFUC_AND, 2 },
	/* 0x06 */ { AFUC_OR, 2 },
	/* 0x07 */ { AFUC_XOR, 2 },
	/* 0x08 */ { AFUC_NOT, 1 },
	/* 0x09 */ { AFUC_BIC, 2 },
	/* 0x0a */ { AFUC_MIN, 2 },
	/* 0x0b */ { AFUC_MAX, 2 },
	/* 0x0c */ { AFUC_MUL8, 2 },
	/* 0x0d */ { AFUC_CMP, 2 },
	/* 0x0e */ { AFUC_INVALID, 0 },
	/* 0x0f */ { AFUC_INVALID, 0 },
	/* 0x10 */ { AFUC_INVALID, 0 },
	/* 0x11 */ { AFUC_INVALID, 0 },
	/* 0x12 */ { AFUC_SHL, 2 },
	/* 0x13 */ { AFUC_USHR, 2 },
	/* 0x14 */ { AFUC_ISHR, 2 },
	/* 0x15 */ { AFUC_ROT, 2 },
	/* 0x16 */ { AFUC_SETBIT_R, 2 },
	/* 0x17 */ { AFUC_INVALID, 0 },
	/* 0x18 */ { AFUC_INVALID, 0 },
	/* 0x19 */ { AFUC_MSB, 1 },
	/* 0x1a */ { AFUC_INVALID, 0 },
	/* 0x1b */ { AFUC_INVALID, 0 },
	/* 0x1c */ { AFUC_INVALID, 0 },
	/* 0x1d */ { AFUC_INVALID, 0 },
	/* 0x1e */ { AFUC_INVALID, 0 },
	/* 0x1f */ { AFUC_INVALID, 0 },
};

/* ALU immediate opcodes (bits[27:31]) for a5xx/a6xx */
struct AluImmEntry {
	AfucOp op;
	bool has_src1;
};

static const AluImmEntry s_aluimm_a6[32] = {
	/* 0x00 */ { AFUC_INVALID, false },
	/* 0x01 */ { AFUC_ADD, true },
	/* 0x02 */ { AFUC_ADDHI, true },
	/* 0x03 */ { AFUC_SUB, true },
	/* 0x04 */ { AFUC_SUBHI, true },
	/* 0x05 */ { AFUC_AND, true },
	/* 0x06 */ { AFUC_OR, true },
	/* 0x07 */ { AFUC_XOR, true },
	/* 0x08 */ { AFUC_NOT, false },
	/* 0x09 */ { AFUC_SHL, true },
	/* 0x0a */ { AFUC_USHR, true },
	/* 0x0b */ { AFUC_ISHR, true },
	/* 0x0c */ { AFUC_ROT, true },
	/* 0x0d */ { AFUC_MUL8, true },
	/* 0x0e */ { AFUC_MIN, true },
	/* 0x0f */ { AFUC_MAX, true },
	/* 0x10 */ { AFUC_CMP, true },
	/* 0x11 */ { AFUC_INVALID, false },
	/* 0x12 */ { AFUC_INVALID, false },
	/* 0x13 */ { AFUC_INVALID, false },
	/* 0x14 */ { AFUC_INVALID, false },
	/* 0x15 */ { AFUC_INVALID, false },
	/* 0x16 */ { AFUC_INVALID, false },
	/* 0x17 */ { AFUC_INVALID, false },
	/* 0x18 */ { AFUC_INVALID, false },
	/* 0x19 */ { AFUC_INVALID, false },
	/* 0x1a */ { AFUC_INVALID, false },
	/* 0x1b */ { AFUC_INVALID, false },
	/* 0x1c */ { AFUC_INVALID, false },
	/* 0x1d */ { AFUC_INVALID, false },
	/* 0x1e */ { AFUC_INVALID, false },
	/* 0x1f */ { AFUC_INVALID, false },
};

static const AluImmEntry s_aluimm_a7[32] = {
	/* 0x00 */ { AFUC_INVALID, false },
	/* 0x01 */ { AFUC_ADD, true },
	/* 0x02 */ { AFUC_ADDHI, true },
	/* 0x03 */ { AFUC_SUB, true },
	/* 0x04 */ { AFUC_SUBHI, true },
	/* 0x05 */ { AFUC_AND, true },
	/* 0x06 */ { AFUC_OR, true },
	/* 0x07 */ { AFUC_XOR, true },
	/* 0x08 */ { AFUC_NOT, false },
	/* 0x09 */ { AFUC_BIC, true },
	/* 0x0a */ { AFUC_MIN, true },
	/* 0x0b */ { AFUC_MAX, true },
	/* 0x0c */ { AFUC_MUL8, true },
	/* 0x0d */ { AFUC_CMP, true },
	/* 0x0e */ { AFUC_INVALID, false },
	/* 0x0f */ { AFUC_INVALID, false },
	/* 0x10 */ { AFUC_INVALID, false },
	/* 0x11 */ { AFUC_INVALID, false },
	/* 0x12 */ { AFUC_INVALID, false },
	/* 0x13 */ { AFUC_INVALID, false },
	/* 0x14 */ { AFUC_INVALID, false },
	/* 0x15 */ { AFUC_INVALID, false },
	/* 0x16 */ { AFUC_INVALID, false },
	/* 0x17 */ { AFUC_INVALID, false },
	/* 0x18 */ { AFUC_INVALID, false },
	/* 0x19 */ { AFUC_INVALID, false },
	/* 0x1a */ { AFUC_INVALID, false },
	/* 0x1b */ { AFUC_INVALID, false },
	/* 0x1c */ { AFUC_INVALID, false },
	/* 0x1d */ { AFUC_INVALID, false },
	/* 0x1e */ { AFUC_INVALID, false },
	/* 0x1f */ { AFUC_INVALID, false },
};

/* ─── Mnemonic Names ───────────────────────────────────────── */

const char* afuc_op_name(AfucOp op)
{
	switch (op) {
	case AFUC_NOP:       return "nop";
	case AFUC_ADD:       return "add";
	case AFUC_ADDHI:     return "addhi";
	case AFUC_SUB:       return "sub";
	case AFUC_SUBHI:     return "subhi";
	case AFUC_AND:       return "and";
	case AFUC_OR:        return "or";
	case AFUC_XOR:       return "xor";
	case AFUC_NOT:       return "not";
	case AFUC_SHL:       return "shl";
	case AFUC_USHR:      return "ushr";
	case AFUC_ISHR:      return "ishr";
	case AFUC_ROT:       return "rot";
	case AFUC_MUL8:      return "mul8";
	case AFUC_MIN:       return "min";
	case AFUC_MAX:       return "max";
	case AFUC_CMP:       return "cmp";
	case AFUC_BIC:       return "bic";
	case AFUC_MSB:       return "msb";
	case AFUC_MOV:       return "mov";
	case AFUC_MOVI:      return "mov";
	case AFUC_SETBIT:    return "setbit";
	case AFUC_CLRBIT:    return "clrbit";
	case AFUC_SETBIT_R:  return "setbit";
	case AFUC_UBFX:      return "ubfx";
	case AFUC_BFI:       return "bfi";
	case AFUC_CWRITE:    return "cwrite";
	case AFUC_CREAD:     return "cread";
	case AFUC_SWRITE:    return "swrite";
	case AFUC_SREAD:     return "sread";
	case AFUC_STORE:     return "store";
	case AFUC_LOAD:      return "load";
	case AFUC_BRNE_IMM:  return "brne";
	case AFUC_BREQ_IMM:  return "breq";
	case AFUC_BRNE_BIT:  return "brne";
	case AFUC_BREQ_BIT:  return "breq";
	case AFUC_JUMP:      return "jump";
	case AFUC_CALL:      return "call";
	case AFUC_RET:       return "ret";
	case AFUC_IRET:      return "iret";
	case AFUC_WAITIN:    return "waitin";
	case AFUC_BL:        return "bl";
	case AFUC_JUMPA:     return "jumpa";
	case AFUC_JUMPR:     return "jump";
	case AFUC_SRET:      return "sret";
	case AFUC_SETSECURE: return "setsecure";
	default:             return "???";
	}
}

/* ─── Helper: sign-extend ──────────────────────────────────── */

static inline int32_t sign_extend(uint32_t val, unsigned bits)
{
	if (val & (1u << (bits - 1)))
		val |= ~((1u << bits) - 1);
	return static_cast<int32_t>(val);
}

/* ─── Main Decoder ─────────────────────────────────────────── */

bool afuc_decode(const uint8_t* data, size_t len, uint64_t addr,
                 AfucInsn& insn, AfucGpuVer gpuver)
{
	if (len < 4)
		return false;

	memset(&insn, 0, sizeof(insn));
	insn.op = AFUC_INVALID;

	uint32_t w = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
	insn.raw = w;

	uint32_t top5 = (w >> 27) & 0x1f;
	uint32_t top6 = (w >> 26) & 0x3f;

	/*
	 * Branch / control flow: bits[30:31] == 11
	 * These use all 6 bits [26:31] as the opcode (no REP flag).
	 */
	if ((top6 >> 4) == 0x3) {
		switch (top6) {
		case 0x30: /* BRNEI: 110000 */
		case 0x31: /* BREQI: 110001 */
		{
			insn.op = (top6 == 0x30) ? AFUC_BRNE_IMM : AFUC_BREQ_IMM;
			insn.src1_enc = (w >> 21) & 0x1f;
			insn.src1 = afuc_src_reg(insn.src1_enc);
			insn.immed = (w >> 16) & 0x1f;
			insn.branch_offset = sign_extend(w & 0xffff, 16);
			insn.is_immed = true;
			return true;
		}

		case 0x32: /* BRNEB: 110010 */
		case 0x33: /* BREQB: 110011 */
		{
			uint32_t src_enc = (w >> 21) & 0x1f;
			uint32_t bit_val = (w >> 16) & 0x1f;

			/* Special case: brne $00, b0, #offset → jump */
			if (top6 == 0x32 && src_enc == 0 && bit_val == 0) {
				insn.op = AFUC_JUMP;
			} else {
				insn.op = (top6 == 0x32) ? AFUC_BRNE_BIT : AFUC_BREQ_BIT;
			}
			insn.src1_enc = src_enc;
			insn.src1 = afuc_src_reg(src_enc);
			insn.bit = bit_val;
			insn.branch_offset = sign_extend(w & 0xffff, 16);
			return true;
		}

		case 0x34: /* RET/IRET: 110100 */
			insn.op = (w & (1u << 25)) ? AFUC_IRET : AFUC_RET;
			return true;

		case 0x35: /* CALL: 110101 */
			insn.op = AFUC_CALL;
			insn.branch_target = w & 0x03ffffff;
			return true;

		case 0x36: /* WAITIN: 110110 */
			insn.op = AFUC_WAITIN;
			return true;

		case 0x37: /* JUMPR / SRET (a7xx): 110111 */
		{
			uint32_t subop = (w >> 20) & 0x3f;
			if (subop == 0x37) {
				insn.op = AFUC_JUMPR;
				insn.src1_enc = w & 0x1f;
				insn.src1 = afuc_src_reg(insn.src1_enc);
			} else if (subop == 0x36) {
				insn.op = AFUC_SRET;
			} else {
				insn.op = AFUC_INVALID;
			}
			return true;
		}

		case 0x38: /* BL: 111000 */
			insn.op = AFUC_BL;
			insn.branch_target = w & 0x03ffffff;
			return true;

		case 0x39: /* JUMPA (a7xx): 111001 */
			insn.op = AFUC_JUMPA;
			insn.branch_target = w & 0x03ffffff;
			return true;

		case 0x3b: /* SETSECURE: 111011 */
			insn.op = AFUC_SETSECURE;
			return true;

		default:
			insn.op = AFUC_INVALID;
			return true;
		}
	}

	/*
	 * Non-branch instructions: bit 26 = REP, bits[27:31] = opcode group.
	 */
	insn.rep = (w >> 26) & 1;

	/* ─── NOP ──────────────────────────────────────────── */
	if (top5 == 0x00) {
		insn.op = AFUC_NOP;
		insn.nop_payload = w & 0x00ffffff;
		return true;
	}

	/* ─── ALU 2-source register ────────────────────────── */
	if (top5 == 0x13) { /* 10011 */
		uint32_t sub_opc = w & 0x1f;
		const AluEntry* table = (gpuver >= AFUC_A7XX) ? s_alu2src_a7 : s_alu2src_a6;
		const AluEntry& e = table[sub_opc];

		if (e.op == AFUC_INVALID && sub_opc != 0)
			return true; /* unknown sub-opcode */

		insn.peek = (w >> 8) & 1;
		insn.xmov = (w >> 9) & 0x3;
		insn.dst_enc = (w >> 11) & 0x1f;
		insn.src2_enc = (w >> 16) & 0x1f;
		insn.src1_enc = (w >> 21) & 0x1f;

		insn.dst = afuc_dst_reg(insn.dst_enc);
		insn.src1 = afuc_src_reg(insn.src1_enc);
		insn.src2 = afuc_src_reg(insn.src2_enc);

		/* Special: OR with src1=0 → MOV */
		if (e.op == AFUC_OR && insn.src1_enc == 0) {
			insn.op = AFUC_MOV;
			insn.is_1src = true;
		} else {
			insn.op = e.op;
			insn.is_1src = (e.nsrc == 1);
		}
		return true;
	}

	/* ─── SETBIT / CLRBIT / UBFX / BFI / a7xx shift-imm ─ */
	if (top5 == 0x12) { /* 10010 */
		insn.src1_enc = (w >> 21) & 0x1f;
		insn.dst_enc = (w >> 16) & 0x1f;
		insn.src1 = afuc_src_reg(insn.src1_enc);
		insn.dst = afuc_dst_reg(insn.dst_enc);

		if (gpuver >= AFUC_A7XX) {
			uint32_t sel = (w >> 12) & 0xf;
			switch (sel) {
			case 0x2: /* shl */
			case 0x3: /* ushr */
			case 0x4: /* ishr */
			case 0x5: /* rot */
			{
				static const AfucOp shift_ops[] = {
					AFUC_INVALID, AFUC_INVALID,
					AFUC_SHL, AFUC_USHR, AFUC_ISHR, AFUC_ROT
				};
				insn.op = shift_ops[sel];
				insn.immed = w & 0xfff;
				insn.is_immed = true;
				return true;
			}
			case 0x6: /* setbit / clrbit */
				insn.bit = (w >> 1) & 0x1f;
				insn.op = (w & 1) ? AFUC_SETBIT : AFUC_CLRBIT;
				return true;
			case 0x7: /* ubfx */
				insn.op = AFUC_UBFX;
				insn.lo = w & 0x1f;
				insn.hi = (w >> 5) & 0x1f;
				return true;
			case 0x8: /* bfi */
				insn.op = AFUC_BFI;
				insn.lo = w & 0x1f;
				insn.hi = (w >> 5) & 0x1f;
				return true;
			default:
				/* Fall through to a6xx-style setbit/clrbit */
				break;
			}
		}

		/* a5xx/a6xx SETBIT/CLRBIT (or a7xx with unrecognized selector) */
		insn.bit = (w >> 1) & 0x1f;
		insn.op = (w & 1) ? AFUC_SETBIT : AFUC_CLRBIT;
		return true;
	}

	/* ─── MOVI ─────────────────────────────────────────── */
	if ((top5 == 0x11 && gpuver <= AFUC_A6XX) ||
	    (top5 == 0x0e && gpuver >= AFUC_A7XX)) {
		insn.op = AFUC_MOVI;
		insn.immed = w & 0xffff;
		insn.dst_enc = (w >> 16) & 0x1f;
		insn.shift = (w >> 21) & 0x1f;
		insn.dst = afuc_dst_reg(insn.dst_enc);
		insn.is_immed = true;
		return true;
	}

	/* ─── STORE (a6xx+: 10100) ─────────────────────────── */
	if (top5 == 0x14) {
		insn.op = AFUC_STORE;
		insn.immed = w & 0xfff;
		insn.preincrement = (w >> 14) & 1;
		insn.src1_enc = (w >> 16) & 0x1f;
		insn.src2_enc = (w >> 21) & 0x1f;
		insn.src1 = afuc_src_reg(insn.src1_enc);
		insn.src2 = afuc_src_reg(insn.src2_enc);
		return true;
	}

	/* ─── CWRITE / SWRITE (10101) ──────────────────────── */
	if (top5 == 0x15) {
		uint32_t bit15 = (w >> 15) & 1;
		insn.base = w & 0xfff;
		insn.sds = (w >> 12) & 0x3;
		insn.preincrement = (w >> 14) & 1;
		insn.src1_enc = (w >> 16) & 0x1f;
		insn.src2_enc = (w >> 21) & 0x1f;
		insn.src1 = afuc_src_reg(insn.src1_enc);
		insn.src2 = afuc_src_reg(insn.src2_enc);

		if (gpuver >= AFUC_A6XX) {
			if (bit15 && insn.sds == 0) {
				insn.op = AFUC_SWRITE;
			} else {
				insn.op = AFUC_CWRITE;
			}
		} else {
			insn.op = AFUC_CWRITE;
		}
		return true;
	}

	/* ─── LOAD (a6xx: 10110) / CREAD (a5xx: 10110) ───── */
	if (top5 == 0x16) {
		insn.preincrement = (w >> 14) & 1;

		if (gpuver >= AFUC_A6XX) {
			uint32_t bit15 = (w >> 15) & 1;
			if (bit15 == 0) {
				insn.op = AFUC_LOAD;
				insn.immed = w & 0xfff;
				insn.dst_enc = (w >> 16) & 0x1f;
				insn.src1_enc = (w >> 21) & 0x1f;
				insn.dst = afuc_dst_reg(insn.dst_enc);
				insn.src1 = afuc_src_reg(insn.src1_enc);
			} else {
				/* shouldn't happen on a6xx */
				insn.op = AFUC_INVALID;
			}
		} else {
			/* a5xx CREAD */
			insn.op = AFUC_CREAD;
			insn.base = w & 0xfff;
			insn.dst_enc = (w >> 16) & 0x1f;
			insn.src1_enc = (w >> 21) & 0x1f;
			insn.dst = afuc_dst_reg(insn.dst_enc);
			insn.src1 = afuc_src_reg(insn.src1_enc);
		}
		return true;
	}

	/* ─── CREAD / SREAD (a6xx+: 10111) ─────────────────── */
	if (top5 == 0x17) {
		uint32_t bit15 = (w >> 15) & 1;
		insn.base = w & 0xfff;
		insn.preincrement = (w >> 14) & 1;
		insn.dst_enc = (w >> 16) & 0x1f;
		insn.src1_enc = (w >> 21) & 0x1f;
		insn.dst = afuc_dst_reg(insn.dst_enc);
		insn.src1 = afuc_src_reg(insn.src1_enc);

		insn.op = bit15 ? AFUC_SREAD : AFUC_CREAD;
		return true;
	}

	/* ─── ALU with 16-bit immediate ────────────────────── */
	{
		const AluImmEntry* table = (gpuver >= AFUC_A7XX) ? s_aluimm_a7 : s_aluimm_a6;
		if (top5 >= 1 && top5 <= 0x10 && table[top5].op != AFUC_INVALID) {
			const AluImmEntry& e = table[top5];
			insn.op = e.op;
			insn.immed = w & 0xffff;
			insn.dst_enc = (w >> 16) & 0x1f;
			insn.src1_enc = (w >> 21) & 0x1f;
			insn.dst = afuc_dst_reg(insn.dst_enc);
			insn.src1 = afuc_src_reg(insn.src1_enc);
			insn.is_immed = true;
			insn.is_1src = !e.has_src1;
			return true;
		}
	}

	/* Unknown instruction — treat as data */
	insn.op = AFUC_INVALID;
	return true;
}
