/*
 * AFUC control / SQE / pipe register name tables.
 *
 * Register definitions derived from the freedreno project's XML register
 * database by Rob Clark, Connor Abbott, and the freedreno contributors.
 * https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/freedreno/registers
 */

#include "afuc.h"
#include <cstddef>

/* ─── Lookup table entry ──────────────────────────────────── */

struct RegEntry {
	uint32_t offset;
	const char *name;
};

/* ─── A5XX Control Registers ──────────────────────────────── */

static const RegEntry s_a5xx_ctrl[] = {
	{ 0x010, "REG_WRITE_ADDR" },
	{ 0x011, "REG_WRITE" },
	{ 0x038, "STORE_HI" },
	{ 0x0b0, "IB1_BASE" },
	{ 0x0b2, "IB1_DWORDS" },
	{ 0x0b4, "IB2_BASE" },
	{ 0x0b6, "IB2_DWORDS" },
	{ 0x0b8, "MEM_READ_ADDR" },
	{ 0x0ba, "MEM_READ_DWORDS" },
};

/* ─── A6XX Control Registers ──────────────────────────────── */

static const RegEntry s_a6xx_ctrl[] = {
	{ 0x001, "RB_RPTR" },
	{ 0x010, "IB1_BASE" },
	{ 0x012, "IB1_DWORDS" },
	{ 0x014, "IB2_BASE" },
	{ 0x016, "IB2_DWORDS" },
	{ 0x018, "MEM_READ_ADDR" },
	{ 0x01a, "MEM_READ_DWORDS" },
	{ 0x024, "REG_WRITE_ADDR" },
	{ 0x025, "REG_WRITE" },
	{ 0x026, "REG_READ_DWORDS" },
	{ 0x027, "REG_READ_ADDR" },
	{ 0x030, "WFI_PEND_INCR" },
	{ 0x031, "QUERY_PEND_INCR" },
	{ 0x032, "CACHE_FLUSH_PEND_INCR" },
	{ 0x038, "WFI_PEND_CTR" },
	{ 0x039, "QUERY_PEND_CTR" },
	{ 0x03a, "CACHE_FLUSH_PEND_CTR" },
	{ 0x041, "DRAW_STATE_SEL" },
	{ 0x042, "SDS_BASE" },
	{ 0x044, "SDS_DWORDS" },
	{ 0x045, "DRAW_STATE_BASE" },
	{ 0x047, "DRAW_STATE_HDR" },
	{ 0x049, "DRAW_STATE_ACTIVE_BITMASK" },
	{ 0x04a, "DRAW_STATE_SET_HDR" },
	{ 0x04c, "DRAW_STATE_SET_HDR_LPAC" },
	{ 0x04d, "DRAW_STATE_SET_PENDING" },
	{ 0x04f, "DRAW_STATE_SET_BASE_LPAC" },
	{ 0x054, "IB_LEVEL" },
	{ 0x058, "LOAD_STORE_HI" },
	{ 0x05b, "REG_READ_TEST_RESULT" },
	{ 0x05d, "PERFCNTR_CNTL" },
	{ 0x060, "PACKET_TABLE_WRITE_ADDR" },
	{ 0x061, "PACKET_TABLE_WRITE" },
	{ 0x062, "ZAP_SHADER_ADDR" },
	{ 0x06e, "PREEMPTION_TIMER" },
	{ 0x06f, "PREEMPTION_TIMER_CNTL" },
	{ 0x070, "CONTEXT_SWITCH_CNTL" },
	{ 0x071, "PREEMPT_ENABLE" },
	{ 0x072, "PREEMPT_TRIGGER" },
	{ 0x075, "SECURE_MODE" },
	{ 0x078, "PREEMPT_COOKIE" },
	{ 0x098, "MARKER" },
	{ 0x110, "SAVE_REGISTER_SMMU_INFO" },
	{ 0x112, "SAVE_REGISTER_PRIV_NON_SECURE" },
	{ 0x114, "SAVE_REGISTER_PRIV_SECURE" },
	{ 0x116, "SAVE_REGISTER_NON_PRIV" },
	{ 0x118, "SAVE_REGISTER_COUNTER" },
	{ 0x126, "PREEMPTION_INFO" },
	{ 0x12a, "MARKER_TEMP" },
	{ 0x12b, "MODE_BITMASK" },
	{ 0x170, "SCRATCH_REG0" },
	{ 0x171, "SCRATCH_REG1" },
	{ 0x172, "SCRATCH_REG2" },
	{ 0x173, "SCRATCH_REG3" },
	{ 0x174, "SCRATCH_REG4" },
	{ 0x175, "SCRATCH_REG5" },
	{ 0x176, "SCRATCH_REG6" },
	{ 0x177, "SCRATCH_REG7" },
	{ 0x200, "THREAD_SYNC" },
};

