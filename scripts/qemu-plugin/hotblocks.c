/* QEMU TCG plugin: count executed guest instructions per translation block, dump the table at exit.
 * Usage: qemu-system-x86_64 -accel tcg -plugin scripts/qemu-plugin/hotblocks.so,out=/tmp/hot.txt ...
 * Output lines: <vaddr hex> <n_insns> <exec_count>   (analyse with scripts/profile-tcg.py)
 * touch <out>.dump to write the current table without exiting (the plugin deletes the trigger file when done). */
#include <qemu-plugin.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct { uint64_t vaddr; uint32_t n; uint64_t count; } tb_t;
static GPtrArray *tbs; static GMutex lock; static char out_path[512] = "/tmp/hotblocks.txt";

static void exec_cb(unsigned int vcpu, void *udata) { __atomic_add_fetch(&((tb_t *)udata)->count, 1, __ATOMIC_RELAXED); }

static void tb_trans(struct qemu_plugin_tb *tb, void *ud)
{
	tb_t *t = g_new0(tb_t, 1);
	t->vaddr = qemu_plugin_tb_vaddr(tb); t->n = (uint32_t)qemu_plugin_tb_n_insns(tb);
	g_mutex_lock(&lock); g_ptr_array_add(tbs, t); g_mutex_unlock(&lock);
	qemu_plugin_register_vcpu_tb_exec_cb(tb, exec_cb, QEMU_PLUGIN_CB_NO_REGS, t);
}

static void dump(void)
{
	FILE *f = fopen(out_path, "w"); if (!f) return;
	g_mutex_lock(&lock);
	for (guint i = 0; i < tbs->len; i++) { tb_t *t = g_ptr_array_index(tbs, i); if (t->count) fprintf(f, "%llx %u %llu\n", (unsigned long long)t->vaddr, t->n, (unsigned long long)t->count); }
	g_mutex_unlock(&lock);
	fclose(f);
}
static void at_exit(void *p) { dump(); }
/* QEMU owns SIGUSR1, so a watcher thread dumps the table whenever <out>.dump appears (then deletes it) */
static gpointer watcher(gpointer d)
{
	char trig[600]; snprintf(trig, sizeof trig, "%s.dump", out_path);
	for (;;) { g_usleep(300000); if (g_file_test(trig, G_FILE_TEST_EXISTS)) { dump(); remove(trig); } }
	return NULL;
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info, int argc, char **argv)
{
	for (int i = 0; i < argc; i++) if (!strncmp(argv[i], "out=", 4)) snprintf(out_path, sizeof out_path, "%s", argv[i] + 4);
	tbs = g_ptr_array_new();
	g_thread_new("hotblocks-watch", watcher, NULL);
	qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans, NULL);
	qemu_plugin_register_atexit_cb(id, at_exit, NULL);
	return 0;
}
