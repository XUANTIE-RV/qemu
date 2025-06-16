/*
 * Copyright (C) 2019, Alex Bennée <alex.bennee@linaro.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static bool do_inline;

/* Plugins need to take care of their own locking */
static GMutex lock;
static GHashTable *hotblocks;
static GHashTable *hotfuncs;
static guint64 bb_limit = 20, func_limit = 20;
struct qemu_plugin_scoreboard *total;
static gchar *func_filter = NULL;

/*
 * Counting Structure
 *
 * The internals of the TCG are not exposed to plugins so we can only
 * get the starting PC for each block. We cheat this slightly by
 * xor'ing the number of instructions to the hash to help
 * differentiate.
 */

typedef struct {
    uint64_t entry;
    struct qemu_plugin_scoreboard *calls;
    struct qemu_plugin_scoreboard *total;
    char *symbol;
} FuncCount;

typedef struct {
    uint64_t start_addr;
    struct qemu_plugin_scoreboard *exec_count;
    struct qemu_plugin_scoreboard *total_count;
    int trans_count;
    unsigned long insns;
    char *symbol;
    FuncCount *f;
} ExecCount;


static gint cmp_exec_rate_count(gconstpointer a, gconstpointer b)
{
    ExecCount *ea = (ExecCount *) a;
    ExecCount *eb = (ExecCount *) b;
    uint64_t count_a =
        qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(ea->exec_count));
    uint64_t count_b =
        qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(eb->exec_count));
    uint64_t total_a = qemu_plugin_u64_sum(
                           qemu_plugin_scoreboard_u64(ea->total_count));
    uint64_t total_b = qemu_plugin_u64_sum(
                           qemu_plugin_scoreboard_u64(eb->total_count));
    if (count_a > count_b) {
        return -1;
    } else if (count_a < count_b) {
        return 1;
    } else {
        return total_a > total_b ? -1 : 1;
    }
}

static gint cmp_rate_exec_count(gconstpointer a, gconstpointer b)
{
    ExecCount *ea = (ExecCount *) a;
    ExecCount *eb = (ExecCount *) b;
    uint64_t count_a =
        qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(ea->exec_count));
    uint64_t count_b =
        qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(eb->exec_count));
    uint64_t total_a = qemu_plugin_u64_sum(
                           qemu_plugin_scoreboard_u64(ea->total_count));
    uint64_t total_b = qemu_plugin_u64_sum(
                           qemu_plugin_scoreboard_u64(eb->total_count));
    if (total_a > total_b) {
        return -1;
    } else if (total_a < total_b) {
        return 1;
    } else {
        return count_a > count_b ? -1 : 1;
    }
}

static gint cmp_func_count(gconstpointer a, gconstpointer b)
{
    FuncCount *ea = (FuncCount *) a;
    FuncCount *eb = (FuncCount *) b;
    uint64_t count_a =
        qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(ea->total));
    uint64_t count_b =
        qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(eb->total));
    return count_a > count_b ? -1 : 1;
}

static void exec_count_free(gpointer key, gpointer value, gpointer user_data)
{
    ExecCount *cnt = value;
    qemu_plugin_scoreboard_free(cnt->exec_count);
    qemu_plugin_scoreboard_free(cnt->total_count);
}

