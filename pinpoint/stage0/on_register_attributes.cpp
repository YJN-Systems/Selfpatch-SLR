#include <stage0.h>
#include <pinpoint_config.h>
#include <cstdio>

static tree log_new_target(tree *node, tree name, tree args, int flags,
			   bool *no_add_attrs)
{
	if (node)
		TargetType::add(*node);

	return NULL_TREE;
}

static tree check_field_fixed_attribute(tree *node, tree name, tree args,
					int flags, bool *no_add_attrs)
{
	if (!node || !*node || TREE_CODE(*node) != FIELD_DECL) {
		*no_add_attrs = true;
		fprintf(stderr,
			"%qs attribute only applies to struct/union fields",
			SPSLR_FIELD_FIXED_ATTRIBUTE);
	}
	return NULL_TREE;
}

/*
 * __attribute__((spslr)) marks a record type as a randomization target.
 * The attribute only registers the type here; fields are fetched later once
 * GCC has completed the type.
 */

static struct attribute_spec spslr_attribute = {
	SPSLR_ATTRIBUTE, 0, 0, false, false, false, false, log_new_target, NULL
};

/*
 * __attribute__((spslr_field_fixed)) marks a field as layout-sensitive.
 * Fixed fields remain part of the target, but are treated as dangerous so
 * instruction/data pins are not generated for offsets that would become
 * ambiguous after randomization.
 */

static struct attribute_spec spslr_fixed_field_attribute = {
	SPSLR_FIELD_FIXED_ATTRIBUTE, 0,	  0, false, false, false, false,
	check_field_fixed_attribute, NULL
};

void on_register_attributes(void *plugin_data, void *user_data)
{
	register_attribute(&spslr_attribute);
	register_attribute(&spslr_fixed_field_attribute);
}
