#include "task.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <spslr.h>

typedef void (*module_report_fn)(struct list_head *task_list);

static struct task_struct init_task = {
    .pid = 1,
    .ppid = 0,
    .comm = "init",
    .state = TASK_SLEEPING,
    .stats = {
        .switches = 0,
        .runtime_ms = 0,
        .priority = 20,
    },
    .parent = NULL,
};

static struct task_struct kworker_task = {
    .pid = 7,
    .ppid = 1,
    .comm = "kworker/0",
    .state = TASK_RUNNING,
    .stats = {
        .switches = 0,
        .runtime_ms = 0,
        .priority = 5,
    },
    .parent = &init_task,
};

static LIST_HEAD(system_task_list);

static const char *state_name(enum task_state state)
{
	switch (state) {
	case TASK_RUNNING:
		return "running";
	case TASK_SLEEPING:
		return "sleeping";
	case TASK_WAITING:
		return "waiting";
	case TASK_ZOMBIE:
		return "zombie";
	default:
		return "unknown";
	}
}

static struct task_struct *task_alloc(int pid, int ppid, const char *comm,
				      enum task_state state, int priority,
				      struct task_struct *parent)
{
	struct task_struct *task = calloc(1, sizeof(*task));
	if (!task) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}

	task->pid = pid;
	task->ppid = ppid;
	snprintf(task->comm, sizeof(task->comm), "%s", comm);
	task->state = state;
	task->stats.priority = priority;
	task->parent = parent;
	INIT_LIST_HEAD(&task->tasks);

	return task;
}

static void build_task_list(void)
{
	INIT_LIST_HEAD(&system_task_list);

	INIT_LIST_HEAD(&init_task.tasks);
	INIT_LIST_HEAD(&kworker_task.tasks);

	list_add_tail(&init_task.tasks, &system_task_list);
	list_add_tail(&kworker_task.tasks, &system_task_list);

	struct task_struct *sshd =
		task_alloc(42, 1, "sshd", TASK_WAITING, 15, &init_task);

	struct task_struct *shell =
		task_alloc(100, 42, "shell", TASK_RUNNING, 10, sshd);

	struct task_struct *demo =
		task_alloc(101, 100, "demo", TASK_RUNNING, 8, shell);

	list_add_tail(&sshd->tasks, &system_task_list);
	list_add_tail(&shell->tasks, &system_task_list);
	list_add_tail(&demo->tasks, &system_task_list);
}

static struct task_struct *find_task(struct list_head *task_list, int pid)
{
	struct list_head *pos;

	list_for_each(pos, task_list) {
		struct task_struct *task =
			list_entry(pos, struct task_struct, tasks);

		if (task->pid == pid) {
			return task;
		}
	}

	return NULL;
}

static void simulate_tick(struct list_head *task_list)
{
	struct list_head *pos;

	list_for_each(pos, task_list) {
		struct task_struct *task =
			list_entry(pos, struct task_struct, tasks);

		if (task->state == TASK_RUNNING) {
			task->stats.runtime_ms += 10;
			task->stats.switches++;
		} else if (task->state == TASK_WAITING) {
			task->stats.runtime_ms += 1;
		}
	}
}

static void print_tasks(struct list_head *task_list)
{
	struct list_head *pos;

	puts("PID   PPID  STATE      PRIO  SWITCHES  RUNTIME  PARENT     COMM");

	list_for_each(pos, task_list) {
		struct task_struct *task =
			list_entry(pos, struct task_struct, tasks);
		const char *parent_name = task->parent ? task->parent->comm :
							 "-";

		printf("%-5d %-5d %-10s %-5d %-9lu %-7lu %-10s %s\n", task->pid,
		       task->ppid, state_name(task->state),
		       task->stats.priority, task->stats.switches,
		       task->stats.runtime_ms, parent_name, task->comm);
	}
}

