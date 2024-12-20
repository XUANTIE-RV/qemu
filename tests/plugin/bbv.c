/*
 * Copyright (C) 2019, LIU Zhiwei <zhiwei_liu@c-sky.com>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define BBV_DEBUG 0

static uint64_t insn_count;

typedef enum ReplayMode {
     REPLAY_MODE_NONE,
     REPLAY_MODE_RECORD,
     REPLAY_MODE_PLAY,
     REPLAY_MODE__MAX,
} ReplayMode;

static ReplayMode  rr = REPLAY_MODE_NONE;

/* Plugins need to take care of their own locking */
static GMutex lock;
static GHashTable *hotblocks;
static guint64 limit = 20;
static guint64 total_cnt;
static guint64 interval = 10000000;
static FILE *ftrans;
static FILE *fexec;
static FILE *fnext;
static bool registered;
static int dim;

/*
 * Counting Structure
 *
 * The internals of the TCG are not exposed to plugins so we can only
 * get the starting PC for each block. We cheat this slightly by
 * xor'ing the number of instructions to the hash to help
 * differentiate.
 */
typedef struct {
    uint64_t start_addr;
    uint64_t exec_count;
    int      trans_count;
    unsigned long insns;
    int      dim;
} ExecCount;

static gint cmp_exec_count(gconstpointer a, gconstpointer b)
{
    ExecCount *ea = (ExecCount *) a;
    ExecCount *eb = (ExecCount *) b;
    return ea->exec_count > eb->exec_count ? -1 : 1;
}

static void plugin_init(void)
{
    gchar *exec, *trans, *next;
    hotblocks = g_hash_table_new(NULL, g_direct_equal);

    if (BBV_DEBUG) {
        exec = g_strdup_printf("exec%ld.txt", time(NULL));
        trans = g_strdup_printf("trans%ld.txt", time(NULL));
        next = g_strdup_printf("next%ld.txt", time(NULL));
        fexec = g_fopen(exec, "wb");
        ftrans = g_fopen(trans, "wb");
        fnext = g_fopen(next, "wb");
        g_free(exec);
        g_free(trans);
        g_free(next);
    }
}

static void reset_exec_count(gpointer key, gpointer value, gpointer user_data)
{
    ExecCount *ea = (ExecCount *)value;
    ea->exec_count = 0;
}

static void print_bbv(void)
{
    g_autoptr(GString) report = g_string_new("");
    GList *counts, *it;
    int i;

    g_string_append_printf(report, "T:");
    counts = g_hash_table_get_values(hotblocks);
    it = g_list_sort(counts, cmp_exec_count);

    if (it) {
        for (i = 0; i < limit && it->next; i++, it = it->next) {
            ExecCount *rec = (ExecCount *) it->data;
            if (rec->exec_count == 0) {
                break;
            }
            if (i != 0) {
                g_string_append_printf(report, " :");
            }
            g_string_append_printf(report, "%d:%"PRId64,
                                   rec->dim,
                                   rec->exec_count * rec->insns);
        }
        g_string_append_printf(report, "\n");
        g_list_free(it);
    }
    g_hash_table_foreach(hotblocks, reset_exec_count, NULL);
    qemu_plugin_outs(report->str);
}

static bool firstexec = true;
static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{
    ExecCount *cnt;
    uint64_t hash = (uint64_t) udata;

    g_mutex_lock(&lock);
    cnt = (ExecCount *) g_hash_table_lookup(hotblocks, (gconstpointer) hash);
    /* should always succeed */
    g_assert(cnt);
    cnt->exec_count++;
    total_cnt += cnt->insns;

    if (BBV_DEBUG) {
        insn_count += cnt->insns;
    }

    if (total_cnt > interval) {
        if (BBV_DEBUG) {
            g_fprintf(fexec, "tb: %lx, cnt: %ld\n", cnt->start_addr, insn_count);
        }
        if (rr == REPLAY_MODE_RECORD) {
            qemu_plugin_inject_simpoint(cnt->insns);
            if (BBV_DEBUG) {
                g_fprintf(fnext, "%lx\n", cnt->start_addr);
            }
        }

        print_bbv();
        total_cnt = 0;
    }
    if (firstexec) {
        if (rr == REPLAY_MODE_RECORD) {
            qemu_plugin_inject_simpoint(cnt->insns);
            if (BBV_DEBUG) {
                g_fprintf(fnext, "%lx\n", cnt->start_addr);
            }
        }
        firstexec = false;
    }
    g_mutex_unlock(&lock);
}

/*
 * When do_inline we ask the plugin to increment the counter for us.
 * Otherwise a helper is inserted which calls the vcpu_tb_exec
 * callback.
 */
static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    ExecCount *cnt;
    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    unsigned long insns = qemu_plugin_tb_n_insns(tb);
    uint64_t hash = ((uint64_t)insns << 52) | pc;

    if (BBV_DEBUG) {
        g_fprintf(ftrans, "tb: %lx, insn: %ld\n", pc, insns);
    }
    g_mutex_lock(&lock);
    cnt = (ExecCount *) g_hash_table_lookup(hotblocks, (gconstpointer) hash);
    if (cnt) {
        assert(pc == cnt->start_addr);
        assert(insns == cnt->insns);
        cnt->trans_count++;
    } else {
        cnt = g_new0(ExecCount, 1);
        cnt->start_addr = pc;
        cnt->trans_count = 1;
        cnt->insns = insns;
        cnt->dim = ++dim;
        g_hash_table_insert(hotblocks, (gpointer) hash, (gpointer) cnt);
    }

    g_mutex_unlock(&lock);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         (void *)hash);
}

/* When process is monitor process, record bbv */
static void monitor_process(qemu_plugin_id_t id)
{
    if (BBV_DEBUG) {
        fprintf(stderr, "register: XXX\n");
    }
    registered = true;
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
}

/* When process is not monitor process, don't record bbv */
static void other_process(qemu_plugin_id_t id)
{
    if (registered) {
        if (BBV_DEBUG) {
            fprintf(stderr, "unregister: XXX\n");
        }
        registered = false;
    }
    qemu_plugin_register_vcpu_tb_trans_cb(id, NULL);
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autofree gchar *out = g_strdup_printf("insns: %" PRIu64 "\n", insn_count);
    qemu_plugin_outs(out);
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    int i;

    for (i = 0; i < argc; i++) {
        char *opt = argv[i];
        if (g_str_has_prefix(opt, "rr=")) {
            if (g_strcmp0(opt + 3, "record") == 0) {
                rr = REPLAY_MODE_RECORD;
            } else {
                fprintf(stderr, "No avaliable replay mode %s\n", opt + 3);
                exit(1);
            }
        } else if (g_str_has_prefix(opt, "interval=")) {
            interval = g_ascii_strtoull(opt + 9, NULL, 10);
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    plugin_init();

    qemu_plugin_register_monitor_process_cb(id, monitor_process);
    qemu_plugin_register_other_process_cb(id, other_process);

    if (BBV_DEBUG) {
        qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    }
    return 0;
}
