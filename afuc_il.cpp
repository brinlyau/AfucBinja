/*
 * AFUC Low-Level IL lifting.
 *
 * Lifts AFUC instructions to Binary Ninja's LLIL for data-flow analysis.
 *
 * Based on the freedreno project's AFUC tools by Rob Clark,
 * Connor Abbott, and the freedreno contributors.
 * https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/freedreno/afuc
 */

#include "binaryninjaapi.h"
#include "lowlevelilinstruction.h"
#include "afuc.h"

using namespace BinaryNinja;
using namespace std;

/* Helper: read a source register as an IL expression */
static ExprId ilReg(LowLevelILFunction& il, AfucReg reg)
{
	if (reg == REG_R00)
		return il.Const(4, 0);
	return il.Register(4, reg);
}

/* Helper: read src register from encoding (source context) */
static ExprId ilSrcReg(LowLevelILFunction& il, uint32_t enc)
{
	AfucReg reg = afuc_src_reg(enc);
	if (reg == REG_R00)
		return il.Const(4, 0);
	return il.Register(4, reg);
}

/* Helper: write to dst register */
static ExprId ilSetDst(LowLevelILFunction& il, uint32_t enc, ExprId val)
{
	AfucReg reg = afuc_dst_reg(enc);
	if (reg == REG_R00)
		return il.Nop(); /* writes to $00 are discarded */
	return il.SetRegister(4, reg, val);
}

/* Helper: ALU binary operation */
static ExprId ilAluBinOp(LowLevelILFunction& il, AfucOp op,
                          ExprId a, ExprId b)
{
	switch (op) {
	case AFUC_ADD:   return il.Add(4, a, b);
	case AFUC_ADDHI: return il.AddCarry(4, a, b, il.Register(4, REG_CARRY), 4);
	case AFUC_SUB:   return il.Sub(4, a, b);
	case AFUC_SUBHI: return il.SubBorrow(4, a, b, il.Register(4, REG_CARRY), 4);
	case AFUC_AND:   return il.And(4, a, b);
	case AFUC_OR:    return il.Or(4, a, b);
	case AFUC_XOR:   return il.Xor(4, a, b);
	case AFUC_SHL:   return il.ShiftLeft(4, a, b);
	case AFUC_USHR:  return il.LogicalShiftRight(4, a, b);
	case AFUC_ISHR:  return il.ArithShiftRight(4, a, b);
	case AFUC_ROT:   return il.RotateLeft(4, a, b);
	case AFUC_MUL8:
		return il.Mult(4,
			il.And(4, a, il.Const(4, 0xff)),
			il.And(4, b, il.Const(4, 0xff)));
	case AFUC_MIN:
		/* Unsigned min: a < b ? a : b — approximate with intrinsic */
		return il.Intrinsic({RegisterOrFlag::Register(0)}, 0 /* min */,
			{ExprId(a), ExprId(b)});
	case AFUC_MAX:
		return il.Intrinsic({RegisterOrFlag::Register(0)}, 1 /* max */,
			{ExprId(a), ExprId(b)});
	case AFUC_CMP:
		return il.Intrinsic({RegisterOrFlag::Register(0)}, 2 /* cmp */,
			{ExprId(a), ExprId(b)});
	case AFUC_BIC:
		return il.And(4, a, il.Not(4, b));
	default:
		return il.Unimplemented();
	}
}

