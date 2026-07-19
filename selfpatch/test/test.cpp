#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "spslr_randomizer.h"
}

/*
 * The randomizer itself only needs malloc and random_u64 from the environment.
 * The remaining environment functions are intentionally not defined here: if
 * the randomizer starts depending on another environment service, the link
 * should fail and make that new dependency explicit.
 */

extern "C" void *spslr_env_malloc(spslr_u64 n)
{
	return std::malloc(static_cast<std::size_t>(n));
}

namespace
{
std::uint64_t random_state = UINT64_C(0x6a09e667f3bcc909);

std::uint64_t splitmix64()
{
	std::uint64_t z = (random_state += UINT64_C(0x9e3779b97f4a7c15));
	z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
	z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
	return z ^ (z >> 31);
}
} // namespace

extern "C" spslr_u64 spslr_env_random_u64(void)
{
	return splitmix64();
}

static const spslr_target_field task_struct_fields[] = {
	{ "thread_info", UINT64_C(24), UINT64_C(0), UINT64_C(8), UINT64_C(1) },
	{ "__state", UINT64_C(4), UINT64_C(24), UINT64_C(4), UINT64_C(1) },
	{ "saved_state", UINT64_C(4), UINT64_C(28), UINT64_C(4), UINT64_C(1) },
	{ "stack", UINT64_C(8), UINT64_C(32), UINT64_C(8), UINT64_C(0) },
	{ "usage", UINT64_C(4), UINT64_C(40), UINT64_C(4), UINT64_C(0) },
	{ "flags", UINT64_C(4), UINT64_C(44), UINT64_C(4), UINT64_C(0) },
	{ "ptrace", UINT64_C(4), UINT64_C(48), UINT64_C(4), UINT64_C(0) },
	{ "on_cpu", UINT64_C(4), UINT64_C(52), UINT64_C(4), UINT64_C(0) },
	{ "wake_entry", UINT64_C(16), UINT64_C(56), UINT64_C(8), UINT64_C(0) },
	{ "wakee_flips", UINT64_C(4), UINT64_C(72), UINT64_C(4), UINT64_C(0) },
	{ "wakee_flip_decay_ts", UINT64_C(8), UINT64_C(80), UINT64_C(8),
	  UINT64_C(0) },
	{ "last_wakee", UINT64_C(8), UINT64_C(88), UINT64_C(8), UINT64_C(0) },
	{ "recent_used_cpu", UINT64_C(4), UINT64_C(96), UINT64_C(4),
	  UINT64_C(0) },
	{ "wake_cpu", UINT64_C(4), UINT64_C(100), UINT64_C(4), UINT64_C(0) },
	{ "on_rq", UINT64_C(4), UINT64_C(104), UINT64_C(4), UINT64_C(0) },
	{ "prio", UINT64_C(4), UINT64_C(108), UINT64_C(4), UINT64_C(0) },
	{ "static_prio", UINT64_C(4), UINT64_C(112), UINT64_C(4), UINT64_C(0) },
	{ "normal_prio", UINT64_C(4), UINT64_C(116), UINT64_C(4), UINT64_C(0) },
	{ "rt_priority", UINT64_C(4), UINT64_C(120), UINT64_C(4), UINT64_C(0) },
	{ "se", UINT64_C(320), UINT64_C(128), UINT64_C(64), UINT64_C(1) },
	{ "rt", UINT64_C(48), UINT64_C(448), UINT64_C(8), UINT64_C(1) },
	{ "dl", UINT64_C(272), UINT64_C(496), UINT64_C(8), UINT64_C(0) },
	{ "dl_server", UINT64_C(8), UINT64_C(768), UINT64_C(8), UINT64_C(0) },
	{ "sched_class", UINT64_C(8), UINT64_C(776), UINT64_C(8), UINT64_C(0) },
	{ "sched_task_group", UINT64_C(8), UINT64_C(784), UINT64_C(8),
	  UINT64_C(0) },
	{ "stats", UINT64_C(256), UINT64_C(832), UINT64_C(64), UINT64_C(0) },
	{ "btrace_seq", UINT64_C(4), UINT64_C(1088), UINT64_C(4), UINT64_C(0) },
	{ "policy", UINT64_C(4), UINT64_C(1092), UINT64_C(4), UINT64_C(0) },
	{ "max_allowed_capacity", UINT64_C(8), UINT64_C(1096), UINT64_C(8),
	  UINT64_C(0) },
	{ "nr_cpus_allowed", UINT64_C(4), UINT64_C(1104), UINT64_C(4),
	  UINT64_C(0) },
	{ "cpus_ptr", UINT64_C(8), UINT64_C(1112), UINT64_C(8), UINT64_C(0) },
	{ "user_cpus_ptr", UINT64_C(8), UINT64_C(1120), UINT64_C(8),
	  UINT64_C(0) },
	{ "cpus_mask", UINT64_C(8), UINT64_C(1128), UINT64_C(8), UINT64_C(1) },
	{ "migration_pending", UINT64_C(8), UINT64_C(1136), UINT64_C(8),
	  UINT64_C(0) },
	{ "migration_disabled", UINT64_C(2), UINT64_C(1144), UINT64_C(2),
	  UINT64_C(0) },
	{ "migration_flags", UINT64_C(2), UINT64_C(1146), UINT64_C(2),
	  UINT64_C(0) },
	{ "rcu_read_lock_nesting", UINT64_C(4), UINT64_C(1148), UINT64_C(4),
	  UINT64_C(1) },
	{ "rcu_read_unlock_special", UINT64_C(4), UINT64_C(1152), UINT64_C(4),
	  UINT64_C(1) },
	{ "rcu_node_entry", UINT64_C(16), UINT64_C(1160), UINT64_C(8),
	  UINT64_C(1) },
	{ "rcu_blocked_node", UINT64_C(8), UINT64_C(1176), UINT64_C(8),
	  UINT64_C(1) },
	{ "rcu_tasks_nvcsw", UINT64_C(8), UINT64_C(1184), UINT64_C(8),
	  UINT64_C(0) },
	{ "rcu_tasks_holdout", UINT64_C(1), UINT64_C(1192), UINT64_C(1),
	  UINT64_C(0) },
	{ "rcu_tasks_idx", UINT64_C(1), UINT64_C(1193), UINT64_C(1),
	  UINT64_C(0) },
	{ "rcu_tasks_idle_cpu", UINT64_C(4), UINT64_C(1196), UINT64_C(4),
	  UINT64_C(0) },
	{ "rcu_tasks_holdout_list", UINT64_C(16), UINT64_C(1200), UINT64_C(8),
	  UINT64_C(1) },
	{ "rcu_tasks_exit_cpu", UINT64_C(4), UINT64_C(1216), UINT64_C(4),
	  UINT64_C(0) },
	{ "rcu_tasks_exit_list", UINT64_C(16), UINT64_C(1224), UINT64_C(8),
	  UINT64_C(1) },
	{ "trc_reader_nesting", UINT64_C(4), UINT64_C(1240), UINT64_C(4),
	  UINT64_C(0) },
	{ "trc_reader_scp", UINT64_C(8), UINT64_C(1248), UINT64_C(8),
	  UINT64_C(0) },
	{ "sched_info", UINT64_C(64), UINT64_C(1256), UINT64_C(8),
	  UINT64_C(0) },
	{ "tasks", UINT64_C(16), UINT64_C(1320), UINT64_C(8), UINT64_C(1) },
	{ "pushable_tasks", UINT64_C(40), UINT64_C(1336), UINT64_C(8),
	  UINT64_C(1) },
	{ "pushable_dl_tasks", UINT64_C(24), UINT64_C(1376), UINT64_C(8),
	  UINT64_C(0) },
	{ "mm", UINT64_C(8), UINT64_C(1400), UINT64_C(8), UINT64_C(0) },
	{ "active_mm", UINT64_C(8), UINT64_C(1408), UINT64_C(8), UINT64_C(0) },
	{ "exit_state", UINT64_C(4), UINT64_C(1416), UINT64_C(4), UINT64_C(0) },
	{ "exit_code", UINT64_C(4), UINT64_C(1420), UINT64_C(4), UINT64_C(0) },
	{ "exit_signal", UINT64_C(4), UINT64_C(1424), UINT64_C(4),
	  UINT64_C(0) },
	{ "pdeath_signal", UINT64_C(4), UINT64_C(1428), UINT64_C(4),
	  UINT64_C(0) },
	{ "jobctl", UINT64_C(8), UINT64_C(1432), UINT64_C(8), UINT64_C(0) },
	{ "personality", UINT64_C(4), UINT64_C(1440), UINT64_C(4),
	  UINT64_C(0) },
	{ "sched_reset_on_fork+sched_contributes_to_load+sched_migrated+sched_task_hot",
	  UINT64_C(1), UINT64_C(1444), UINT64_C(1), UINT64_C(1) },
	{ "<anonymous>", UINT64_C(0), UINT64_C(1448), UINT64_C(4),
	  UINT64_C(1) },
	{ "sched_remote_wakeup+sched_rt_mutex+user_dumpable+in_execve+in_iowait+restore_sigmask+no_cgroup_migration+frozen",
	  UINT64_C(1), UINT64_C(1448), UINT64_C(1), UINT64_C(1) },
	{ "use_memdelay+in_eventfd+pasid_activated+reported_split_lock+in_thrashing+in_nf_duplicate",
	  UINT64_C(1), UINT64_C(1449), UINT64_C(1), UINT64_C(1) },
	{ "atomic_flags", UINT64_C(8), UINT64_C(1456), UINT64_C(8),
	  UINT64_C(0) },
	{ "restart_block", UINT64_C(56), UINT64_C(1464), UINT64_C(8),
	  UINT64_C(0) },
	{ "pid", UINT64_C(4), UINT64_C(1520), UINT64_C(4), UINT64_C(0) },
	{ "tgid", UINT64_C(4), UINT64_C(1524), UINT64_C(4), UINT64_C(0) },
	{ "stack_canary", UINT64_C(8), UINT64_C(1528), UINT64_C(8),
	  UINT64_C(1) },
	{ "real_parent", UINT64_C(8), UINT64_C(1536), UINT64_C(8),
	  UINT64_C(0) },
	{ "parent", UINT64_C(8), UINT64_C(1544), UINT64_C(8), UINT64_C(0) },
	{ "children", UINT64_C(16), UINT64_C(1552), UINT64_C(8), UINT64_C(1) },
	{ "sibling", UINT64_C(16), UINT64_C(1568), UINT64_C(8), UINT64_C(1) },
	{ "group_leader", UINT64_C(8), UINT64_C(1584), UINT64_C(8),
	  UINT64_C(0) },
	{ "ptraced", UINT64_C(16), UINT64_C(1592), UINT64_C(8), UINT64_C(1) },
	{ "ptrace_entry", UINT64_C(16), UINT64_C(1608), UINT64_C(8),
	  UINT64_C(1) },
	{ "thread_pid", UINT64_C(8), UINT64_C(1624), UINT64_C(8), UINT64_C(0) },
	{ "pid_links", UINT64_C(64), UINT64_C(1632), UINT64_C(8), UINT64_C(0) },
	{ "thread_node", UINT64_C(16), UINT64_C(1696), UINT64_C(8),
	  UINT64_C(1) },
	{ "vfork_done", UINT64_C(8), UINT64_C(1712), UINT64_C(8), UINT64_C(0) },
	{ "set_child_tid", UINT64_C(8), UINT64_C(1720), UINT64_C(8),
	  UINT64_C(0) },
	{ "clear_child_tid", UINT64_C(8), UINT64_C(1728), UINT64_C(8),
	  UINT64_C(0) },
	{ "worker_private", UINT64_C(8), UINT64_C(1736), UINT64_C(8),
	  UINT64_C(0) },
	{ "utime", UINT64_C(8), UINT64_C(1744), UINT64_C(8), UINT64_C(0) },
	{ "stime", UINT64_C(8), UINT64_C(1752), UINT64_C(8), UINT64_C(0) },
	{ "gtime", UINT64_C(8), UINT64_C(1760), UINT64_C(8), UINT64_C(0) },
	{ "prev_cputime", UINT64_C(24), UINT64_C(1768), UINT64_C(8),
	  UINT64_C(0) },
	{ "nvcsw", UINT64_C(8), UINT64_C(1792), UINT64_C(8), UINT64_C(0) },
	{ "nivcsw", UINT64_C(8), UINT64_C(1800), UINT64_C(8), UINT64_C(0) },
	{ "start_time", UINT64_C(8), UINT64_C(1808), UINT64_C(8), UINT64_C(0) },
	{ "start_boottime", UINT64_C(8), UINT64_C(1816), UINT64_C(8),
	  UINT64_C(0) },
	{ "min_flt", UINT64_C(8), UINT64_C(1824), UINT64_C(8), UINT64_C(0) },
	{ "maj_flt", UINT64_C(8), UINT64_C(1832), UINT64_C(8), UINT64_C(0) },
	{ "posix_cputimers", UINT64_C(80), UINT64_C(1840), UINT64_C(8),
	  UINT64_C(0) },
	{ "posix_cputimers_work", UINT64_C(48), UINT64_C(1920), UINT64_C(8),
	  UINT64_C(0) },
	{ "ptracer_cred", UINT64_C(8), UINT64_C(1968), UINT64_C(8),
	  UINT64_C(0) },
	{ "real_cred", UINT64_C(8), UINT64_C(1976), UINT64_C(8), UINT64_C(0) },
	{ "cred", UINT64_C(8), UINT64_C(1984), UINT64_C(8), UINT64_C(0) },
	{ "cached_requested_key", UINT64_C(8), UINT64_C(1992), UINT64_C(8),
	  UINT64_C(0) },
	{ "comm", UINT64_C(16), UINT64_C(2000), UINT64_C(1), UINT64_C(0) },
	{ "nameidata", UINT64_C(8), UINT64_C(2016), UINT64_C(8), UINT64_C(0) },
	{ "sysvsem", UINT64_C(8), UINT64_C(2024), UINT64_C(8), UINT64_C(0) },
	{ "sysvshm", UINT64_C(16), UINT64_C(2032), UINT64_C(8), UINT64_C(0) },
	{ "fs", UINT64_C(8), UINT64_C(2048), UINT64_C(8), UINT64_C(0) },
	{ "files", UINT64_C(8), UINT64_C(2056), UINT64_C(8), UINT64_C(0) },
	{ "io_uring", UINT64_C(8), UINT64_C(2064), UINT64_C(8), UINT64_C(0) },
	{ "io_uring_restrict", UINT64_C(8), UINT64_C(2072), UINT64_C(8),
	  UINT64_C(0) },
	{ "nsproxy", UINT64_C(8), UINT64_C(2080), UINT64_C(8), UINT64_C(0) },
	{ "signal", UINT64_C(8), UINT64_C(2088), UINT64_C(8), UINT64_C(0) },
	{ "sighand", UINT64_C(8), UINT64_C(2096), UINT64_C(8), UINT64_C(0) },
	{ "blocked", UINT64_C(8), UINT64_C(2104), UINT64_C(8), UINT64_C(0) },
	{ "real_blocked", UINT64_C(8), UINT64_C(2112), UINT64_C(8),
	  UINT64_C(0) },
	{ "saved_sigmask", UINT64_C(8), UINT64_C(2120), UINT64_C(8),
	  UINT64_C(0) },
	{ "pending", UINT64_C(24), UINT64_C(2128), UINT64_C(8), UINT64_C(1) },
	{ "sas_ss_sp", UINT64_C(8), UINT64_C(2152), UINT64_C(8), UINT64_C(0) },
	{ "sas_ss_size", UINT64_C(8), UINT64_C(2160), UINT64_C(8),
	  UINT64_C(0) },
	{ "sas_ss_flags", UINT64_C(4), UINT64_C(2168), UINT64_C(4),
	  UINT64_C(0) },
	{ "task_works", UINT64_C(8), UINT64_C(2176), UINT64_C(8), UINT64_C(0) },
	{ "audit_context", UINT64_C(8), UINT64_C(2184), UINT64_C(8),
	  UINT64_C(0) },
	{ "loginuid", UINT64_C(4), UINT64_C(2192), UINT64_C(4), UINT64_C(0) },
	{ "sessionid", UINT64_C(4), UINT64_C(2196), UINT64_C(4), UINT64_C(0) },
	{ "seccomp", UINT64_C(16), UINT64_C(2200), UINT64_C(8), UINT64_C(0) },
	{ "syscall_dispatch", UINT64_C(32), UINT64_C(2216), UINT64_C(8),
	  UINT64_C(0) },
	{ "parent_exec_id", UINT64_C(8), UINT64_C(2248), UINT64_C(8),
	  UINT64_C(0) },
	{ "self_exec_id", UINT64_C(8), UINT64_C(2256), UINT64_C(8),
	  UINT64_C(0) },
	{ "alloc_lock", UINT64_C(4), UINT64_C(2264), UINT64_C(4), UINT64_C(0) },
	{ "pi_lock", UINT64_C(4), UINT64_C(2268), UINT64_C(4), UINT64_C(0) },
	{ "wake_q", UINT64_C(8), UINT64_C(2272), UINT64_C(8), UINT64_C(0) },
	{ "pi_waiters", UINT64_C(16), UINT64_C(2280), UINT64_C(8),
	  UINT64_C(0) },
	{ "pi_top_task", UINT64_C(8), UINT64_C(2296), UINT64_C(8),
	  UINT64_C(0) },
	{ "pi_blocked_on", UINT64_C(8), UINT64_C(2304), UINT64_C(8),
	  UINT64_C(0) },
	{ "blocked_on", UINT64_C(8), UINT64_C(2312), UINT64_C(8), UINT64_C(0) },
	{ "blocked_lock", UINT64_C(4), UINT64_C(2320), UINT64_C(4),
	  UINT64_C(0) },
	{ "journal_info", UINT64_C(8), UINT64_C(2328), UINT64_C(8),
	  UINT64_C(0) },
	{ "bio_list", UINT64_C(8), UINT64_C(2336), UINT64_C(8), UINT64_C(0) },
	{ "plug", UINT64_C(8), UINT64_C(2344), UINT64_C(8), UINT64_C(0) },
	{ "reclaim_state", UINT64_C(8), UINT64_C(2352), UINT64_C(8),
	  UINT64_C(0) },
	{ "io_context", UINT64_C(8), UINT64_C(2360), UINT64_C(8), UINT64_C(0) },
	{ "capture_control", UINT64_C(8), UINT64_C(2368), UINT64_C(8),
	  UINT64_C(0) },
	{ "ptrace_message", UINT64_C(8), UINT64_C(2376), UINT64_C(8),
	  UINT64_C(0) },
	{ "last_siginfo", UINT64_C(8), UINT64_C(2384), UINT64_C(8),
	  UINT64_C(0) },
	{ "ioac", UINT64_C(56), UINT64_C(2392), UINT64_C(8), UINT64_C(0) },
	{ "acct_rss_mem1", UINT64_C(8), UINT64_C(2448), UINT64_C(8),
	  UINT64_C(0) },
	{ "acct_vm_mem1", UINT64_C(8), UINT64_C(2456), UINT64_C(8),
	  UINT64_C(0) },
	{ "acct_timexpd", UINT64_C(8), UINT64_C(2464), UINT64_C(8),
	  UINT64_C(0) },
	{ "mems_allowed", UINT64_C(8), UINT64_C(2472), UINT64_C(8),
	  UINT64_C(0) },
	{ "mems_allowed_seq", UINT64_C(4), UINT64_C(2480), UINT64_C(4),
	  UINT64_C(0) },
	{ "cpuset_mem_spread_rotor", UINT64_C(4), UINT64_C(2484), UINT64_C(4),
	  UINT64_C(0) },
	{ "cgroups", UINT64_C(8), UINT64_C(2488), UINT64_C(8), UINT64_C(0) },
	{ "cg_list", UINT64_C(16), UINT64_C(2496), UINT64_C(8), UINT64_C(1) },
	{ "robust_list", UINT64_C(8), UINT64_C(2512), UINT64_C(8),
	  UINT64_C(0) },
	{ "compat_robust_list", UINT64_C(8), UINT64_C(2520), UINT64_C(8),
	  UINT64_C(0) },
	{ "pi_state_list", UINT64_C(16), UINT64_C(2528), UINT64_C(8),
	  UINT64_C(0) },
	{ "pi_state_cache", UINT64_C(8), UINT64_C(2544), UINT64_C(8),
	  UINT64_C(0) },
	{ "futex_exit_mutex", UINT64_C(24), UINT64_C(2552), UINT64_C(8),
	  UINT64_C(0) },
	{ "futex_state", UINT64_C(4), UINT64_C(2576), UINT64_C(4),
	  UINT64_C(0) },
	{ "perf_recursion", UINT64_C(4), UINT64_C(2580), UINT64_C(1),
	  UINT64_C(0) },
	{ "perf_event_ctxp", UINT64_C(8), UINT64_C(2584), UINT64_C(8),
	  UINT64_C(0) },
	{ "perf_event_mutex", UINT64_C(24), UINT64_C(2592), UINT64_C(8),
	  UINT64_C(1) },
	{ "perf_event_list", UINT64_C(16), UINT64_C(2616), UINT64_C(8),
	  UINT64_C(1) },
	{ "perf_ctx_data", UINT64_C(8), UINT64_C(2632), UINT64_C(8),
	  UINT64_C(0) },
	{ "mempolicy", UINT64_C(8), UINT64_C(2640), UINT64_C(8), UINT64_C(0) },
	{ "il_prev", UINT64_C(2), UINT64_C(2648), UINT64_C(2), UINT64_C(0) },
	{ "il_weight", UINT64_C(1), UINT64_C(2650), UINT64_C(1), UINT64_C(0) },
	{ "pref_node_fork", UINT64_C(2), UINT64_C(2652), UINT64_C(2),
	  UINT64_C(0) },
	{ "rseq", UINT64_C(40), UINT64_C(2656), UINT64_C(8), UINT64_C(0) },
	{ "mm_cid", UINT64_C(24), UINT64_C(2696), UINT64_C(8), UINT64_C(0) },
	{ "tlb_ubc", UINT64_C(24), UINT64_C(2720), UINT64_C(8), UINT64_C(0) },
	{ "splice_pipe", UINT64_C(8), UINT64_C(2744), UINT64_C(8),
	  UINT64_C(0) },
	{ "task_frag", UINT64_C(16), UINT64_C(2752), UINT64_C(8), UINT64_C(0) },
	{ "delays", UINT64_C(8), UINT64_C(2768), UINT64_C(8), UINT64_C(0) },
	{ "nr_dirtied", UINT64_C(4), UINT64_C(2776), UINT64_C(4), UINT64_C(0) },
	{ "nr_dirtied_pause", UINT64_C(4), UINT64_C(2780), UINT64_C(4),
	  UINT64_C(0) },
	{ "dirty_paused_when", UINT64_C(8), UINT64_C(2784), UINT64_C(8),
	  UINT64_C(0) },
	{ "timer_slack_ns", UINT64_C(8), UINT64_C(2792), UINT64_C(8),
	  UINT64_C(0) },
	{ "default_timer_slack_ns", UINT64_C(8), UINT64_C(2800), UINT64_C(8),
	  UINT64_C(0) },
	{ "trace_recursion", UINT64_C(8), UINT64_C(2808), UINT64_C(8),
	  UINT64_C(0) },
	{ "throttle_disk", UINT64_C(8), UINT64_C(2816), UINT64_C(8),
	  UINT64_C(0) },
	{ "utask", UINT64_C(8), UINT64_C(2824), UINT64_C(8), UINT64_C(0) },
	{ "kmap_ctrl", UINT64_C(0), UINT64_C(2832), UINT64_C(1), UINT64_C(0) },
	{ "rcu", UINT64_C(16), UINT64_C(2832), UINT64_C(8), UINT64_C(0) },
	{ "rcu_users", UINT64_C(4), UINT64_C(2848), UINT64_C(4), UINT64_C(0) },
	{ "pagefault_disabled", UINT64_C(4), UINT64_C(2852), UINT64_C(4),
	  UINT64_C(0) },
	{ "oom_reaper_list", UINT64_C(8), UINT64_C(2856), UINT64_C(8),
	  UINT64_C(0) },
	{ "oom_reaper_timer", UINT64_C(40), UINT64_C(2864), UINT64_C(8),
	  UINT64_C(0) },
	{ "stack_vm_area", UINT64_C(8), UINT64_C(2904), UINT64_C(8),
	  UINT64_C(0) },
	{ "stack_refcount", UINT64_C(4), UINT64_C(2912), UINT64_C(4),
	  UINT64_C(0) },
	{ "security", UINT64_C(8), UINT64_C(2920), UINT64_C(8), UINT64_C(0) },
	{ "bpf_net_context", UINT64_C(8), UINT64_C(2928), UINT64_C(8),
	  UINT64_C(0) },
	{ "mce_vaddr", UINT64_C(8), UINT64_C(2936), UINT64_C(8), UINT64_C(0) },
	{ "mce_kflags", UINT64_C(8), UINT64_C(2944), UINT64_C(8), UINT64_C(0) },
	{ "mce_addr", UINT64_C(8), UINT64_C(2952), UINT64_C(8), UINT64_C(0) },
	{ "mce_ripv+mce_whole_page+__mce_reserved", UINT64_C(8), UINT64_C(2960),
	  UINT64_C(1), UINT64_C(1) },
	{ "mce_kill_me", UINT64_C(16), UINT64_C(2968), UINT64_C(8),
	  UINT64_C(0) },
	{ "mce_count", UINT64_C(4), UINT64_C(2984), UINT64_C(4), UINT64_C(0) },
	{ "kretprobe_instances", UINT64_C(8), UINT64_C(2992), UINT64_C(8),
	  UINT64_C(0) },
	{ "rethooks", UINT64_C(8), UINT64_C(3000), UINT64_C(8), UINT64_C(0) },
	{ "l1d_flush_kill", UINT64_C(16), UINT64_C(3008), UINT64_C(8),
	  UINT64_C(0) },
	{ "unwind_info", UINT64_C(40), UINT64_C(3024), UINT64_C(8),
	  UINT64_C(0) },
	{ "thread", UINT64_C(152), UINT64_C(3064), UINT64_C(8), UINT64_C(1) },
};

