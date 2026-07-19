#include <functional>
#include <list>

#include <pinpoint.h>
#include <passes.h>
#include <ipin_registry.h>
#include <target_registry.h>

/*
 * AccessChain flattens nested COMPONENT_REF / ARRAY_REF expressions from
 * outer base to inner access.
 *
 * This lets the pass rebuild the expression one step at a time while replacing
 * only SPSLR-relevant field offsets with separator calls. Non-target offsets
 * stay as ordinary constant pointer arithmetic.
 */

struct AccessChain {
	struct Step {
		enum Kind { STEP_COMPONENT, STEP_ARRAY } kind = STEP_COMPONENT;

		tree t = NULL_TREE;

		/* COMPONENT_REF data */
		bool relevant = false;
		tree field = NULL_TREE;
		ipin::handle pin = ipin::invalid;
	};

	bool relevant = false;
	std::list<Step> steps;
	tree base = NULL_TREE;
};

static tree walk_tree_contains_component_ref(tree *tp, int *walk_subtrees,
					     void *data)
{
	int *found_flag = (int *)data;

	if (!tp || !*tp)
		return NULL_TREE;

	if (TREE_CODE(*tp) == COMPONENT_REF)
		*found_flag = 1;

	return NULL_TREE;
}

static bool tree_contains_component_ref(tree ref)
{
	int found_flag = 0;
	walk_tree(&ref, walk_tree_contains_component_ref, &found_flag, NULL);
	return found_flag != 0;
}

static bool access_chain(tree ref, AccessChain &chain)
{
	if (!ref)
		return false;

	switch (TREE_CODE(ref)) {
	case COMPONENT_REF: {
		AccessChain::Step step;
		step.kind = AccessChain::Step::STEP_COMPONENT;
		step.t = ref;
		tree field;
		step.relevant = target::component_ref(ref, &field) &&
				!target::field_is_fixed(field);
		if (step.relevant) {
			step.field = field;
			step.pin = ipin::make(field);
			chain.relevant = true;
		}
		chain.steps.push_front(step);
		return access_chain(TREE_OPERAND(ref, 0), chain);
	}

	case ARRAY_REF:
	case ARRAY_RANGE_REF: {
		AccessChain::Step step;
		step.kind = AccessChain::Step::STEP_ARRAY;
		step.t = ref;
		chain.steps.push_front(step);
		return access_chain(TREE_OPERAND(ref, 0), chain);
	}

	default:
		/*
		 * Access chain construction needs to reach all COMPONENT_REFs. Further
		 * implementation may be required to cover all possible AST scenarios.
		 */
		if (tree_contains_component_ref(ref))
			return false;

		chain.base = ref;
		return true;
	}
}

/*
 * Rewrite a relevant field-access chain into explicit pointer arithmetic.
 *
 * The resulting MEM_REF has offset zero; all interesting byte offsets have
 * either become separator calls or fixed constants. This makes the later asm
 * marker pass independent of GCC's original COMPONENT_REF tree shape.
 */

static tree separate_offset_chain_maybe(tree ref, gimple_stmt_iterator *gsi)
{
	AccessChain chain;
	if (!access_chain(ref, chain)) {
		pinpoint_fatal(
			"separate_offset_chain_maybe encountered invalid access chain: top=%s base=%s",
			get_tree_code_name(TREE_CODE(ref)),
			TREE_OPERAND(ref, 0) ? get_tree_code_name(TREE_CODE(
						       TREE_OPERAND(ref, 0))) :
					       "<null>");
	}

	if (!chain.relevant)
		return NULL_TREE;

	tree cur_expr = chain.base;

	// NOTE -> Could fold into single call here (needs to track what offsets contribute, +irrelevant combined)

	for (const AccessChain::Step &step : chain.steps) {
		if (step.kind == AccessChain::Step::STEP_COMPONENT) {
			if (TREE_CODE(cur_expr) == ADDR_EXPR)
				pinpoint_fatal(
					"separate_offset_chain_maybe encountered ADDR_EXPR as base of COMPONENT_REF");

			tree cur_ptr = build_fold_addr_expr(cur_expr);

			tree field_ptr_type =
				build_pointer_type(TREE_TYPE(step.t));
			tree field_ptr;

			if (step.relevant) {
				tree return_tmp =
					create_tmp_var(size_type_node, NULL);
				gimple *call_stmt = ipin::make_gimple_separator(
					return_tmp, step.pin);
				if (!call_stmt)
					pinpoint_fatal(
						"separate_offset_chain_maybe failed to make gimple separator");

				gsi_insert_before(gsi, call_stmt,
						  GSI_SAME_STMT);
				field_ptr = build2(POINTER_PLUS_EXPR,
						   field_ptr_type, cur_ptr,
						   return_tmp);
			} else {
				tree field_decl = TREE_OPERAND(step.t, 1);

				std::size_t field_offset =
					target::field_offset(field_decl);
				bool field_bitfield =
					target::field_is_bitfield(field_decl);
				if (field_bitfield)
					pinpoint_fatal(
						"separate_offset_chain_maybe encountered bitfield access in relevant COMPONENT_REF chain");

				field_ptr = build2(POINTER_PLUS_EXPR,
						   field_ptr_type, cur_ptr,
						   build_int_cst(sizetype,
								 field_offset));
			}

			tree ptr_tmp = create_tmp_var(field_ptr_type, NULL);

			tree ptr_val = force_gimple_operand_gsi(
				gsi, field_ptr,
				true, // require simple result
				ptr_tmp, // target temp
				true, // insert before current stmt
				GSI_SAME_STMT);

			tree offset0 = fold_convert(TREE_TYPE(ptr_val),
						    build_int_cst(sizetype, 0));

			cur_expr = build2(MEM_REF, TREE_TYPE(step.t), ptr_val,
					  offset0);

			continue;
		}

		if (step.kind == AccessChain::Step::STEP_ARRAY) {
			tree idx = TREE_OPERAND(step.t, 1);
			tree low = TREE_OPERAND(step.t, 2);
			tree elts = TREE_OPERAND(step.t, 3);

			cur_expr = build4(TREE_CODE(step.t), TREE_TYPE(step.t),
					  cur_expr, idx, low, elts);
			continue;
		}
	}

	return cur_expr;
}