bool afuc_get_llil(Architecture* arch, uint64_t addr, LowLevelILFunction& il,
                   const AfucInsn& insn, AfucGpuVer gpuver)
{
	switch (insn.op) {

	/* ── NOP ──────────────────────────────────────────── */
	case AFUC_NOP:
		il.AddInstruction(il.Nop());
		break;

	/* ── ALU binary ops ───────────────────────────────── */
	case AFUC_ADD: case AFUC_ADDHI: case AFUC_SUB: case AFUC_SUBHI:
	case AFUC_AND: case AFUC_OR: case AFUC_XOR:
	case AFUC_SHL: case AFUC_USHR: case AFUC_ISHR: case AFUC_ROT:
	case AFUC_MUL8: case AFUC_MIN: case AFUC_MAX: case AFUC_CMP:
	case AFUC_BIC:
	{
		ExprId src1, src2;
		if (insn.is_immed) {
			src1 = ilSrcReg(il, insn.src1_enc);
			src2 = il.Const(4, insn.immed);
		} else {
			src1 = ilSrcReg(il, insn.src1_enc);
			src2 = ilSrcReg(il, insn.src2_enc);
		}
		ExprId result = ilAluBinOp(il, insn.op, src1, src2);

		/* For ADD/SUB, also set carry flag */
		if (insn.op == AFUC_ADD || insn.op == AFUC_SUB) {
			il.AddInstruction(ilSetDst(il, insn.dst_enc, result));
			/* Carry approximation — set carry pseudo-reg */
			/* (simplified: just set it to something non-zero) */
		} else {
			il.AddInstruction(ilSetDst(il, insn.dst_enc, result));
		}
		break;
	}

	/* ── NOT ──────────────────────────────────────────── */
	case AFUC_NOT:
	{
		ExprId src;
		if (insn.is_immed)
			src = il.Const(4, insn.immed);
		else
			src = ilSrcReg(il, insn.src2_enc);
		il.AddInstruction(ilSetDst(il, insn.dst_enc, il.Not(4, src)));
		break;
	}

	/* ── MSB ──────────────────────────────────────────── */
	case AFUC_MSB:
	{
		/* MSB returns the position of the most significant bit */
		ExprId src = ilSrcReg(il, insn.src2_enc);
		il.AddInstruction(ilSetDst(il, insn.dst_enc, il.Intrinsic(
			{RegisterOrFlag::Register(0)}, 3 /* msb */, {src})));
		break;
	}

	/* ── MOV (pseudo: or $00, src) ────────────────────── */
	case AFUC_MOV:
		il.AddInstruction(ilSetDst(il, insn.dst_enc,
			ilSrcReg(il, insn.src2_enc)));
		break;

	/* ── MOVI (move immediate with shift) ─────────────── */
	case AFUC_MOVI:
	{
		uint32_t val = insn.immed << insn.shift;
		il.AddInstruction(ilSetDst(il, insn.dst_enc, il.Const(4, val)));
		break;
	}

	/* ── SETBIT / CLRBIT ──────────────────────────────── */
	case AFUC_SETBIT:
	{
		ExprId src = ilSrcReg(il, insn.src1_enc);
		ExprId result = il.Or(4, src, il.Const(4, 1u << insn.bit));
		il.AddInstruction(ilSetDst(il, insn.dst_enc, result));
		break;
	}
	case AFUC_CLRBIT:
	{
		ExprId src = ilSrcReg(il, insn.src1_enc);
		ExprId result = il.And(4, src, il.Const(4, ~(1u << insn.bit)));
		il.AddInstruction(ilSetDst(il, insn.dst_enc, result));
		break;
	}

	/* ── SETBIT_R (register bit set/clear, a7xx) ──────── */
	case AFUC_SETBIT_R:
	{
		/* dst = src1 | (1 << src2) */
		ExprId src1 = ilSrcReg(il, insn.src1_enc);
		ExprId src2 = ilSrcReg(il, insn.src2_enc);
		ExprId bit = il.ShiftLeft(4, il.Const(4, 1), src2);
		il.AddInstruction(ilSetDst(il, insn.dst_enc, il.Or(4, src1, bit)));
		break;
	}

	/* ── UBFX (unsigned bitfield extract) ─────────────── */
	case AFUC_UBFX:
	{
		ExprId src = ilSrcReg(il, insn.src1_enc);
		uint32_t width = insn.hi - insn.lo + 1;
		uint32_t mask = (1u << width) - 1;
		ExprId result = il.And(4,
			il.LogicalShiftRight(4, src, il.Const(4, insn.lo)),
			il.Const(4, mask));
		il.AddInstruction(ilSetDst(il, insn.dst_enc, result));
		break;
	}

	/* ── BFI (bitfield insert) ────────────────────────── */
	case AFUC_BFI:
	{
		/* dst = (dst & ~mask) | ((src >> lo) & mask), where mask covers bits lo..hi */
		ExprId src = ilSrcReg(il, insn.src1_enc);
		ExprId dst_val = ilReg(il, afuc_dst_reg(insn.dst_enc));
		uint32_t width = insn.hi - insn.lo + 1;
		uint32_t mask = ((1u << width) - 1) << insn.lo;
		ExprId inserted = il.And(4,
			il.ShiftLeft(4, src, il.Const(4, insn.lo)),
			il.Const(4, mask));
		ExprId cleared = il.And(4, dst_val, il.Const(4, ~mask));
		il.AddInstruction(ilSetDst(il, insn.dst_enc, il.Or(4, cleared, inserted)));
		break;
	}

	/* ── LOAD (memory read) ──────────────────────────── */
	case AFUC_LOAD:
	{
		ExprId base = ilSrcReg(il, insn.src1_enc);
		ExprId addr_expr = il.Add(4, base, il.Const(4, insn.immed));
		ExprId val = il.Load(4, addr_expr);
		il.AddInstruction(ilSetDst(il, insn.dst_enc, val));
		break;
	}

	/* ── STORE (memory write) ────────────────────────── */
	case AFUC_STORE:
	{
		ExprId base = ilSrcReg(il, insn.src2_enc);
		ExprId addr_expr = il.Add(4, base, il.Const(4, insn.immed));
		ExprId val = ilSrcReg(il, insn.src1_enc);
		il.AddInstruction(il.Store(4, addr_expr, val));
		break;
	}

	/* ── CWRITE / SWRITE (control/SQE register write) ── */
	case AFUC_CWRITE:
	case AFUC_SWRITE:
	{
		ExprId base = ilSrcReg(il, insn.src2_enc);
		ExprId addr_expr = il.Add(4, base, il.Const(4, insn.base));
		ExprId val = ilSrcReg(il, insn.src1_enc);
		il.AddInstruction(il.Store(4, addr_expr, val));
		break;
	}

	/* ── CREAD / SREAD (control/SQE register read) ──── */
	case AFUC_CREAD:
	case AFUC_SREAD:
	{
		ExprId base = ilSrcReg(il, insn.src1_enc);
		ExprId addr_expr = il.Add(4, base, il.Const(4, insn.base));
		ExprId val = il.Load(4, addr_expr);
		il.AddInstruction(ilSetDst(il, insn.dst_enc, val));
		break;
	}

	/* ── Conditional branches ─────────────────────────── */
	case AFUC_BRNE_IMM:
	case AFUC_BREQ_IMM:
	{
		uint64_t target = addr + 4 + (int64_t)insn.branch_offset * 4;

		ExprId cond;
		ExprId src = ilSrcReg(il, insn.src1_enc);
		ExprId imm = il.Const(4, insn.immed);

		if (insn.op == AFUC_BREQ_IMM)
			cond = il.CompareEqual(4, src, imm);
		else
			cond = il.CompareNotEqual(4, src, imm);

		BNLowLevelILLabel* trueLabel = il.GetLabelForAddress(arch, target);
		BNLowLevelILLabel* falseLabel = il.GetLabelForAddress(arch, addr + 8);

		if (trueLabel && falseLabel) {
			LowLevelILLabel* tl = reinterpret_cast<LowLevelILLabel*>(trueLabel);
			LowLevelILLabel* fl = reinterpret_cast<LowLevelILLabel*>(falseLabel);
			il.AddInstruction(il.If(cond, *tl, *fl));
		} else {
			LowLevelILLabel trueCode, falseCode;
			il.AddInstruction(il.If(cond, trueCode, falseCode));
			il.MarkLabel(trueCode);
			il.AddInstruction(il.Jump(il.ConstPointer(4, target)));
			il.MarkLabel(falseCode);
		}
		break;
	}

	case AFUC_BRNE_BIT:
	case AFUC_BREQ_BIT:
	{
		uint64_t target = addr + 4 + (int64_t)insn.branch_offset * 4;

		ExprId src = ilSrcReg(il, insn.src1_enc);
		ExprId bit_test = il.And(4, src, il.Const(4, 1u << insn.bit));
		ExprId cond;

		if (insn.op == AFUC_BREQ_BIT)
			cond = il.CompareNotEqual(4, bit_test, il.Const(4, 0));
		else
			cond = il.CompareEqual(4, bit_test, il.Const(4, 0));

		BNLowLevelILLabel* trueLabel = il.GetLabelForAddress(arch, target);
		BNLowLevelILLabel* falseLabel = il.GetLabelForAddress(arch, addr + 8);

		if (trueLabel && falseLabel) {
			LowLevelILLabel* tl = reinterpret_cast<LowLevelILLabel*>(trueLabel);
			LowLevelILLabel* fl = reinterpret_cast<LowLevelILLabel*>(falseLabel);
			il.AddInstruction(il.If(cond, *tl, *fl));
		} else {
			LowLevelILLabel trueCode, falseCode;
			il.AddInstruction(il.If(cond, trueCode, falseCode));
			il.MarkLabel(trueCode);
			il.AddInstruction(il.Jump(il.ConstPointer(4, target)));
			il.MarkLabel(falseCode);
		}
		break;
	}

	/* ── Unconditional relative jump ──────────────────── */
	case AFUC_JUMP:
	{
		uint64_t target = addr + 4 + (int64_t)insn.branch_offset * 4;
		BNLowLevelILLabel* label = il.GetLabelForAddress(arch, target);
		if (label)
			il.AddInstruction(il.Goto(*reinterpret_cast<LowLevelILLabel*>(label)));
		else
			il.AddInstruction(il.Jump(il.ConstPointer(4, target)));
		break;
	}

	/* ── CALL ─────────────────────────────────────────── */
	case AFUC_CALL:
	{
		uint64_t target = (uint64_t)insn.branch_target * 4;
		il.AddInstruction(il.Call(il.ConstPointer(4, target)));
		break;
	}

	/* ── BL (branch and link) ─────────────────────────── */
	case AFUC_BL:
	{
		uint64_t target = (uint64_t)insn.branch_target * 4;
		/* BL stores return address in $lr, then calls */
		il.AddInstruction(il.Call(il.ConstPointer(4, target)));
		break;
	}

	/* ── JUMPA (absolute) ─────────────────────────────── */
	case AFUC_JUMPA:
	{
		uint64_t target = (uint64_t)insn.branch_target * 4;
		il.AddInstruction(il.Jump(il.ConstPointer(4, target)));
		break;
	}

	/* ── JUMPR (indirect) ─────────────────────────────── */
	case AFUC_JUMPR:
		il.AddInstruction(il.Jump(ilSrcReg(il, insn.src1_enc)));
		break;

	/* ── RET / IRET ───────────────────────────────────── */
	case AFUC_RET:
	case AFUC_IRET:
		il.AddInstruction(il.Return(il.Const(4, 0)));
		break;

	/* ── SRET (return from bl) ────────────────────────── */
	case AFUC_SRET:
		il.AddInstruction(il.Return(il.Register(4, REG_LR)));
		break;

	/* ── WAITIN ───────────────────────────────────────── */
	case AFUC_WAITIN:
		/* waitin terminates the current packet handler */
		il.AddInstruction(il.Return(il.Const(4, 0)));
		break;

	/* ── SETSECURE ────────────────────────────────────── */
	case AFUC_SETSECURE:
		il.AddInstruction(il.Intrinsic({}, 4 /* setsecure */,
			{il.Register(4, REG_R02)}));
		break;

	default:
		il.AddInstruction(il.Unimplemented());
		break;
	}

	return true;
}