static const spslr_target_layout task_struct_layout = {
	UINT64_C(3264),
	UINT64_C(201),
	task_struct_fields,
};

static const spslr_target task_struct_targets[] = {
	{
		{ 0xc2, 0xd1, 0xdd, 0x74, 0xed, 0x39, 0x06, 0x5e, 0xaf, 0x50,
		  0xc4, 0xcf, 0x8f, 0x38, 0x14, 0x05 },
		"task_struct",
		&task_struct_layout,
	},
};

extern "C" {
const spslr_target *spslr_targets = task_struct_targets;
spslr_u64 spslr_target_cnt =
	sizeof(task_struct_targets) / sizeof(task_struct_targets[0]);
}

namespace
{
void dump_layout_context(spslr_u64 failing_index)
{
	const spslr_u64 begin = failing_index > 2 ? failing_index - 2 : 0;
	const spslr_u64 end = failing_index + 3 < task_struct_layout.field_cnt ?
				      failing_index + 3 :
				      task_struct_layout.field_cnt;

	std::fprintf(stderr, "final layout around field index %" PRIu64 ":\n",
		     failing_index);
	for (spslr_u64 i = begin; i < end; ++i) {
		spslr_randomizer_field_info info{};
		if (spslr_randomizer_get_field(
			    0, i, SPSLR_RANDOMIZER_FIELD_IDX_MODE_FINAL,
			    &info) != 0) {
			std::fprintf(stderr, "  [%" PRIu64 "] <query failed>\n",
				     i);
			continue;
		}

		std::fprintf(stderr,
			     "  [%" PRIu64 "] offset=%" PRIu64 " size=%" PRIu64
			     " align=%" PRIu64 " initial=%" PRIu64
			     " flags=%" PRIu64 "\n",
			     i, info.offset, info.size, info.alignment,
			     info.initial_offset, info.flags);
	}
}

int explain_validation_failure()
{
	spslr_u64 target_size = 0;
	spslr_u64 field_count = 0;
	if (spslr_randomizer_get_target(0, &target_size, &field_count) != 0)
		return -1;

	spslr_u64 current_end = 0;
	for (spslr_u64 i = 0; i < field_count; ++i) {
		spslr_randomizer_field_info info{};
		if (spslr_randomizer_get_field(
			    0, i, SPSLR_RANDOMIZER_FIELD_IDX_MODE_FINAL,
			    &info) != 0) {
			std::fprintf(stderr,
				     "field query failed at index %" PRIu64
				     "\n",
				     i);
			return -1;
		}

		const char *reason = nullptr;
		if (info.offset < current_end)
			reason = "overlap or final-order violation";
		else if (info.alignment == 0 ||
			 info.offset % info.alignment != 0)
			reason = "alignment violation";
		else if ((info.flags & SPSLR_FLAG_FIELD_FIXED) != 0 &&
			 info.offset != info.initial_offset)
			reason = "fixed field moved";
		else if (info.offset > target_size ||
			 info.size > target_size - info.offset)
			reason = "field exceeds target size";

		if (reason != nullptr) {
			std::fprintf(
				stderr,
				"validation failure at final index %" PRIu64
				": %s (previous end=%" PRIu64 ")\n",
				i, reason, current_end);
			dump_layout_context(i);
			return 1;
		}

		current_end = info.offset + info.size;
	}

	std::fprintf(
		stderr,
		"validator returned failure, but the diagnostic pass found none\n");
	return 1;
}
} // namespace

