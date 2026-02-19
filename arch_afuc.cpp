/*
 * Binary Ninja Architecture Plugin for AFUC
 * (Adreno Firmware Micro Controller) ISA
 *
 * Supports Qualcomm Adreno 5xx, 6xx, and 7xx GPU firmware.
 *
 * Based on the freedreno project's AFUC tools by Rob Clark,
 * Connor Abbott, and the freedreno contributors.
 * https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/freedreno/afuc
 */

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "binaryninjaapi.h"
#include "lowlevelilinstruction.h"
#include "afuc.h"

using namespace BinaryNinja;
using namespace std;

/* ─── Forward declarations ─────────────────────────────────── */

extern const char* afuc_op_name(AfucOp op);
bool afuc_get_llil(Architecture* arch, uint64_t addr, LowLevelILFunction& il,
                   const AfucInsn& insn, AfucGpuVer gpuver);

/* ─── Architecture class ───────────────────────────────────── */

class AfucArchitecture : public Architecture
{
	AfucGpuVer m_gpuver;

public:
	AfucArchitecture(const string& name, AfucGpuVer gpuver)
		: Architecture(name), m_gpuver(gpuver)
	{
	}

	/* ── Basic properties ─────────────────────────────── */

	BNEndianness GetEndianness() const override
	{
		return LittleEndian;
	}

	size_t GetAddressSize() const override
	{
		return 4;
	}

	size_t GetDefaultIntegerSize() const override
	{
		return 4;
	}

	size_t GetInstructionAlignment() const override
	{
		return 4;
	}

	size_t GetMaxInstructionLength() const override
	{
		/*
		 * Returning 8 allows us to read the delay slot instruction
		 * alongside the branch for IL lifting.
		 */
		return 8;
	}

	size_t GetOpcodeDisplayLength() const override
	{
		return 4;
	}

	/* ── Register model ───────────────────────────────── */

	string GetRegisterName(uint32_t reg) override
	{
		if (reg < AFUC_REG_COUNT)
			return afuc_reg_name(static_cast<AfucReg>(reg));
		return "";
	}

	vector<uint32_t> GetFullWidthRegisters() override
	{
		vector<uint32_t> regs;
		for (uint32_t i = 0; i < AFUC_REG_COUNT; i++)
			regs.push_back(i);
		return regs;
	}

	vector<uint32_t> GetAllRegisters() override
	{
		return GetFullWidthRegisters();
	}

	BNRegisterInfo GetRegisterInfo(uint32_t reg) override
	{
		BNRegisterInfo info;
		info.fullWidthRegister = reg;
		info.offset = 0;
		info.size = 4;
		info.extend = NoExtend;
		return info;
	}

	uint32_t GetStackPointerRegister() override
	{
		return REG_SP;
	}

	uint32_t GetLinkRegister() override
	{
		return REG_LR;
	}

	/* ── Intrinsics ──────────────────────────────────── */

	enum AfucIntrinsic {
		AFUC_INTRIN_MIN = 0,
		AFUC_INTRIN_MAX,
		AFUC_INTRIN_CMP,
		AFUC_INTRIN_MSB,
		AFUC_INTRIN_SETSECURE,
		AFUC_INTRIN_COUNT,
	};

	string GetIntrinsicName(uint32_t intrinsic) override
	{
		switch (intrinsic) {
		case AFUC_INTRIN_MIN:       return "min";
		case AFUC_INTRIN_MAX:       return "max";
		case AFUC_INTRIN_CMP:       return "cmp";
		case AFUC_INTRIN_MSB:       return "msb";
		case AFUC_INTRIN_SETSECURE: return "setsecure";
		default:                    return "";
		}
	}

	vector<uint32_t> GetAllIntrinsics() override
	{
		vector<uint32_t> result;
		for (uint32_t i = 0; i < AFUC_INTRIN_COUNT; i++)
			result.push_back(i);
		return result;
	}

	vector<NameAndType> GetIntrinsicInputs(uint32_t intrinsic) override
	{
		switch (intrinsic) {
		case AFUC_INTRIN_MIN:
		case AFUC_INTRIN_MAX:
		case AFUC_INTRIN_CMP:
			return {
				NameAndType("a", Type::IntegerType(4, false)),
				NameAndType("b", Type::IntegerType(4, false)),
			};
		case AFUC_INTRIN_MSB:
			return { NameAndType("val", Type::IntegerType(4, false)) };
		case AFUC_INTRIN_SETSECURE:
			return { NameAndType("mode", Type::IntegerType(4, false)) };
		default:
			return {};
		}
	}