/* ─── A7XX Control Registers ──────────────────────────────── */

static const RegEntry s_a7xx_ctrl[] = {
	{ 0x001, "RB_RPTR" },
	{ 0x004, "PREEMPT_INSTR" },
	{ 0x010, "IB1_BASE" },
	{ 0x012, "IB1_DWORDS" },
	{ 0x014, "IB2_BASE" },
	{ 0x016, "IB2_DWORDS" },
	{ 0x018, "IB3_BASE" },
	{ 0x01a, "IB3_DWORDS" },
	{ 0x01c, "MEM_READ_ADDR" },
	{ 0x01e, "MEM_READ_DWORDS" },
	{ 0x030, "WFI_PEND_INCR" },
	{ 0x031, "QUERY_PEND_INCR" },
	{ 0x032, "CACHE_CLEAN_PEND_INCR" },
	{ 0x036, "REG_WRITE_ADDR" },
	{ 0x037, "REG_WRITE" },
	{ 0x038, "REG_READ_DWORDS" },
	{ 0x039, "REG_READ_ADDR" },
	{ 0x03a, "CACHE_CLEAN_PEND_CTR" },
	{ 0x03e, "WFI_PEND_CTR" },
	{ 0x03f, "QUERY_PEND_CTR" },
	{ 0x041, "DRAW_STATE_SEL" },
	{ 0x042, "SDS_BASE" },
	{ 0x044, "SDS_DWORDS" },
	{ 0x045, "DRAW_STATE_BASE" },
	{ 0x047, "DRAW_STATE_HDR" },
	{ 0x049, "DRAW_STATE_ACTIVE_BITMASK" },
	{ 0x04b, "MODE_BITMASK" },
	{ 0x04c, "DRAW_STATE_SET_HDR" },
	{ 0x04d, "DRAW_STATE_SET_PENDING" },
	{ 0x04f, "DRAW_STATE_SET_BASE" },
	{ 0x054, "IB_LEVEL" },
	{ 0x058, "LOAD_STORE_HI" },
	{ 0x05b, "REG_READ_TEST_RESULT" },
	{ 0x05d, "PERFCNTR_CNTL" },
	{ 0x060, "PACKET_TABLE_WRITE_ADDR" },
	{ 0x061, "PACKET_TABLE_WRITE" },
	{ 0x06e, "PREEMPTION_TIMER" },
	{ 0x06f, "PREEMPTION_TIMER_CNTL" },
	{ 0x070, "CONTEXT_SWITCH_CNTL" },
	{ 0x071, "PREEMPT_ENABLE" },
	{ 0x072, "PREEMPT_TRIGGER" },
	{ 0x075, "SECURE_MODE" },
	{ 0x078, "PREEMPT_COOKIE" },
	{ 0x098, "MARKER" },
	{ 0x0a0, "LOAD_STORE_RANGE_MIN" },
	{ 0x0a1, "LOAD_STORE_RANGE_LEN" },
	{ 0x0b1, "COPROCESSOR_LOCK" },
	{ 0x0d4, "APERTURE_CNTL" },
	{ 0x0d5, "APERTURE_CNTL_PREEMPT" },
	{ 0x0d6, "BV_INSTR_BASE" },
	{ 0x0d8, "BV_CNTL" },
	{ 0x0d9, "LPAC_INSTR_BASE" },
	{ 0x0db, "LPAC_CNTL" },
	{ 0x0e2, "GLOBAL_TIMESTAMP" },
	{ 0x0e3, "LOCAL_TIMESTAMP" },
	{ 0x23f, "THREAD_SYNC" },
};

