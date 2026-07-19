#define _GNU_SOURCE

#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <link.h>
#include <dlfcn.h>

#include <spslr.h>
#include <sanemaker/traps.h>

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

static struct task_struct copy_init_task()
{
	return init_task;
}

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

	sanemaker_target_tag(task, struct task_struct);

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

static void destroy_dynamic_tasks(void)
{
	struct list_head *pos = system_task_list.next;

	while (pos != &system_task_list) {
		struct list_head *next = pos->next;
		struct task_struct *task =
			list_entry(pos, struct task_struct, tasks);

		if (task != &init_task && task != &kworker_task) {
			list_del(&task->tasks);
			sanemaker_target_untag(task);
			free(task);
		}

		pos = next;
	}

	INIT_LIST_HEAD(&system_task_list);
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

#define XSTR(a) STR(a)
#define STR(a) #a

static int fetch_spslr_entry(void *handle, struct spslr_entry *entry)
{
	if (!handle)
		return -1;

	entry->start_units = dlsym(handle, XSTR(SPSLR_START_UNITS_SYM));
	if (!entry->start_units)
		return -1;

	entry->stop_units = dlsym(handle, XSTR(SPSLR_STOP_UNITS_SYM));
	if (!entry->stop_units)
		return -1;

	entry->start_targets = dlsym(handle, XSTR(SPSLR_START_TARGETS_SYM));
	if (!entry->start_targets)
		return -1;

	entry->stop_targets = dlsym(handle, XSTR(SPSLR_STOP_TARGETS_SYM));
	if (!entry->stop_targets)
		return -1;

	return 0;
}

static void sanemaker_register_image_phdrs(const char *name, uintptr_t base,
					   const ElfW(Phdr) * phdrs,
					   ElfW(Half) phnum)
{
	if (!name || !name[0])
		return;

	sanemaker_new_image(name, (const void *)base);

	for (ElfW(Half) i = 0; i < phnum; i++) {
		const ElfW(Phdr) *phdr = &phdrs[i];

		if (phdr->p_type != PT_LOAD)
			continue;

		if (!(phdr->p_flags & PF_X))
			continue;

		const void *begin = (const void *)(base + phdr->p_vaddr);
		const void *end =
			(const void *)(base + phdr->p_vaddr + phdr->p_memsz);

		sanemaker_new_image_text(name, begin, end);
	}
}

static void sanemaker_deregister_link_map_image(struct link_map *lm)
{
	if (!lm || !lm->l_name)
		return;

	const char *name = lm->l_name;
	sanemaker_drop_image(name);
}

static int sanemaker_register_image_handle(void *handle)
{
	struct link_map *lm = NULL;

	if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) != 0 || !lm)
		return -1;

	ElfW(Ehdr) *ehdr = (ElfW(Ehdr) *)lm->l_addr;
	ElfW(Phdr) *phdrs = (ElfW(Phdr) *)((uintptr_t)ehdr + ehdr->e_phoff);

	sanemaker_register_image_phdrs(lm->l_name, (uintptr_t)lm->l_addr, phdrs,
				       ehdr->e_phnum);

	return 0;
}

static int sanemaker_deregister_image_handle(void *handle)
{
	struct link_map *lm = NULL;

	if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) != 0 || !lm)
		return -1;

	sanemaker_deregister_link_map_image(lm);
	return 0;
}

static int sanemaker_register_loaded_image_cb(struct dl_phdr_info *info,
					      size_t size, void *data)
{
	(void)size;
	(void)data;

	sanemaker_register_image_phdrs(info->dlpi_name,
				       (uintptr_t)info->dlpi_addr,
				       info->dlpi_phdr, info->dlpi_phnum);

	return 0;
}

static int sanemaker_register_loaded_images(void)
{
	return dl_iterate_phdr(sanemaker_register_loaded_image_cb, NULL);
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

	struct spslr_ctx module_ctx;

	if (fetch_spslr_entry(handle, &module_ctx.entry) != 0) {
		fprintf(stderr,
			"failed to fetch spslr entry location in module\n");
		dlclose(handle);
		return -1;
	}

	unsigned long workspace_size = spslr_workspace_size(&module_ctx.entry);
	module_ctx.workspace = malloc(workspace_size);
	if (!module_ctx.workspace) {
		fprintf(stderr, "failed to allocate module workspace buffer\n");
		dlclose(handle);
		return -1;
	}

	struct spslr_status status = spslr_patch_module(&module_ctx);
	free(module_ctx.workspace);

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

	if (sanemaker_register_image_handle(handle) != 0) {
		fprintf(stderr, "warning: failed to register image %s\n",
			module_path);
	}

	report(task_list);

	sanemaker_deregister_image_handle(handle);

	dlclose(handle);
	return 0;
}

static void print_task_struct_layout(void)
{
	puts("\n== task_struct layout ==");
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
}

__attribute__((noinline)) char read_at_13(const struct task_struct *task)
{
	const char *bytes = (const char *)task;
	return bytes[13];
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s <path-to-subject-module.so>\n",
			argv[0]);
		return EXIT_FAILURE;
	}

	if (sanemaker_register_loaded_images() != 0)
		fprintf(stderr, "warning: failed to register loaded images\n");

	sanemaker_target_tag(&init_task, struct task_struct);
	sanemaker_target_tag(&kworker_task, struct task_struct);

	const unsigned char *task_struct_hash =
		spslr_target_hash(struct task_struct);
	printf("The task_struct hash is at %p\n  Its value is: ",
	       task_struct_hash);
	for (unsigned i = 0; i < 16; i++)
		printf("%02x", task_struct_hash[i]);
	printf("\n");

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

	puts("\n== task table before scheduler tick ==");
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

	puts("\n== init_task rvalue field access ==");
	printf("copy_init_task().comm=\"%s\" (should be \"init\")\n",
	       copy_init_task().comm);

	destroy_dynamic_tasks();

	char invalid_read = read_at_13(&init_task);
	printf("\nUninstrumented read yields %x\n", invalid_read & 0xff);

	sanemaker_target_untag(&init_task);
	sanemaker_target_untag(&kworker_task);

	return EXIT_SUCCESS;
}
