#ifndef TASK_H
#define TASK_H

#include <stddef.h>

enum task_state {
	TASK_RUNNING,
	TASK_SLEEPING,
	TASK_WAITING,
	TASK_ZOMBIE,
};

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

struct sched_stats {
	unsigned long switches;
	unsigned long runtime_ms;
	int priority;
};

struct task_struct {
	int pid;
	int ppid;
	char comm[32];
	enum task_state state;
	struct sched_stats stats;
	struct task_struct *parent;
	struct list_head tasks;
} __attribute__((spslr));

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define LIST_HEAD(name) struct list_head name = { &(name), &(name) }

static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

static inline void list_add_tail(struct list_head *node, struct list_head *head)
{
	struct list_head *prev = head->prev;

	node->next = head;
	node->prev = prev;
	prev->next = node;
	head->prev = node;
}

static inline void list_del(struct list_head *node)
{
	struct list_head *next = node->next;
	struct list_head *prev = node->prev;

	prev->next = next;
	next->prev = prev;

	node->next = node;
	node->prev = node;
}

#endif