	vector<Confidence<Ref<Type>>> GetIntrinsicOutputs(uint32_t intrinsic) override
	{
		switch (intrinsic) {
		case AFUC_INTRIN_MIN:
		case AFUC_INTRIN_MAX:
		case AFUC_INTRIN_CMP:
		case AFUC_INTRIN_MSB:
			return { Type::IntegerType(4, false) };
		case AFUC_INTRIN_SETSECURE:
			return {};
		default:
			return {};
		}
	}

	/* ── Instruction Info (control flow analysis) ──────── */

	bool GetInstructionInfo(const uint8_t* data, uint64_t addr,
	                        size_t maxLen, InstructionInfo& result) override
	{
		AfucInsn insn;
		if (!afuc_decode(data, maxLen, addr, insn, m_gpuver))
			return false;

		result.length = 4;

		switch (insn.op) {
		case AFUC_BRNE_IMM:
		case AFUC_BREQ_IMM:
		case AFUC_BRNE_BIT:
		case AFUC_BREQ_BIT:
		{
			/* Conditional branch with delay slot */
			uint64_t target = addr + 4 + (int64_t)insn.branch_offset * 4;
			uint64_t fallthrough = addr + 8; /* skip delay slot */
			result.AddBranch(TrueBranch, target, nullptr, true);
			result.AddBranch(FalseBranch, fallthrough, nullptr, true);
			break;
		}

		case AFUC_JUMP:
		{
			uint64_t target = addr + 4 + (int64_t)insn.branch_offset * 4;
			result.AddBranch(UnconditionalBranch, target, nullptr, true);
			break;
		}

		case AFUC_CALL:
		{
			uint64_t target = (uint64_t)insn.branch_target * 4;
			result.AddBranch(CallDestination, target, nullptr, true);
			break;
		}

		case AFUC_BL:
		{
			uint64_t target = (uint64_t)insn.branch_target * 4;
			result.AddBranch(CallDestination, target, nullptr, true);
			break;
		}

		case AFUC_JUMPA:
		{
			uint64_t target = (uint64_t)insn.branch_target * 4;
			result.AddBranch(UnconditionalBranch, target, nullptr, true);
			break;
		}

		case AFUC_JUMPR:
			result.AddBranch(UnresolvedBranch, 0, nullptr, true);
			break;

		case AFUC_RET:
		case AFUC_IRET:
		case AFUC_SRET:
			result.AddBranch(FunctionReturn, 0, nullptr, true);
			break;

		case AFUC_WAITIN:
			/*
			 * waitin waits for the next packet and jumps to its handler
			 * via the packet table. Effectively terminates the current
			 * handler, like a function return.
			 */
			result.AddBranch(FunctionReturn, 0, nullptr, true);
			break;

		default:
			break;
		}

		return true;
	}

	/* ── Instruction Text (disassembly) ────────────────── */