static void func_count_free(gpointer key, gpointer value, gpointer user_data)
{
    FuncCount *cnt = value;
    qemu_plugin_scoreboard_free(cnt->calls);
    qemu_plugin_scoreboard_free(cnt->total);
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) report_exec = g_string_new("");
    g_autoptr(GString) report_rate = g_string_new("");
    g_autoptr(GString) freport = g_string_new("");
    g_autoptr(GString) ireport = g_string_new("");

    GList *counts, *it;
    int i;

    uint64_t total_insn = qemu_plugin_u64_sum(
                            qemu_plugin_scoreboard_u64(total));
    g_string_append_printf(ireport, "---------------------------------------\n");
    g_string_append_printf(ireport, "Total instructions number: %ld\n", total_insn);
    qemu_plugin_outs(ireport->str);

    g_string_append_printf(report_exec, "---------------------------------------\n");
    g_string_append_printf(report_exec, "Total blocks number: %u\n",
                           g_hash_table_size(hotblocks));
    counts = g_hash_table_get_values(hotblocks);
    it = g_list_sort(counts, cmp_exec_rate_count);

    g_string_append_printf(report_exec, ">>>> Sort by (ecount, rate)\n");

    if (it) {
        g_string_append_printf(report_exec, "%-19s %9s %9s %17s %7s %s\n",
                               "pc", "tcount", "icount", "ecount", "rate(\%)",
                               "symbol");

        for (i = 0; i < bb_limit && it->next; i++, it = it->next) {
            ExecCount *rec = (ExecCount *) it->data;
            if (func_filter && (g_strcmp0(rec->symbol, func_filter) != 0)) {
                continue;
            }
            uint64_t ecount = qemu_plugin_u64_sum(
                    qemu_plugin_scoreboard_u64(rec->exec_count));
            uint64_t bb_total = qemu_plugin_u64_sum(
                                qemu_plugin_scoreboard_u64(rec->total_count));
            float rate = 100 * bb_total / (float)total_insn;
            g_string_append_printf(
                report_exec, "0x%016"PRIx64", %8d, %8ld, %16"PRId64", %6.2f %s\n",
                rec->start_addr, rec->trans_count,
                rec->insns, ecount, rate, rec->symbol);
        }

        g_list_free(it);
    }

    qemu_plugin_outs(report_exec->str);

    counts = g_hash_table_get_values(hotblocks);
    it = g_list_sort(counts, cmp_rate_exec_count);
    g_string_append_printf(report_rate, ">>>> Sort by (rate, ecount)\n");

    if (it) {
        g_string_append_printf(report_rate, "%-19s %9s %9s %17s %7s %s\n",
                               "pc", "tcount", "icount", "ecount", "rate(\%)",
                               "symbol");

        for (i = 0; i < bb_limit && it->next; i++, it = it->next) {
            ExecCount *rec = (ExecCount *) it->data;
            if (func_filter && (g_strcmp0(rec->symbol, func_filter) != 0)) {
                continue;
            }
            uint64_t ecount = qemu_plugin_u64_sum(
                    qemu_plugin_scoreboard_u64(rec->exec_count));
            uint64_t bb_total = qemu_plugin_u64_sum(
                                qemu_plugin_scoreboard_u64(rec->total_count));
            float rate = 100 * bb_total / (float)total_insn;
            g_string_append_printf(
                report_rate, "0x%016"PRIx64", %8d, %8ld, %16"PRId64", %6.2f, %s\n",
                rec->start_addr, rec->trans_count,
                rec->insns, ecount, rate, rec->symbol);
        }

        g_list_free(it);
    }
    g_string_append_printf(report_rate, "---------------------------------------\n");
    qemu_plugin_outs(report_rate->str);

    g_string_append_printf(freport, "Total functions number: %d\n",
                           g_hash_table_size(hotfuncs));
    counts = g_hash_table_get_values(hotfuncs);
    it = g_list_sort(counts, cmp_func_count);

    if (it) {
        g_string_append_printf(freport, "%-19s %17s %17s %4s %s\n",
                               "entry", "calls", "total", "rate(\%)", "symbol");

        for (i = 0; i < func_limit && it->next; i++, it = it->next) {
            FuncCount *rec = (FuncCount *) it->data;
            uint64_t func_calls = qemu_plugin_u64_sum(
                    qemu_plugin_scoreboard_u64(rec->calls));
            uint64_t  func_total = qemu_plugin_u64_sum(
                    qemu_plugin_scoreboard_u64(rec->total));
            g_string_append_printf(
                freport, "0x%016"PRIx64", %16ld, %16"PRId64", %6.2f, %s\n",
                rec->entry, func_calls, func_total,
                100 * func_total / (float)total_insn, rec->symbol);
        }

        g_list_free(it);
    }
    g_string_append_printf(freport, "---------------------------------------\n");
    qemu_plugin_outs(freport->str);

    g_hash_table_foreach(hotblocks, exec_count_free, NULL);
    g_hash_table_destroy(hotblocks);
    g_hash_table_foreach(hotfuncs, func_count_free, NULL);
    g_hash_table_destroy(hotfuncs);
    qemu_plugin_scoreboard_free(total);
}

