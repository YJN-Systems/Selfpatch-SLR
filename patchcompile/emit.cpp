#include "emit.h"
#include "accumulation.h"

#include <spslr_list.h>

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

/*
 * These records mirror the C runtime descriptor layout emitted as assembly.
 * Keep them in sync with selfpatch's .spslr reader structures and
 * spslr_list.h opcode definitions.
 */

struct TARGET_REC {
	uint32_t size;
	uint32_t fieldcnt;
	uint32_t fieldoff;
};

struct TARGET_FIELD_REC {
	uint32_t offset;
	uint32_t size;
	uint32_t alignment;
	uint32_t flags;
};

struct IPIN_REC {
	std::string addr_sym;
	uint32_t size;
	uint32_t program;
};

struct IPIN_OP_REC {
	uint32_t code;
	uint32_t op0;
	uint32_t op1;
};

struct DPIN_REC {
	std::string addr_sym;
	uint32_t target;
};

/*
 * Identical "target field -> randomized offset" computations can share one
 * ipin program. The runtime program is immutable descriptor data, so multiple
 * instruction pins may point at the same program offset.
 */

struct PROGRAM_KEY {
	uint32_t target;
	uint32_t field;

	bool operator==(const PROGRAM_KEY &other) const
	{
		return target == other.target && field == other.field;
	}
};

struct PROGRAM_KEY_HASH {
	std::size_t operator()(const PROGRAM_KEY &k) const
	{
		return (static_cast<std::size_t>(k.target) << 32) ^ k.field;
	}
};

static bool emit_header(std::ostream &out);
static bool emit_u32_object(std::ostream &out, const char *name,
			    uint32_t value);
static bool emit_targets(std::ostream &out,
			 const std::vector<TARGET_REC> &targets);
static bool emit_target_fields(std::ostream &out,
			       const std::vector<TARGET_FIELD_REC> &fields);
static bool emit_ipins(std::ostream &out, const std::vector<IPIN_REC> &ipins);
static bool emit_ipin_ops(std::ostream &out,
			  const std::vector<IPIN_OP_REC> &ops);
static bool emit_dpins(std::ostream &out, const std::vector<DPIN_REC> &dpins);

/*
 * Build or reuse the tiny runtime program for one field-offset patch.
 *
 * Current ipin programs are simple:
 *   1. add the randomized offset of field <field> in target <target>
 *   2. patch the instruction immediate with the accumulated value
 *
 * The returned value is an index into spslr_ipin_ops[], not a byte address.
 */

static uint32_t intern_simple_ipin_program(
	std::vector<IPIN_OP_REC> &ops,
	std::unordered_map<PROGRAM_KEY, uint32_t, PROGRAM_KEY_HASH> &memo,
	uint32_t target, uint32_t field)
{
	PROGRAM_KEY key{ target, field };

	auto it = memo.find(key);
	if (it != memo.end())
		return it->second;

	const uint32_t start = static_cast<uint32_t>(ops.size());

	ops.push_back(IPIN_OP_REC{
		.code = SPSLR_IPIN_OP_ADD_OFFSET,
		.op0 = target,
		.op1 = field,
	});

	ops.push_back(IPIN_OP_REC{
		.code = SPSLR_IPIN_OP_PATCH,
		.op0 = 0,
		.op1 = 0,
	});

	memo.emplace(key, start);
	return start;
}

/*
 * Main executables carry complete SPSLR metadata in a read-only .spslr section.
 * The runtime locates these exported symbols during selfpatch initialization.
 */

static bool emit_header(std::ostream &out)
{
	out << ".section .spslr,\"a\",@progbits\n";
	out << ".balign 8\n";
	return !!out;
}

/*
 * Module metadata may contain relocations against module-local symbols.
 * Put it in writable relocation-friendly storage so the loader can resolve
 * those addresses without text/rodata relocation warnings.
 */

static bool emit_module_header(std::ostream &out)
{
	out << ".section .data.rel.ro.spslr,\"aw\",@progbits\n";
	out << ".balign 8\n";
	return !!out;
}

static bool emit_u32_object(std::ostream &out, const char *name, uint32_t value)
{
	out << ".globl " << name << "\n";
	out << ".type " << name << ", @object\n";
	out << ".balign 4\n";
	out << name << ":\n";
	out << "\t.long " << value << "\n";
	out << ".size " << name << ", 4\n";
	return !!out;
}