	bool GetInstructionText(const uint8_t* data, uint64_t addr,
	                        size_t& len, vector<InstructionTextToken>& result) override
	{
		AfucInsn insn;
		if (!afuc_decode(data, len, addr, insn, m_gpuver))
			return false;

		len = 4;

		if (insn.op == AFUC_INVALID) {
			/* Show raw data word */
			char buf[32];
			snprintf(buf, sizeof(buf), "[%08x]", insn.raw);
			result.emplace_back(TextToken, buf);
			return true;
		}

		/* ── Build prefix modifiers ─────────────────────── */
		string prefix;
		if (insn.rep)
			prefix += "(rep)";
		if (insn.sds == 1)
			prefix += "(sds1)";
		else if (insn.sds == 2)
			prefix += "(sds2)";
		else if (insn.sds == 3)
			prefix += "(sds3)";
		if (insn.xmov == 1)
			prefix += "(xmov1)";
		else if (insn.xmov == 2)
			prefix += "(xmov2)";
		else if (insn.xmov == 3)
			prefix += "(xmov3)";
		if (insn.peek)
			prefix += "(peek)";

		if (!prefix.empty())
			result.emplace_back(TextToken, prefix);

		/* ── Mnemonic ───────────────────────────────────── */
		const char* mnem = afuc_op_name(insn.op);
		result.emplace_back(InstructionToken, mnem);

		/* Padding after mnemonic */
		size_t mlen = strlen(mnem) + prefix.size();
		size_t pad = (mlen < 10) ? (10 - mlen) : 1;
		result.emplace_back(TextToken, string(pad, ' '));

		char buf[64];

		/* ── Operands by instruction type ───────────────── */
		switch (insn.op) {

		/* ── NOP ──────────────────────────────────────── */
		case AFUC_NOP:
			break;

		/* ── ALU 2-source register ────────────────────── */
		case AFUC_ADD: case AFUC_ADDHI: case AFUC_SUB: case AFUC_SUBHI:
		case AFUC_AND: case AFUC_OR: case AFUC_XOR:
		case AFUC_SHL: case AFUC_USHR: case AFUC_ISHR: case AFUC_ROT:
		case AFUC_MUL8: case AFUC_MIN: case AFUC_MAX: case AFUC_CMP:
		case AFUC_BIC: case AFUC_SETBIT_R:
		{
			if (insn.is_immed) {
				/* Immediate form */
				result.emplace_back(RegisterToken, afuc_dst_reg_name(insn.dst_enc));
				result.emplace_back(OperandSeparatorToken, ", ");
				if (!insn.is_1src) {
					result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src1_enc));
					result.emplace_back(OperandSeparatorToken, ", ");
				}
				snprintf(buf, sizeof(buf), "0x%x", insn.immed);
				result.emplace_back(IntegerToken, buf, insn.immed);
			} else {
				/* Register form */
				result.emplace_back(RegisterToken, afuc_dst_reg_name(insn.dst_enc));
				result.emplace_back(OperandSeparatorToken, ", ");
				if (!insn.is_1src) {
					result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src1_enc));
					result.emplace_back(OperandSeparatorToken, ", ");
				}
				result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src2_enc));
			}
			break;
		}

		/* ── NOT / MSB (1-source) ─────────────────────── */
		case AFUC_NOT:
		case AFUC_MSB:
		{
			result.emplace_back(RegisterToken, afuc_dst_reg_name(insn.dst_enc));
			result.emplace_back(OperandSeparatorToken, ", ");
			if (insn.is_immed) {
				snprintf(buf, sizeof(buf), "0x%x", insn.immed);
				result.emplace_back(IntegerToken, buf, insn.immed);
			} else {
				/* For 2src encoding, the source is in src2 position (bits[16:20]) */
				result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src2_enc));
			}
			break;
		}

		/* ── MOV (pseudo for OR with $00) ─────────────── */
		case AFUC_MOV:
			result.emplace_back(RegisterToken, afuc_dst_reg_name(insn.dst_enc));
			result.emplace_back(OperandSeparatorToken, ", ");
			result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src2_enc));
			break;

		/* ── MOVI (move immediate with shift) ─────────── */
		case AFUC_MOVI:
		{
			result.emplace_back(RegisterToken, afuc_dst_reg_name(insn.dst_enc));
			result.emplace_back(OperandSeparatorToken, ", ");
			snprintf(buf, sizeof(buf), "0x%x", insn.immed);
			result.emplace_back(IntegerToken, buf, insn.immed);
			if (insn.shift != 0) {
				result.emplace_back(TextToken, " << ");
				snprintf(buf, sizeof(buf), "%u", insn.shift);
				result.emplace_back(IntegerToken, buf, insn.shift);
			}
			/* Annotate pipe register when writing to $addr with high shift */
			if (insn.dst_enc == 0x1d && insn.shift >= 16) {
				uint32_t val = insn.immed << insn.shift;
				val &= ~0x40000u; /* b18 = auto-increment disable flag */
				if ((val & 0x00ffffffu) == 0) {
					const char* pname = afuc_pipe_reg_name(m_gpuver, val >> 24);
					if (pname) {
						string ann = string("  ; |") + pname;
						result.emplace_back(TextToken, ann);
					}
				}
			}
			break;
		}

		/* ── SETBIT / CLRBIT ──────────────────────────── */
		case AFUC_SETBIT:
		case AFUC_CLRBIT:
			result.emplace_back(RegisterToken, afuc_dst_reg_name(insn.dst_enc));
			result.emplace_back(OperandSeparatorToken, ", ");
			result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src1_enc));
			result.emplace_back(OperandSeparatorToken, ", ");
			snprintf(buf, sizeof(buf), "b%u", insn.bit);
			result.emplace_back(IntegerToken, buf, insn.bit);
			break;

		/* ── UBFX / BFI ───────────────────────────────── */
		case AFUC_UBFX:
		case AFUC_BFI:
			result.emplace_back(RegisterToken, afuc_dst_reg_name(insn.dst_enc));
			result.emplace_back(OperandSeparatorToken, ", ");
			result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src1_enc));
			result.emplace_back(OperandSeparatorToken, ", ");
			snprintf(buf, sizeof(buf), "b%u", insn.lo);
			result.emplace_back(IntegerToken, buf, insn.lo);
			result.emplace_back(OperandSeparatorToken, ", ");
			snprintf(buf, sizeof(buf), "b%u", insn.hi);
			result.emplace_back(IntegerToken, buf, insn.hi);
			break;

		/* ── CWRITE / SWRITE ──────────────────────────── */
		case AFUC_CWRITE:
		case AFUC_SWRITE:
		{
			result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src1_enc));
			result.emplace_back(OperandSeparatorToken, ", ");
			result.emplace_back(BeginMemoryOperandToken, "[");
			result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src2_enc));
			result.emplace_back(TextToken, " + ");
			const char* rname = nullptr;
			if (insn.op == AFUC_SWRITE)
				rname = afuc_sqe_reg_name(insn.base);
			else
				rname = afuc_ctrl_reg_name(m_gpuver, insn.base);
			if (rname) {
				string sym = (insn.op == AFUC_SWRITE) ? string("%") : string("@");
				sym += rname;
				result.emplace_back(TextToken, sym);
			} else {
				snprintf(buf, sizeof(buf), "0x%03x", insn.base);
				result.emplace_back(IntegerToken, buf, insn.base);
			}
			result.emplace_back(EndMemoryOperandToken, "]");
			if (insn.preincrement)
				result.emplace_back(TextToken, "!");
			break;
		}

		/* ── CREAD / SREAD ────────────────────────────── */
		case AFUC_CREAD:
		case AFUC_SREAD:
		{
			result.emplace_back(RegisterToken, afuc_dst_reg_name(insn.dst_enc));
			result.emplace_back(OperandSeparatorToken, ", ");
			result.emplace_back(BeginMemoryOperandToken, "[");
			result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src1_enc));
			result.emplace_back(TextToken, " + ");
			const char* rname = nullptr;
			if (insn.op == AFUC_SREAD)
				rname = afuc_sqe_reg_name(insn.base);
			else
				rname = afuc_ctrl_reg_name(m_gpuver, insn.base);
			if (rname) {
				string sym = (insn.op == AFUC_SREAD) ? string("%") : string("@");
				sym += rname;
				result.emplace_back(TextToken, sym);
			} else {
				snprintf(buf, sizeof(buf), "0x%03x", insn.base);
				result.emplace_back(IntegerToken, buf, insn.base);
			}
			result.emplace_back(EndMemoryOperandToken, "]");
			if (insn.preincrement)
				result.emplace_back(TextToken, "!");
			break;
		}

		/* ── STORE ────────────────────────────────────── */
		case AFUC_STORE:
			result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src1_enc));
			result.emplace_back(OperandSeparatorToken, ", ");
			result.emplace_back(BeginMemoryOperandToken, "[");
			result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src2_enc));
			result.emplace_back(TextToken, " + ");
			snprintf(buf, sizeof(buf), "0x%03x", insn.immed);
			result.emplace_back(IntegerToken, buf, insn.immed);
			result.emplace_back(EndMemoryOperandToken, "]");
			if (insn.preincrement)
				result.emplace_back(TextToken, "!");
			break;

		/* ── LOAD ─────────────────────────────────────── */
		case AFUC_LOAD:
			result.emplace_back(RegisterToken, afuc_dst_reg_name(insn.dst_enc));
			result.emplace_back(OperandSeparatorToken, ", ");
			result.emplace_back(BeginMemoryOperandToken, "[");
			result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src1_enc));
			result.emplace_back(TextToken, " + ");
			snprintf(buf, sizeof(buf), "0x%03x", insn.immed);
			result.emplace_back(IntegerToken, buf, insn.immed);
			result.emplace_back(EndMemoryOperandToken, "]");
			if (insn.preincrement)
				result.emplace_back(TextToken, "!");
			break;

		/* ── Conditional branches (immediate compare) ── */
		case AFUC_BRNE_IMM:
		case AFUC_BREQ_IMM:
		{
			result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src1_enc));
			result.emplace_back(OperandSeparatorToken, ", ");
			snprintf(buf, sizeof(buf), "0x%x", insn.immed);
			result.emplace_back(IntegerToken, buf, insn.immed);
			result.emplace_back(OperandSeparatorToken, ", ");
			uint64_t target = addr + 4 + (int64_t)insn.branch_offset * 4;
			snprintf(buf, sizeof(buf), "#0x%" PRIx64, target);
			result.emplace_back(PossibleAddressToken, buf, target);
			break;
		}

		/* ── Conditional branches (bit test) ───────────── */
		case AFUC_BRNE_BIT:
		case AFUC_BREQ_BIT:
		{
			result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src1_enc));
			result.emplace_back(OperandSeparatorToken, ", ");
			snprintf(buf, sizeof(buf), "b%u", insn.bit);
			result.emplace_back(IntegerToken, buf, insn.bit);
			result.emplace_back(OperandSeparatorToken, ", ");
			uint64_t target = addr + 4 + (int64_t)insn.branch_offset * 4;
			snprintf(buf, sizeof(buf), "#0x%" PRIx64, target);
			result.emplace_back(PossibleAddressToken, buf, target);
			break;
		}

		/* ── Unconditional relative jump ───────────────── */
		case AFUC_JUMP:
		{
			uint64_t target = addr + 4 + (int64_t)insn.branch_offset * 4;
			snprintf(buf, sizeof(buf), "#0x%" PRIx64, target);
			result.emplace_back(PossibleAddressToken, buf, target);
			break;
		}

		/* ── CALL / BL ─────────────────────────────────── */
		case AFUC_CALL:
		case AFUC_BL:
		{
			uint64_t target = (uint64_t)insn.branch_target * 4;
			snprintf(buf, sizeof(buf), "#0x%" PRIx64, target);
			result.emplace_back(PossibleAddressToken, buf, target);
			break;
		}

		/* ── JUMPA (absolute) ──────────────────────────── */
		case AFUC_JUMPA:
		{
			uint64_t target = (uint64_t)insn.branch_target * 4;
			snprintf(buf, sizeof(buf), "#0x%" PRIx64, target);
			result.emplace_back(PossibleAddressToken, buf, target);
			break;
		}

		/* ── JUMPR (indirect) ──────────────────────────── */
		case AFUC_JUMPR:
			result.emplace_back(RegisterToken, afuc_src_reg_name(insn.src1_enc));
			break;

		/* ── RET / IRET / SRET / WAITIN / SETSECURE ──── */
		case AFUC_RET:
		case AFUC_IRET:
		case AFUC_SRET:
		case AFUC_WAITIN:
			break;

		case AFUC_SETSECURE:
			result.emplace_back(RegisterToken, "$02");
			result.emplace_back(OperandSeparatorToken, ", ");
			{
				uint64_t target = addr + 4 + 3 * 4; /* skip next 3 instructions */
				snprintf(buf, sizeof(buf), "#0x%" PRIx64, target);
				result.emplace_back(PossibleAddressToken, buf, target);
			}
			break;

		default:
			break;
		}

		return true;
	}

	/* ── Low-Level IL ─────────────────────────────────── */

	bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr,
	                              size_t& len, LowLevelILFunction& il) override
	{
		AfucInsn insn;
		if (!afuc_decode(data, len, addr, insn, m_gpuver)) {
			il.AddInstruction(il.Undefined());
			return false;
		}

		len = 4;

		if (insn.op == AFUC_INVALID) {
			il.AddInstruction(il.Undefined());
			return false;
		}

		return afuc_get_llil(this, addr, il, insn, m_gpuver);
	}

	/* ── NOP conversion for patching ──────────────────── */

	bool ConvertToNop(uint8_t* data, uint64_t addr, size_t len) override
	{
		(void)addr;
		if (len < 4)
			return false;
		uint32_t nop = (m_gpuver >= AFUC_A6XX) ? 0x01000000 : 0x00000000;
		memcpy(data, &nop, 4);
		return true;
	}
};