int main(int argc, char **argv)
{
	std::uint64_t iterations = 10000;
	if (argc >= 2)
		iterations = std::strtoull(argv[1], nullptr, 0);
	if (argc >= 3)
		random_state = std::strtoull(argv[2], nullptr, 0);

	if (spslr_randomizer_init() != 0) {
		std::fprintf(stderr, "spslr_randomizer_init failed\n");
		return EXIT_FAILURE;
	}

	if (spslr_randomizer_validate_target(0) != 0) {
		std::fprintf(stderr, "original JSON layout is invalid\n");
		return EXIT_FAILURE;
	}

	for (std::uint64_t iteration = 1; iteration <= iterations;
	     ++iteration) {
		if (spslr_randomize() != 0) {
			std::fprintf(
				stderr,
				"spslr_randomize failed at iteration %" PRIu64
				"\n",
				iteration);
			return EXIT_FAILURE;
		}

		if (spslr_randomizer_validate_target(0) != 0) {
			std::fprintf(
				stderr,
				"seed-state=0x%016" PRIx64
				", validation failed after iteration %" PRIu64
				"\n",
				random_state, iteration);
			explain_validation_failure();
			return EXIT_FAILURE;
		}
	}

	std::printf("validated %" PRIu64 " randomized task_struct layouts\n",
		    iterations);
	return EXIT_SUCCESS;
}