/*
 * Emit the global target table. The target UID is the array index, so callers
 * must provide a dense vector ordered by global UID.
 */

static bool emit_targets(std::ostream &out,
			 const std::vector<TARGET_REC> &targets)
{
	if (!emit_u32_object(out, "spslr_target_cnt",
			     static_cast<uint32_t>(targets.size())))
		return false;

	out << ".globl spslr_targets\n";
	out << ".type spslr_targets, @object\n";
	out << ".balign 4\n";
	out << "spslr_targets:\n";

	for (const TARGET_REC &t : targets) {
		out << "\t.long " << t.size << "\n";
		out << "\t.long " << t.fieldcnt << "\n";
		out << "\t.long " << t.fieldoff << "\n";
	}

	out << ".size spslr_targets, .-spslr_targets\n";
	return !!out;
}

/*
 * Emit the flattened field table referenced by TARGET_REC::fieldoff.
 * Fields are stored in original-layout order and later permuted by runtime.
 */

static bool emit_target_fields(std::ostream &out,
			       const std::vector<TARGET_FIELD_REC> &fields)
{
	if (!emit_u32_object(out, "spslr_target_field_cnt",
			     static_cast<uint32_t>(fields.size())))
		return false;

	out << ".globl spslr_target_fields\n";
	out << ".type spslr_target_fields, @object\n";
	out << ".balign 4\n";
	out << "spslr_target_fields:\n";

	for (const TARGET_FIELD_REC &f : fields) {
		out << "\t.long " << f.offset << "\n";
		out << "\t.long " << f.size << "\n";
		out << "\t.long " << f.alignment << "\n";
		out << "\t.long " << f.flags << "\n";
	}

	out << ".size spslr_target_fields, .-spslr_target_fields\n";
	return !!out;
}

/*
 * Emit instruction pins. addr_sym names the immediate bytes inside the final
 * instruction stream; program indexes the spslr_ipin_ops interpreter program.
 */

static bool emit_ipins(std::ostream &out, const std::vector<IPIN_REC> &ipins)
{
	if (!emit_u32_object(out, "spslr_ipin_cnt",
			     static_cast<uint32_t>(ipins.size())))
		return false;

	out << ".globl spslr_ipins\n";
	out << ".type spslr_ipins, @object\n";
	out << ".balign 8\n";
	out << "spslr_ipins:\n";

	for (const IPIN_REC &ip : ipins) {
		out << "\t.quad " << ip.addr_sym << "\n";
		out << "\t.long " << ip.size << "\n";
		out << "\t.long " << ip.program << "\n";
	}

	out << ".size spslr_ipins, .-spslr_ipins\n";
	return !!out;
}

/*
 * Emit the bytecode-like ipin operation stream interpreted by selfpatch.
 * Programs terminate with SPSLR_IPIN_OP_PATCH.
 */

static bool emit_ipin_ops(std::ostream &out,
			  const std::vector<IPIN_OP_REC> &ops)
{
	if (!emit_u32_object(out, "spslr_ipin_op_cnt",
			     static_cast<uint32_t>(ops.size())))
		return false;

	out << ".globl spslr_ipin_ops\n";
	out << ".type spslr_ipin_ops, @object\n";
	out << ".balign 4\n";
	out << "spslr_ipin_ops:\n";

	for (const IPIN_OP_REC &op : ops) {
		out << "\t.long " << op.code << "\n";
		out << "\t.long " << op.op0 << "\n";
		out << "\t.long " << op.op1 << "\n";
	}

	out << ".size spslr_ipin_ops, .-spslr_ipin_ops\n";
	return !!out;
}

/*
 * Emit data pins. Each record names an already-existing object address that
 * must be rewritten from original layout into randomized layout at startup.
 */

static bool emit_dpins(std::ostream &out, const std::vector<DPIN_REC> &dpins)
{
	if (!emit_u32_object(out, "spslr_dpin_cnt",
			     static_cast<uint32_t>(dpins.size())))
		return false;

	out << ".globl spslr_dpins\n";
	out << ".type spslr_dpins, @object\n";
	out << ".balign 8\n";
	out << "spslr_dpins:\n";

	for (const DPIN_REC &dp : dpins) {
		out << "\t.quad " << dp.addr_sym << "\n";
		out << "\t.long " << dp.target << "\n";
	}

	out << ".size spslr_dpins, .-spslr_dpins\n";
	return !!out;
}

}

