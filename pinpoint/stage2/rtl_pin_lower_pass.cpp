#include <stage2.h>
#include <stage1.h>
#include <pinpoint_config.h>
#include <pinpoint_error.h>

#include <safe-rtl.h>

#include <unordered_map>
#include <string>
#include <cstring>
#include <cstdio>

static UID next_stage2_pin_uid = 0;
static std::unordered_map<UID, S2InstructionPin> pins;

const std::unordered_map<UID, S2InstructionPin> &s2_pins()
{
	return pins;
}

void s2_pins_reset()
{
	pins.clear();
	next_stage2_pin_uid = 0;
}

/*
 * Stage2 pins are the bridge from compiler metadata to runtime patching:
 * target + original offset describe what to compute, while symbol points at
 * the immediate bytes that must be rewritten.
 */

UID s2_pin_allocate(const S2InstructionPin &pin)
{
	UID uid = next_stage2_pin_uid++;
	pins.emplace(uid, pin);
	return uid;
}

static bool read_marker_inputs(rtx src, UID &target, std::size_t &offset)
{
	rtx in0 = ASM_OPERANDS_INPUT(src, 0);
	rtx in1 = ASM_OPERANDS_INPUT(src, 1);

	if (!CONST_INT_P(in0) || !CONST_INT_P(in1))
		return false;

	target = (UID)INTVAL(in0);
	offset = (std::size_t)INTVAL(in1);
	return true;
}

static bool extract_set_from_pat(rtx pat, rtx &set_out)
{
	if (!pat)
		return false;

	if (GET_CODE(pat) == SET) {
		set_out = pat;
		return true;
	}

	if (GET_CODE(pat) == PARALLEL) {
		int len = XVECLEN(pat, 0);
		for (int i = 0; i < len; ++i) {
			rtx elem = XVECEXP(pat, 0, i);
			if (elem && GET_CODE(elem) == SET) {
				set_out = elem;
				return true;
			}
		}
	}

	return false;
}

static bool get_regno_from_dest(rtx dest, unsigned &regno)
{
	if (!dest)
		return false;

	if (REG_P(dest)) {
		regno = REGNO(dest);
		return true;
	}

	if (GET_CODE(dest) == SUBREG) {
		rtx inner = SUBREG_REG(dest);
		if (inner && REG_P(inner)) {
			regno = REGNO(inner);
			return true;
		}
	}

	return false;
}

/*
 * Recognize the RTL form of a stage1 marker.
 *
 * At this point GCC has selected the destination register. That register
 * choice determines the exact x86_64 encoding we must emit so the immediate
 * operand can be labeled and patched later.
 */

static bool is_stage1_marker_insn(rtx_insn *insn, rtx &set_rtx, rtx &asm_src,
				  UID &target, std::size_t &offset,
				  unsigned &regno)
{
	if (!insn || !INSN_P(insn))
		return false;

	rtx pat = PATTERN(insn);
	if (!extract_set_from_pat(pat, set_rtx))
		return false;

	rtx dest = SET_DEST(set_rtx);
	rtx src = SET_SRC(set_rtx);

	if (!src || GET_CODE(src) != ASM_OPERANDS)
		return false;

	const char *templ = ASM_OPERANDS_TEMPLATE(src);
	if (!templ)
		return false;

	/* safer than exact strcmp */
	if (!std::strstr(templ, s1_ipin_marker))
		return false;

	if (!read_marker_inputs(src, target, offset))
		return false;

	if (!get_regno_from_dest(dest, regno))
		return false;

	asm_src = src;
	return true;
}

static const pass_data rtl_pin_lower_pass_data = {
	RTL_PASS, /* type */
	"spslr_rtl_pin_lower", /* name */
	OPTGROUP_NONE, /* optinfo_flags */
	TV_NONE, /* tv_id */
	PROP_rtl, /* properties_required */
	0, /* properties_provided */
	0, /* properties_destroyed */
	0, /* todo_flags_start */
	0 /* todo_flags_finish */
};

struct EncodedReg {
	unsigned rex;
	unsigned modrm;
};