/* ─── SQE Registers (shared across a6xx/a7xx) ────────────── */

static const RegEntry s_sqe_regs[] = {
	{ 0x04, "PREEMPT_INSTR" },
	{ 0x05, "SP" },
	{ 0x08, "STACK0" },
	{ 0x09, "STACK1" },
	{ 0x0a, "STACK2" },
	{ 0x0b, "STACK3" },
	{ 0x0c, "STACK4" },
	{ 0x0d, "STACK5" },
	{ 0x0e, "STACK6" },
	{ 0x0f, "STACK7" },
};

/* ─── A6XX Pipe Registers ─────────────────────────────────── */

static const RegEntry s_a6xx_pipe[] = {
	{ 0x80, "WAIT_FOR_IDLE" },
	{ 0x81, "WFI_PEND_DECR" },
	{ 0x82, "QUERY_PEND_DECR" },
	{ 0x84, "WAIT_MEM_WRITES" },
	{ 0xa0, "NRT_ADDR" },
	{ 0xa2, "NRT_DATA" },
	{ 0xe7, "EVENT_CMD" },
	{ 0xe8, "EVENT_TS_ADDR" },
	{ 0xea, "EVENT_TS_CTRL" },
	{ 0xeb, "EVENT_TS_DATA" },
};

/* ─── A7XX Pipe Registers ─────────────────────────────────── */

static const RegEntry s_a7xx_pipe[] = {
	{ 0x81, "WFI_PEND_DECR" },
	{ 0x82, "QUERY_PEND_DECR" },
	{ 0x84, "WAIT_MEM_WRITES" },
	{ 0x87, "WAIT_FOR_IDLE" },
	{ 0xa0, "NRT_ADDR" },
	{ 0xa2, "NRT_DATA" },
	{ 0xe7, "EVENT_CMD" },
	{ 0xe8, "EVENT_TS_ADDR" },
	{ 0xea, "EVENT_TS_CTRL" },
	{ 0xeb, "EVENT_TS_DATA" },
};

/* ─── Generic linear-scan lookup ──────────────────────────── */

static const char* lookup(const RegEntry* table, size_t count, uint32_t offset)
{
	for (size_t i = 0; i < count; i++) {
		if (table[i].offset == offset)
			return table[i].name;
	}
	return nullptr;
}

#define LOOKUP(tbl, off) lookup(tbl, sizeof(tbl)/sizeof(tbl[0]), off)

/* ─── Public API ──────────────────────────────────────────── */

const char* afuc_ctrl_reg_name(AfucGpuVer gpuver, uint32_t offset)
{
	switch (gpuver) {
	case AFUC_A5XX: return LOOKUP(s_a5xx_ctrl, offset);
	case AFUC_A6XX: return LOOKUP(s_a6xx_ctrl, offset);
	case AFUC_A7XX: return LOOKUP(s_a7xx_ctrl, offset);
	default:        return nullptr;
	}
}

const char* afuc_sqe_reg_name(uint32_t offset)
{
	return LOOKUP(s_sqe_regs, offset);
}

const char* afuc_pipe_reg_name(AfucGpuVer gpuver, uint32_t offset)
{
	switch (gpuver) {
	case AFUC_A5XX: return nullptr; /* a5xx pipe regs not documented */
	case AFUC_A6XX: return LOOKUP(s_a6xx_pipe, offset);
	case AFUC_A7XX: return LOOKUP(s_a7xx_pipe, offset);
	default:        return nullptr;
	}
}
