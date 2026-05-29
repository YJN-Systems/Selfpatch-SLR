#include "task.h"

#include <stdio.h>

void subject_module_report(struct list_head *task_list)
{
	struct list_head *pos;
	unsigned long total_runtime = 0;
	unsigned long total_switches = 0;
	size_t task_count = 0;
	size_t running_count = 0;

	list_for_each(pos, task_list) {
		struct task_struct *task =
			list_entry(pos, struct task_struct, tasks);

		task_count++;
		total_runtime += task->stats.runtime_ms;
		total_switches += task->stats.switches;

		if (task->state == TASK_RUNNING) {
			running_count++;
		}
	}

	printf("tasks:          %zu\n", task_count);
	printf("running tasks:  %zu\n", running_count);
	printf("total switches: %lu\n", total_switches);
	printf("total runtime:  %lums\n", total_runtime);
}