/*
 * Encode "mov imm32, r64" for the hard register selected by GCC.
 *
 * The immediate is deliberately four bytes: the final asm labels exactly that
 * immediate field, and runtime selfpatch overwrites those bytes with the
 * randomized offset value.
 */
static bool x86_64_encode_mov_imm32_to_reg(unsigned regno, EncodedReg &out)
{
	if (!HARD_REGISTER_NUM_P(regno))
		return false;

	const char *name = reg_names[regno];
	if (!name)
		return false;

	struct RegMapEntry {
		const char *name;
		unsigned rex;
		unsigned rm;
	};

	static const RegMapEntry regmap[] = {
		{ "ax", 0x48, 0 },  { "cx", 0x48, 1 },	{ "dx", 0x48, 2 },
		{ "bx", 0x48, 3 },  { "sp", 0x48, 4 },	{ "bp", 0x48, 5 },
		{ "si", 0x48, 6 },  { "di", 0x48, 7 },

		{ "r8", 0x49, 0 },  { "r9", 0x49, 1 },	{ "r10", 0x49, 2 },
		{ "r11", 0x49, 3 }, { "r12", 0x49, 4 }, { "r13", 0x49, 5 },
		{ "r14", 0x49, 6 }, { "r15", 0x49, 7 },
	};

	for (const auto &e : regmap) {
		if (std::strcmp(name, e.name) == 0) {
			out.rex = e.rex;
			out.modrm = 0xC0 | e.rm; /* mod=11, /0, rm=e.rm */
			return true;
		}
	}

	return false;
}

/*
 * Emit the final instruction bytes manually so the immediate operand can be
 * exposed as an object symbol.
 *
 * The symbol does not denote executable code as a callable function; it denotes
 * the four-byte immediate field inside the instruction stream.
 */

static std::string make_final_x86_64_asm(const std::string &sym,
					 const EncodedReg &enc, std::size_t imm)
{
	char buf[512];
	std::snprintf(buf, sizeof(buf),
		      ".byte 0x%02x, 0xC7, 0x%02x\n"
		      "%s:\n"
		      ".type %s, @object\n"
		      ".size %s, 4\n"
		      ".long %zu",
		      enc.rex, enc.modrm, sym.c_str(), sym.c_str(), sym.c_str(),
		      imm);
	return std::string(buf);
}

static bool lower_stage1_marker_insn(rtx_insn *insn)
{
	rtx set_rtx = nullptr;
	rtx asm_src = nullptr;
	UID target = 0;
	std::size_t offset = 0;
	unsigned regno = 0;

	if (!is_stage1_marker_insn(insn, set_rtx, asm_src, target, offset,
				   regno))
		return false;

	EncodedReg enc{};
	if (!x86_64_encode_mov_imm32_to_reg(regno, enc)) {
		pinpoint_fatal(
			"stage2: unsupported hard register for ipin lowering: regno=%u",
			(unsigned)regno);
		return false;
	}

	S2InstructionPin pin;
	pin.target = target;
	pin.offset = offset;
	pin.imm_size = 4;

	UID pin_uid = s2_pin_allocate(pin);

	auto it = pins.find(pin_uid);
	if (it == pins.end())
		pinpoint_fatal("stage2: internal error after s2_pin_allocate");

	it->second.symbol = ".L" + std::string(SPSLR_PINPOINT_STAGE2_PIN) +
			    std::to_string(pin_uid);

	std::string final_asm =
		make_final_x86_64_asm(it->second.symbol, enc, offset);

	ASM_OPERANDS_TEMPLATE(asm_src) = ggc_strdup(final_asm.c_str());
	return true;
}

rtl_pin_lower_pass::rtl_pin_lower_pass(gcc::context *ctxt)
	: rtl_opt_pass(rtl_pin_lower_pass_data, ctxt)
{
}

unsigned int rtl_pin_lower_pass::execute(function *fn)
{
	(void)fn;

	for (rtx_insn *insn = get_insns(); insn; insn = NEXT_INSN(insn)) {
		(void)lower_stage1_marker_insn(insn);
	}

	return 0;
}