static void plugin_init(void)
{
    hotblocks = g_hash_table_new(NULL, g_direct_equal);
    hotfuncs = g_hash_table_new(g_str_hash, g_str_equal);
    total = qemu_plugin_scoreboard_new(sizeof(uint64_t));
}

static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{
    ExecCount *cnt = (ExecCount *)udata;
    qemu_plugin_u64_add(qemu_plugin_scoreboard_u64(cnt->exec_count),
                        cpu_index, 1);
    qemu_plugin_u64_add(qemu_plugin_scoreboard_u64(cnt->total_count),
                        cpu_index, cnt->insns);
    if (cnt->start_addr == cnt->f->entry) {
        qemu_plugin_u64_add(qemu_plugin_scoreboard_u64(cnt->f->calls),
                            cpu_index, 1);
        qemu_plugin_u64_add(qemu_plugin_scoreboard_u64(cnt->f->total),
                            cpu_index, cnt->insns);
    } else {
        qemu_plugin_u64_add(qemu_plugin_scoreboard_u64(cnt->f->total),
                            cpu_index, cnt->insns);
    }
    qemu_plugin_u64_add(qemu_plugin_scoreboard_u64(total),
                        cpu_index, cnt->insns);

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
    size_t insns = qemu_plugin_tb_n_insns(tb);
    uint64_t hash = ((uint64_t)insns << 54) | ((pc << 9) >> 9);

    g_mutex_lock(&lock);
    cnt = (ExecCount *) g_hash_table_lookup(hotblocks, (gconstpointer) hash);
    if (cnt) {
        assert((pc == cnt->start_addr) && (cnt->insns == insns));
        cnt->trans_count++;
    } else {
        cnt = g_new0(ExecCount, 1);
        cnt->start_addr = pc;
        cnt->trans_count = 1;
        cnt->insns = insns;
        cnt->exec_count = qemu_plugin_scoreboard_new(sizeof(uint64_t));
        cnt->total_count = qemu_plugin_scoreboard_new(sizeof(uint64_t));
        cnt->symbol = g_strdup(qemu_plugin_insn_symbol(qemu_plugin_tb_get_insn(tb, 0)));
        if (cnt->symbol == NULL) {
            cnt->symbol = "__undefine";
        }
        FuncCount *fcnt = g_hash_table_lookup(hotfuncs, cnt->symbol);
        if (fcnt == NULL) {
            fcnt = g_new0(FuncCount, 1);
            fcnt->calls = qemu_plugin_scoreboard_new(sizeof(uint64_t));
            fcnt->total = qemu_plugin_scoreboard_new(sizeof(uint64_t));
            fcnt->entry = pc;
            fcnt->symbol = cnt->symbol;
            g_hash_table_insert(hotfuncs, cnt->symbol, (gpointer)fcnt);
        }
        cnt->f = fcnt;
        g_hash_table_insert(hotblocks, (gpointer) hash, (gpointer) cnt);
    }

    g_mutex_unlock(&lock);

    if (do_inline) {
        qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
            tb, QEMU_PLUGIN_INLINE_ADD_U64,
            qemu_plugin_scoreboard_u64(cnt->exec_count), 1);
    } else {
        qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             (void *)cnt);
    }
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "inline") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &do_inline)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "hotblocks") == 0) {
            bb_limit = g_ascii_strtoull(tokens[1], NULL, 10);
        } else if (g_strcmp0(tokens[0], "hotfuncs") == 0) {
            func_limit = g_ascii_strtoull(tokens[1], NULL, 10);
        } else if (g_strcmp0(tokens[0], "filter_by_func") == 0) {
            func_filter = g_strdup(tokens[1]);
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    plugin_init();

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