/* ─── GPU version auto-detection ──────────────────────────── */

/*
 * The firmware ID is encoded in the second DWORD (offset 4) of the
 * firmware file, bits 12-23. This NOP payload identifies the GPU.
 *
 * Known firmware IDs (from freedreno afuc/util.h):
 *   0x730 = A730 (a7xx)    0x740 = A740 (a7xx)
 *   0x512 = GEN70500 (a7xx) 0x520 = A750 (a7xx)
 *   0x6ee = A630 (a6xx)    0x6dc = A650 (a6xx)   0x6dd = A660 (a6xx)
 *   0x5ff = A530 (a5xx)
 */
static AfucGpuVer afuc_detect_gpuver(uint32_t fw_id)
{
	switch (fw_id) {
	case 0x730: case 0x740: case 0x512: case 0x520:
		return AFUC_A7XX;
	case 0x6ee: case 0x6dc: case 0x6dd:
		return AFUC_A6XX;
	case 0x5ff:
		return AFUC_A5XX;
	default:
		if (fw_id >= 0x700) return AFUC_A7XX;
		if (fw_id >= 0x600) return AFUC_A6XX;
		/* 0x5xx range: 0x512/0x520 are a7xx, 0x5ff is a5xx */
		if (fw_id >= 0x500 && fw_id < 0x530) return AFUC_A7XX;
		if (fw_id >= 0x500) return AFUC_A5XX;
		return AFUC_A6XX;
	}
}

