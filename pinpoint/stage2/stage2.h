#pragma once

#include <stage0.h>
#include <safe-rtl.h>
#include <unordered_map>
#include <string>

struct S2InstructionPin {
	UID target;
	std::size_t offset;
	std::size_t imm_size;
	std::string symbol;
};

const std::unordered_map<UID, S2InstructionPin> &s2_pins();
void s2_pins_reset();
UID s2_pin_allocate(const S2InstructionPin &pin);

struct rtl_pin_lower_pass : rtl_opt_pass {
	rtl_pin_lower_pass(gcc::context *ctxt);
	unsigned int execute(function *fn) override;
};