static int fetch_module_spslr_symbols(void *handle, struct spslr_module *mod)
{
	if (!handle || !mod)
		return -1;

	mod->ipin_cnt = dlsym(handle, SPSLR_MODULE_SYM_IPIN_CNT);
	if (!mod->ipin_cnt)
		return -1;

	mod->ipins = dlsym(handle, SPSLR_MODULE_SYM_IPINS);
	if (!mod->ipins)
		return -1;

	mod->ipin_op_cnt = dlsym(handle, SPSLR_MODULE_SYM_IPIN_OP_CNT);
	if (!mod->ipin_op_cnt)
		return -1;

	mod->ipin_ops = dlsym(handle, SPSLR_MODULE_SYM_IPIN_OPS);
	if (!mod->ipin_ops)
		return -1;

	mod->dpin_cnt = dlsym(handle, SPSLR_MODULE_SYM_DPIN_CNT);
	if (!mod->dpin_cnt)
		return -1;

	mod->dpins = dlsym(handle, SPSLR_MODULE_SYM_DPINS);
	if (!mod->dpins)
		return -1;

	return 0;
}

static int run_module_report(const char *module_path,
			     struct list_head *task_list)
{
	void *handle = dlopen(module_path, RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		fprintf(stderr, "dlopen failed for %s: %s\n", module_path,
			dlerror());
		return -1;
	}

	struct spslr_module mod;
	if (fetch_module_spslr_symbols(handle, &mod) < 0) {
		fprintf(stderr, "failed to fetch spslr symbols in module\n");
		dlclose(handle);
		return -1;
	}

	struct spslr_status status = spslr_patch_module(&mod);
	if (status.error != SPSLR_OK) {
		fprintf(stderr, "failed to patch module\n");
		dlclose(handle);
		return -1;
	}

	dlerror();

	module_report_fn report =
		(module_report_fn)dlsym(handle, "subject_module_report");

	const char *err = dlerror();
	if (err) {
		fprintf(stderr, "dlsym failed: %s\n", err);
		dlclose(handle);
		return -1;
	}

	report(task_list);

	dlclose(handle);
	return 0;
}

static void print_task_struct_layout(void)
{
	puts("== task_struct layout ==");
	printf("sizeof(struct task_struct): %zu\n", sizeof(struct task_struct));
	printf("pid:     offset %-3zu size %zu\n",
	       offsetof(struct task_struct, pid),
	       sizeof(((struct task_struct *)0)->pid));
	printf("ppid:    offset %-3zu size %zu\n",
	       offsetof(struct task_struct, ppid),
	       sizeof(((struct task_struct *)0)->ppid));
	printf("comm:    offset %-3zu size %zu\n",
	       offsetof(struct task_struct, comm),
	       sizeof(((struct task_struct *)0)->comm));
	printf("state:   offset %-3zu size %zu\n",
	       offsetof(struct task_struct, state),
	       sizeof(((struct task_struct *)0)->state));
	printf("stats:   offset %-3zu size %zu\n",
	       offsetof(struct task_struct, stats),
	       sizeof(((struct task_struct *)0)->stats));
	printf("parent:  offset %-3zu size %zu\n",
	       offsetof(struct task_struct, parent),
	       sizeof(((struct task_struct *)0)->parent));
	printf("tasks:   offset %-3zu size %zu\n",
	       offsetof(struct task_struct, tasks),
	       sizeof(((struct task_struct *)0)->tasks));
	putchar('\n');
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s <path-to-subject-module.so>\n",
			argv[0]);
		return EXIT_FAILURE;
	}

	struct spslr_status status = spslr_init();
	if (status.error != SPSLR_OK) {
		fprintf(stderr, "failed to initialize spslr\n");
		return EXIT_FAILURE;
	}

	status = spslr_selfpatch();
	if (status.error != SPSLR_OK) {
		fprintf(stderr, "failed to selfpatch\n");
		return EXIT_FAILURE;
	}

	print_task_struct_layout();

	build_task_list();

	puts("== task table before scheduler tick ==");
	print_tasks(&system_task_list);

	simulate_tick(&system_task_list);
	simulate_tick(&system_task_list);

	struct task_struct *demo = find_task(&system_task_list, 101);
	if (demo) {
		demo->state = TASK_SLEEPING;
		demo->stats.priority = 12;
	}

	puts("\n== task table after scheduler tick ==");
	print_tasks(&system_task_list);

	puts("\n== module report ==");
	if (run_module_report(argv[1], &system_task_list) < 0)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