static uint32_t afuc_get_fwid(BinaryView* data)
{
	if (!data || data->GetLength() < 8)
		return 0;
	DataBuffer buf = data->ReadBuffer(4, 4);
	if (buf.GetLength() < 4)
		return 0;
	uint32_t word1;
	memcpy(&word1, buf.GetData(), 4);
	return (word1 >> 12) & 0xfff;
}

/* ─── BinaryView for AFUC firmware files ──────────────────── */

class AfucBinaryView : public BinaryView
{
	bool m_parseOnly;

public:
	AfucBinaryView(BinaryView* data, bool parseOnly = false)
		: BinaryView("AFUC", data->GetFile(), data), m_parseOnly(parseOnly)
	{
	}

	bool Init() override
	{
		try {
			auto parent = GetParentView();
			if (!parent)
				return false;

			size_t fileLen = parent->GetLength();
			if (fileLen < 8)
				return false;

			/* Detect GPU version from firmware ID */
			uint32_t fw_id = afuc_get_fwid(parent);
			AfucGpuVer gpuver = afuc_detect_gpuver(fw_id);

			const char* arch_name;
			switch (gpuver) {
			case AFUC_A5XX: arch_name = "afuc-a5xx"; break;
			case AFUC_A7XX: arch_name = "afuc-a7xx"; break;
			default:        arch_name = "afuc-a6xx"; break;
			}

			Ref<Architecture> arch = Architecture::GetByName(arch_name);
			if (!arch)
				return false;

			SetDefaultArchitecture(arch);

			Ref<Platform> plat = arch->GetStandalonePlatform();
			if (plat)
				SetDefaultPlatform(plat);

			/*
			 * Firmware layout:
			 *   file[0..3]   = header (not an instruction)
			 *   file[4..end] = instructions (loaded at SQE address 0)
			 *
			 * Map instructions at virtual address 0 so branch targets
			 * resolve correctly (branches use word addresses * 4).
			 */
			size_t codeLen = fileLen - 4;
			AddAutoSegment(0, codeLen, 4, codeLen,
				SegmentExecutable | SegmentReadable);
			AddAutoSection("code", 0, codeLen, ReadOnlyCodeSectionSemantics);

			if (m_parseOnly)
				return true;

			if (plat)
				AddEntryPointForAnalysis(plat, 0);

			LogInfo("AFUC firmware loaded: fw_id=0x%03x arch=%s size=%zu instructions",
				fw_id, arch_name, codeLen / 4);

			return true;
		} catch (...) {
			LogError("AFUC firmware view initialization failed");
			return false;
		}
	}
};