static void dispatch_separation_maybe(const std::list<tree *> &path,
				      gimple_stmt_iterator *gsi,
				      unsigned &cancel_levels)
{
	if (path.empty() || !gsi)
		return;

	tree ref = *path.back();
	if (!ref || TREE_CODE(ref) != COMPONENT_REF)
		return;

	cancel_levels = 1;

	tree instrumented_ref = separate_offset_chain_maybe(ref, gsi);
	if (!instrumented_ref)
		return;

	gimple_set_modified(gsi_stmt(*gsi), true);
	*path.back() = instrumented_ref;

	// At this point, instrumented_ref is a MEM_REF node (off=0). A wrapping ADDR_EXPR cancels it out.

	if (path.size() < 2)
		return;

	tree *parent = *(++path.rbegin());

	if (TREE_CODE(*parent) == ADDR_EXPR) {
		// Note -> the base of the MEM_REF is expected to have the same type as the ADDR_EXPR
		*parent = TREE_OPERAND(instrumented_ref, 0);
		cancel_levels++;
	}
}

static const pass_data separate_offset_pass_data = {
	GIMPLE_PASS, "separate_offset", OPTGROUP_NONE, TV_NONE, 0, 0, 0,
	0,	     TODO_update_ssa
};

separate_offset_pass::separate_offset_pass(gcc::context *ctxt)
	: gimple_opt_pass(separate_offset_pass_data, ctxt)
{
}

struct TreeWalkData {
	std::list<tree *> path;
	gimple_stmt_iterator *gsi;
	unsigned cancel_levels;
	std::function<void(const std::list<tree *> &, gimple_stmt_iterator *,
			   unsigned &)>
		callback;
};

static tree walk_tree_level(tree *tp, int *walk_subtrees, void *data)
{
	TreeWalkData *twd = (TreeWalkData *)data;
	if (!twd)
		return NULL_TREE;

	if (!twd->path.empty() && twd->path.back() == tp)
		return NULL_TREE; // root of this level

	if (walk_subtrees)
		*walk_subtrees = 0;

	twd->cancel_levels = 0;
	twd->path.push_back(tp);

	twd->callback(twd->path, twd->gsi, twd->cancel_levels);

	if (twd->cancel_levels == 0)
		walk_tree(tp, walk_tree_level, data, NULL);

	twd->path.pop_back();

	if (twd->cancel_levels > 0)
		twd->cancel_levels--;

	// Cancel current level if there are still cancel_levels due
	return twd->cancel_levels == 0 ? NULL_TREE : *tp;
}

static bool
walk_gimple_stmt(gimple_stmt_iterator *gsi,
		 std::function<void(const std::list<tree *> &,
				    gimple_stmt_iterator *, unsigned &)>
			 callback)
{
	if (!gsi || gsi_end_p(*gsi) || !callback)
		return false;

	gimple *stmt = gsi_stmt(*gsi);

	for (std::size_t i = 0; i < gimple_num_ops(stmt); i++) {
		tree *op = gimple_op_ptr(stmt, i);
		if (!op || !*op)
			continue;

		TreeWalkData twd;
		twd.gsi = gsi;
		twd.callback = callback;

		walk_tree_level(op, NULL, &twd);
	}

	return true;
}

unsigned int separate_offset_pass::execute(function *fn)
{
	if (!fn)
		return 0;

	basic_block bb;
	FOR_EACH_BB_FN(bb, fn)
	{
		for (gimple_stmt_iterator gsi = gsi_start_bb(bb);
		     !gsi_end_p(gsi); gsi_next(&gsi)) {
			if (!walk_gimple_stmt(&gsi, dispatch_separation_maybe))
				pinpoint_fatal(
					"separate_offset pass failed to walk gimple statement");
		}
	}

	return 0;
}