bool emit_patcher_program_asm(std::ostream &out, bool is_module)
{
	if (!is_module) {
		if (!emit_header(out))
			return false;
	} else {
		if (!emit_module_header(out))
			return false;
	}

	/*
	 * Runtime descriptors use dense array indexing, not maps. Therefore every
	 * global target UID from 0 to targets.size()-1 must exist before emission.
	 */
	std::vector<TARGET_REC> target_recs(targets.size());
	std::vector<TARGET_FIELD_REC> field_recs;

	/*
	 * Only the main executable emits the target layout table. Modules reuse the
	 * executable's target map and contribute only their own ipins/dpins.
	 */
	if (!is_module) {
		field_recs.reserve(64);

		for (uint32_t uid = 0;
		     uid < static_cast<uint32_t>(targets.size()); ++uid) {
			if (!targets.contains(uid))
				return false;

			const TARGET &target = targets.at(uid);

			TARGET_REC trec{};
			trec.size = static_cast<uint32_t>(target.size);
			trec.fieldoff =
				static_cast<uint32_t>(field_recs.size());
			trec.fieldcnt =
				static_cast<uint32_t>(target.fields.size());

			for (const auto &[off, field] : target.fields) {
				(void)off;
				field_recs.push_back(TARGET_FIELD_REC{
					.offset = static_cast<uint32_t>(
						field.offset),
					.size = static_cast<uint32_t>(
						field.size),
					.alignment = static_cast<uint32_t>(
						field.alignment),
					.flags = static_cast<uint32_t>(
						field.flags),
				});
			}

			target_recs[uid] = trec;
		}
	}

	std::vector<IPIN_REC> ipin_recs;
	std::vector<IPIN_OP_REC> ipin_ops;
	std::unordered_map<PROGRAM_KEY, uint32_t, PROGRAM_KEY_HASH> program_memo;

	std::vector<DPIN_REC> dpin_recs;

	for (const auto &[cu_uid, cu] : units) {
		(void)cu_uid;

		/*
		 * Resolve each CU-local ipin to a global target and field index. Field offsets
		 * from pinpoint are accepted only if they survived global target merging.
		 */
		for (const auto &[sym, ipin] : cu.ipins) {
			const uint32_t global_target = static_cast<uint32_t>(
				cu.local_targets.at(ipin.local_target));

			if (!targets.contains(global_target))
				return false;

			const TARGET &target = targets.at(global_target);

			if (!target.fields.contains(ipin.field_offset))
				return false;

			const FIELD &field =
				target.fields.at(ipin.field_offset);

			const uint32_t program = intern_simple_ipin_program(
				ipin_ops, program_memo, global_target,
				static_cast<uint32_t>(field.idx));

			ipin_recs.push_back(IPIN_REC{
				.addr_sym = ipin.symbol,
				.size = static_cast<uint32_t>(ipin.imm_size),
				.program = program,
			});
		}

		for (const auto &[sym, dpin] : cu.dpins) {
			std::vector<DPIN::COMPONENT> sorted_components(
				dpin.components.begin(), dpin.components.end());

			/*
			 * Patch nested data from inside to outside.
			 *
			 * Rewriting an outer object may move the bytes containing an inner object.
			 * Sorting by descending nesting level ensures inner target instances are
			 * converted while their original addresses are still meaningful.
			 */
			std::sort(sorted_components.begin(),
				  sorted_components.end(),
				  [](const DPIN::COMPONENT &a,
				     const DPIN::COMPONENT &b) {
					  return a.level > b.level;
				  });

			for (const DPIN::COMPONENT &component :
			     sorted_components) {
				const uint32_t global_target =
					static_cast<uint32_t>(
						cu.local_targets.at(
							component.target));

				/*
				 * A component offset turns one linker symbol into the address of an embedded
				 * target instance inside that object.
				 */
				std::string addr = dpin.symbol;
				if (component.offset != 0)
					addr += " + " +
						std::to_string(
							component.offset);

				dpin_recs.push_back(DPIN_REC{
					.addr_sym = std::move(addr),
					.target = global_target,
				});
			}
		}
	}

	if (!is_module) {
		if (!emit_targets(out, target_recs))
			return false;
		if (!emit_target_fields(out, field_recs))
			return false;
	}

	if (!emit_ipins(out, ipin_recs))
		return false;
	if (!emit_ipin_ops(out, ipin_ops))
		return false;
	if (!emit_dpins(out, dpin_recs))
		return false;

	out << ".section .note.GNU-stack,\"\",@progbits\n";
	return !!out;
}