/* ─── BinaryViewType: auto-detects AFUC firmware ──────────── */

class AfucFirmwareViewType : public BinaryViewType
{
public:
	AfucFirmwareViewType() : BinaryViewType("AFUC", "AFUC Firmware")
	{
	}

	Ref<BinaryView> Create(BinaryView* data) override
	{
		try {
			return new AfucBinaryView(data);
		} catch (...) {
			return nullptr;
		}
	}

	Ref<BinaryView> Parse(BinaryView* data) override
	{
		try {
			return new AfucBinaryView(data, true);
		} catch (...) {
			return nullptr;
		}
	}

	bool IsTypeValidForData(BinaryView* data) override
	{
		try {
			if (!data || data->GetLength() < 8)
				return false;

			DataBuffer buf = data->ReadBuffer(0, 8);
			if (buf.GetLength() < 8)
				return false;

			uint32_t w0, w1;
			memcpy(&w0, buf.GetDataAt(0), 4);
			memcpy(&w1, buf.GetDataAt(4), 4);

			/*
			 * Word 1 (offset 4) is a NOP instruction carrying the firmware ID.
			 * NOP encoding: top 6 bits (26:31) must be 0.
			 */
			if ((w1 >> 26) != 0)
				return false;

			uint32_t fw_id = (w1 >> 12) & 0xfff;

			/* Only match known firmware IDs to avoid false positives */
			switch (fw_id) {
			case 0x730: case 0x740: case 0x512: case 0x520:
			case 0x6ee: case 0x6dc: case 0x6dd:
			case 0x5ff:
				return true;
			default:
				return false;
			}
		} catch (...) {
			return false;
		}
	}

