/*
 * Binary Ninja Architecture Plugin for AFUC
 * (Adreno Firmware Micro Controller) ISA
 *
 * Supports Adreno 5xx, 6xx, and 7xx GPU firmware.
 *
 * Based on the freedreno project's AFUC tools by Rob Clark,
 * Connor Abbott, and the freedreno contributors.
 * https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/freedreno/afuc
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

/* ─── GPU Versions ─────────────────────────────────────────── */

enum AfucGpuVer {
	AFUC_A5XX = 5,
	AFUC_A6XX = 6,
	AFUC_A7XX = 7,
};

/* ─── Register Identifiers ─────────────────────────────────── */

enum AfucReg : uint32_t {
	REG_R00 = 0,
	REG_R01, REG_R02, REG_R03, REG_R04, REG_R05, REG_R06, REG_R07,
	REG_R08, REG_R09, REG_R0A, REG_R0B, REG_R0C, REG_R0D, REG_R0E, REG_R0F,
	REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15, REG_R16, REG_R17,
	REG_R18, REG_R19,
	REG_SP   = 0x1a,
	REG_LR   = 0x1b,
	REG_REM  = 0x1c,

	/* Source-only special registers (0x1d-0x1f when read) */
	REG_MEMDATA = 0x1d,
	REG_REGDATA = 0x1e,
	REG_DATA    = 0x1f,

	/* Destination-only special registers (0x1d-0x1e when written) */
	REG_ADDR    = 0x20,  /* encoding 0x1d as dst */
	REG_USRADDR = 0x21,  /* encoding 0x1e as dst */

	/* Carry flag (pseudo-register for IL) */
	REG_CARRY   = 0x22,

	AFUC_REG_COUNT
};

/* Map hardware encoding (0-0x1f) to our register enum for source operands */
static inline AfucReg afuc_src_reg(uint32_t enc)
{
	return static_cast<AfucReg>(enc); /* 0x00-0x1f maps directly */
}

/* Map hardware encoding (0-0x1f) to our register enum for dest operands */
static inline AfucReg afuc_dst_reg(uint32_t enc)
{
	if (enc == 0x1d) return REG_ADDR;
	if (enc == 0x1e) return REG_USRADDR;
	return static_cast<AfucReg>(enc);
}

/* ─── Opcodes ──────────────────────────────────────────────── */

enum AfucOp {
	AFUC_NOP,

	/* ALU ops (register-register and immediate forms) */
	AFUC_ADD,
	AFUC_ADDHI,
	AFUC_SUB,
	AFUC_SUBHI,
	AFUC_AND,
	AFUC_OR,
	AFUC_XOR,
	AFUC_NOT,
	AFUC_SHL,
	AFUC_USHR,
	AFUC_ISHR,
	AFUC_ROT,
	AFUC_MUL8,
	AFUC_MIN,
	AFUC_MAX,
	AFUC_CMP,
	AFUC_BIC,
	AFUC_MSB,
	AFUC_MOV,     /* pseudo: or $dst, $00, $src */

	/* Move immediate with shift */
	AFUC_MOVI,

	/* Bit manipulation */
	AFUC_SETBIT,   /* immediate bit set */
	AFUC_CLRBIT,   /* immediate bit clear */
	AFUC_SETBIT_R, /* register bit set/clear (a7xx) */
	AFUC_UBFX,     /* unsigned bitfield extract (a7xx) */
	AFUC_BFI,      /* bitfield insert (a7xx) */

	/* Control register access */
	AFUC_CWRITE,
	AFUC_CREAD,
	AFUC_SWRITE,
	AFUC_SREAD,

	/* Memory access */
	AFUC_STORE,
	AFUC_LOAD,

	/* Branch / control flow */
	AFUC_BRNE_IMM,  /* branch if not equal (immediate) */
	AFUC_BREQ_IMM,  /* branch if equal (immediate) */
	AFUC_BRNE_BIT,  /* branch if bit not set */
	AFUC_BREQ_BIT,  /* branch if bit set */
	AFUC_JUMP,      /* unconditional relative jump (pseudo: brne $00, b0, #off) */
	AFUC_CALL,
	AFUC_RET,
	AFUC_IRET,
	AFUC_WAITIN,
	AFUC_BL,
	AFUC_JUMPA,     /* absolute jump (a7xx) */
	AFUC_JUMPR,     /* indirect jump (a7xx) */
	AFUC_SRET,      /* return from bl (a7xx) */
	AFUC_SETSECURE,

	AFUC_INVALID,
};

/* ─── Decoded Instruction ──────────────────────────────────── */

struct AfucInsn {
	AfucOp op;

	/* Operand registers (as AfucReg enums for src, dst) */
	AfucReg dst;
	AfucReg src1;
	AfucReg src2;

	/* Raw register encoding values (for display) */
	uint32_t dst_enc;
	uint32_t src1_enc;
	uint32_t src2_enc;

	/* Immediates */
	uint32_t immed;
	uint32_t shift;     /* for MOVI */
	uint32_t bit;       /* for SETBIT/CLRBIT, branch-bit */
	uint32_t lo, hi;    /* for UBFX/BFI */

	/* Control/SQE register base (12-bit) */
	uint32_t base;

	/* Modifiers */
	bool rep;
	uint32_t xmov;     /* 0-3 */
	bool peek;
	uint32_t sds;       /* 0-3 */
	bool preincrement;

	/* Encoding type info */
	bool is_immed;      /* uses immediate operand */
	bool is_1src;       /* single-source ALU */

	/* For branches */
	int32_t branch_offset;   /* signed, in instruction words */
	uint32_t branch_target;  /* absolute, in instruction words */

	/* NOP payload */
	uint32_t nop_payload;

	/* Raw instruction word */
	uint32_t raw;
};

/* ─── Decoder ──────────────────────────────────────────────── */

bool afuc_decode(const uint8_t* data, size_t len, uint64_t addr,
                 AfucInsn& insn, AfucGpuVer gpuver = AFUC_A6XX);

/* ─── Register name helpers ────────────────────────────────── */

const char* afuc_reg_name(AfucReg reg);
const char* afuc_src_reg_name(uint32_t enc);
const char* afuc_dst_reg_name(uint32_t enc);

/* ─── Control / SQE / pipe register name resolution ───────── */

const char* afuc_ctrl_reg_name(AfucGpuVer gpuver, uint32_t offset);
const char* afuc_sqe_reg_name(uint32_t offset);
const char* afuc_pipe_reg_name(AfucGpuVer gpuver, uint32_t offset);
