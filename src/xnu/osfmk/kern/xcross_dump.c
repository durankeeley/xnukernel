/*
 * xcross_dump.c -- debugging aid for the cross-built kernel (not part of Apple's XNU).
 *
 * `sysctl -w debug.xcross_dump=<pid>` (0 = every process) prints, for each thread that is blocked in the kernel, one line on the
 * serial console:  XD t=<thread id> p=<pid>:<name> st=<state> ev=<wait event> : <rip> <ret> <ret> ...
 * The frames come from the saved context of a blocked thread (x86_kernel_state: rip + rbp chain on its kernel stack).
 * Addresses are printed raw (printf %p would be hidden); a header line gives the kernel text base and slide so they can be symbolised
 * offline (scripts/xd-symbolize.py).
 */
#include <kern/thread.h>
#include <kern/task.h>
#include <kern/processor.h>
#include <kern/queue.h>
#include <kern/locks.h>
#include <i386/thread.h>
#include <mach/vm_param.h>
#include <kern/misc_protos.h>

extern int scnprintf(char *, size_t, const char *, ...);

extern int proc_pid(void *);
extern void *proc_find(int pid);
extern int proc_rele(void *p);
extern task_t proc_task(void *p);
extern const char *proc_name_address(void *p);   /* reads p_comm; no locking (proc_name() does a locked proc_find) */
extern lck_mtx_t tasks_threads_lock;

void xcross_dump_threads(int pid_filter);

extern int xcross_kext_lookup(uint64_t addr, char *name, int len, uint64_t *off);   /* iokit/Kernel/xcross_osaction.cpp */

#define XD_NAMES 0x40000000   /* flag in the sysctl value: also resolve kext identifiers (takes the kext lock; may block) */

static int
xd_frame(char *buf, int n, int max, uint64_t addr, uint64_t stext, int names)
{
	char kn[96]; uint64_t koff = 0;
	if (names && addr > stext + 0x1000000 && xcross_kext_lookup(addr, kn, sizeof(kn), &koff)) {
		(void)koff;
		return n + scnprintf(buf + n, max - n, " %s@%llx", kn, addr - stext);
	}
	return n + scnprintf(buf + n, max - n, " %llx", addr - stext);   /* printf hides raw kernel addresses; offsets from the text base are not hidden */
}

/*
 * NOTE on list layout in this tree (see thread_create_internal): the queue links hold pointers to the OBJECTS (thread/task base), not to the
 * embedded link, and the list terminates at the head's own address. So: first = head->next (an object), next = obj->link.next, stop at &head.
 */
static void
xd_task_threads(task_t task, int pid, const char *name, uint64_t stext, int names, unsigned *nthreads, unsigned *nshown)
{
	thread_t self = current_thread();
	uint64_t head = (uint64_t)&task->threads;
	uint64_t cur = (uint64_t)task->threads.next;
	for (unsigned guard = 0; cur != head && cur != 0 && guard < 4096; guard++) {
		thread_t t = (thread_t)cur;
		cur = (uint64_t)t->task_threads.next;
		(*nthreads)++;
		if (t == self || t->kernel_stack == 0 || !(t->state & TH_WAIT)) {
			continue;
		}
		struct x86_kernel_state *iks = STACK_IKS(t->kernel_stack);
		uint64_t rip = iks->k_rip, rbp = iks->k_rbp;
		uint64_t lo = (uint64_t)t->kernel_stack, hi = lo + (uint64_t)kernel_stack_size - 16;
		uint64_t ev = (uint64_t)t->wait_event, evo = ev - stext;
		char line[900]; int n = 0;
		n += scnprintf(line + n, sizeof(line) - n, "XD t=%llx p=%d:%s st=%x ev=%llx :", (uint64_t)t->thread_id, pid, name,
		    (unsigned)t->state, evo < 0x10000000ULL ? evo : ev);
		n = xd_frame(line, n, sizeof(line), rip, stext, names);
		for (int i = 0; i < 40 && rbp >= lo && rbp < hi && (rbp & 7) == 0 && n < (int)sizeof(line) - 160; i++) {
			uint64_t ret = *(uint64_t *)(rbp + 8);
			n = xd_frame(line, n, sizeof(line), ret, stext, names);
			rbp = *(uint64_t *)rbp;
		}
		printf("%s\n", line);
		(*nshown)++;
	}
}

void
xcross_dump_threads(int arg)
{
	unsigned int nthreads = 0, nshown = 0, ntasks = 0;
	int names = (arg & XD_NAMES) != 0, pid_filter = arg & ~XD_NAMES;
	uint64_t stext = (uint64_t)vm_kernel_stext;

	printf("XD begin slide=%llx pid=%d names=%d\n", (uint64_t)vm_kernel_slide, pid_filter, names);
	bool locked = lck_mtx_try_lock(&tasks_threads_lock);
	printf("XD tasks_threads_lock %s\n", locked ? "acquired" : "HELD by someone else - walking without it");
	if (pid_filter) {
		void *bsd = proc_find(pid_filter);
		task_t task = bsd ? proc_task(bsd) : NULL;
		printf("XD proc_find(%d) = %s\n", pid_filter, bsd ? "found" : "NULL");
		if (task) {
			ntasks = 1;
			xd_task_threads(task, pid_filter, proc_name_address(bsd), stext, names, &nthreads, &nshown);
		}
		if (bsd) {
			proc_rele(bsd);
		}
	} else {
		uint64_t head = (uint64_t)&tasks;
		uint64_t cur = (uint64_t)tasks.next;
		for (unsigned guard = 0; cur != head && cur != 0 && guard < 5000; guard++) {
			task_t task = (task_t)cur;
			cur = (uint64_t)task->tasks.next;
			void *bsd = get_bsdtask_info(task);
			ntasks++;
			xd_task_threads(task, bsd ? proc_pid(bsd) : 0, bsd ? proc_name_address(bsd) : "kernel", stext, names, &nthreads, &nshown);
		}
	}
	if (locked) {
		lck_mtx_unlock(&tasks_threads_lock);
	}
	printf("XD end tasks=%u threads=%u shown=%u\n", ntasks, nthreads, nshown);
}