	bool IsDeprecated() override { return false; }
};

/* ─── Calling Convention ──────────────────────────────────── */

class AfucCallingConvention : public CallingConvention
{
public:
	AfucCallingConvention(Architecture* arch)
		: CallingConvention(arch, "default")
	{
	}

	vector<uint32_t> GetCallerSavedRegisters() override
	{
		/* $01-$11 are temporaries (scratch across calls) */
		return {
			REG_R01, REG_R02, REG_R03, REG_R04, REG_R05,
			REG_R06, REG_R07, REG_R08, REG_R09, REG_R0A,
			REG_R0B,
		};
	}

	vector<uint32_t> GetCalleeSavedRegisters() override
	{
		/* $12-$19 are globals (preserved across calls by convention) */
		return {
			REG_R12, REG_R13, REG_R14, REG_R15, REG_R16,
			REG_R17, REG_R18, REG_R19,
		};
	}

	vector<uint32_t> GetIntegerArgumentRegisters() override
	{
		/* AFUC doesn't use register-based argument passing;
		 * PM4 packet data arrives through the $data FIFO */
		return {};
	}

	uint32_t GetIntegerReturnValueRegister() override
	{
		return REG_R01;
	}
};

/* ─── Plugin Entry Point ──────────────────────────────────── */

extern "C"
{
	BN_DECLARE_CORE_ABI_VERSION

	BINARYNINJAPLUGIN bool CorePluginInit()
	{
		auto* a5 = new AfucArchitecture("afuc-a5xx", AFUC_A5XX);
		auto* a6 = new AfucArchitecture("afuc-a6xx", AFUC_A6XX);
		auto* a7 = new AfucArchitecture("afuc-a7xx", AFUC_A7XX);

		Architecture::Register(a5);
		Architecture::Register(a6);
		Architecture::Register(a7);

		/* Register calling conventions */
		Ref<CallingConvention> cc5 = new AfucCallingConvention(a5);
		Ref<CallingConvention> cc6 = new AfucCallingConvention(a6);
		Ref<CallingConvention> cc7 = new AfucCallingConvention(a7);

		a5->RegisterCallingConvention(cc5);
		a6->RegisterCallingConvention(cc6);
		a7->RegisterCallingConvention(cc7);

		a5->SetDefaultCallingConvention(cc5);
		a6->SetDefaultCallingConvention(cc6);
		a7->SetDefaultCallingConvention(cc7);

		BinaryViewType::Register(new AfucFirmwareViewType());

		LogInfo("AFUC architecture plugin loaded (a5xx/a6xx/a7xx)");
		return true;
	}
}
