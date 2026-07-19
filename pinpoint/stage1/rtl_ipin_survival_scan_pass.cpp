#include <pinpoint.h>
#include <passes.h>
#include <ipin_registry.h>
#include <safe-rtl.h>

static const pass_data rtl_ipin_survival_scan_pass_data = {
	RTL_PASS,
	"spslr_rtl_ipin_survival_scan",
	OPTGROUP_NONE,
	TV_NONE,
	PROP_rtl,
	0,
	0,
	0,
	0,
};

rtl_ipin_survival_scan_pass::rtl_ipin_survival_scan_pass(gcc::context *ctxt)
	: rtl_opt_pass(rtl_ipin_survival_scan_pass_data, ctxt)
{
}

unsigned int rtl_ipin_survival_scan_pass::execute(function *fn)
{
	(void)fn;

	for (rtx_insn *insn = get_insns(); insn; insn = NEXT_INSN(insn)) {
		if (!NONDEBUG_INSN_P(insn))
			continue;

		ipin::handle pin = ipin::identify_rtl_pin(PATTERN(insn));
		if (pin != ipin::invalid)
			ipin::mark_live(pin, PATTERN(insn));
	}

	return 0;
}
