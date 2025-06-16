#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <qemu-plugin.h>
#include <string.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;


/* Record the target address and its count.  */
typedef struct {
    uint64_t target_addr;
    struct qemu_plugin_scoreboard *count;
} target_record_t;

/* Record the jump instruction and its statistics.  */
typedef struct {
    /* The address of the jump instruction.  */
    uint64_t pc;
    GHashTable *target_record_table;
    /* The count of jumping targets.  */
    struct qemu_plugin_scoreboard *target_count;
    /* The count of executing this instruction.  */
    struct qemu_plugin_scoreboard *total_count;
    /* The register number of which the value is used as the target address.  */
    uint8_t source_reg;
} jump_stat_t;


#define MAX_REGISTERS 128

typedef struct {
    struct qemu_plugin_register *reg_handles[MAX_REGISTERS];
} CPU;

/* The hash table to store the jump instruction statistics.  */
static GHashTable *jump_stats_table;
/* The array to store the CPU related information.  */
static GArray *cpus;

static GMutex jump_stats_lock;
static GMutex target_record_lock;
static GRWLock cpu_array_lock;


static void target_record_table_init(jump_stat_t *stat)
{
    stat->target_record_table = g_hash_table_new(g_direct_hash, g_direct_equal);
}

static target_record_t *target_record_table_lookup(jump_stat_t *stat, uint64_t target)
{
    return g_hash_table_lookup(stat->target_record_table, GUINT_TO_POINTER(target));
}

static target_record_t *target_record_table_insert(jump_stat_t *stat, uint64_t target)
{
    target_record_t *record = g_new0(target_record_t, 1);
    record->target_addr = target;
    record->count = qemu_plugin_scoreboard_new(sizeof(uint64_t));
    g_hash_table_insert(stat->target_record_table, GUINT_TO_POINTER(target), record);
    return record;
}

static void target_record_table_free(gpointer key, gpointer value, gpointer user_data)
{
    target_record_t *record = (target_record_t *)value;
    qemu_plugin_scoreboard_free(record->count);
    g_free(record);
}

static void jump_stat_hash_init(void)
{
    jump_stats_table = g_hash_table_new(g_direct_hash, g_direct_equal);
}

static jump_stat_t *jump_stat_hash_lookup(uint64_t pc)
{
    return g_hash_table_lookup(jump_stats_table, GUINT_TO_POINTER(pc));
}

static jump_stat_t *jump_stat_hash_insert(uint64_t pc)
{
    jump_stat_t *stat = g_new0(jump_stat_t, 1);
    stat->pc = pc;
    stat->target_count = qemu_plugin_scoreboard_new(sizeof(uint64_t));
    stat->total_count = qemu_plugin_scoreboard_new(sizeof(uint64_t));
    target_record_table_init(stat);
    g_hash_table_insert(jump_stats_table, GUINT_TO_POINTER(pc), stat);
    return stat;
}

static void jump_stat_hash_free(gpointer key, gpointer value, gpointer user_data)
{
    jump_stat_t *stat = (jump_stat_t *)value;
    g_hash_table_foreach(stat->target_record_table, target_record_table_free, NULL);
    g_hash_table_destroy(stat->target_record_table);
    qemu_plugin_scoreboard_free(stat->target_count);
    qemu_plugin_scoreboard_free(stat->total_count);
    g_free(stat);
}

static gint cmp_jump_target_count(gconstpointer a, gconstpointer b)
{
    jump_stat_t *ta = (jump_stat_t *)a;
    jump_stat_t *tb = (jump_stat_t *)b;
    uint64_t count_a =
        qemu_plugin_u64_sum (qemu_plugin_scoreboard_u64(ta->total_count));
    uint64_t count_b =
        qemu_plugin_u64_sum (qemu_plugin_scoreboard_u64(tb->total_count));
    return count_a > count_b ? -1 : 1;
}

static gint cmp_target_record_count(gconstpointer a, gconstpointer b)
{
    target_record_t *ta = (target_record_t *)a;
    target_record_t *tb = (target_record_t *)b;
    uint64_t count_a =
        qemu_plugin_u64_sum (qemu_plugin_scoreboard_u64(ta->count));
    uint64_t count_b =
        qemu_plugin_u64_sum (qemu_plugin_scoreboard_u64(tb->count));
    return count_a > count_b ? -1 : 1;
}

/* Check if the instruction is an indirect jump.  */
static int is_indirect_jump(uint32_t insn)
{

#define RET16_OPCODE 0x8082
#define RET32_OPCODE 0x80c7

    if (insn == RET16_OPCODE || insn == RET32_OPCODE)
        return 0;

    return ((insn & 0x7f) == 0x67 && (insn & 0xfff00000) == 0)
           || ((insn & 0xf07f) == 0x8002)
           || ((insn & 0xf07f) == 0x9002);
}

/* Get the source register number.  */
static uint8_t get_rs1(uint32_t insn)
{
    if ((insn & 0x3) == 0x3)
      return (uint8_t)((insn >> 15) & 0x1f);
    else
      return (uint8_t)((insn >> 7) & 0x1f);
}

static void record_target(unsigned int vcpu_index, jump_stat_t *stat, uint64_t target)
{
    target_record_t *record = target_record_table_lookup(stat, target);

    if (!record)
    {
        g_mutex_lock(&target_record_lock);
        record = target_record_table_insert(stat, target);
        g_mutex_unlock(&target_record_lock);
        qemu_plugin_u64_add(qemu_plugin_scoreboard_u64(stat->target_count),
                            vcpu_index, 1);
    }

    qemu_plugin_u64_add(qemu_plugin_scoreboard_u64(record->count), vcpu_index, 1);
}

static void insn_callback(unsigned int vcpu_index, void *userdata)
{
    jump_stat_t *stat = (jump_stat_t *)userdata;
    CPU *cpu = &g_array_index(cpus, CPU, vcpu_index);
    g_autoptr(GByteArray) reg_value = g_byte_array_new();

    if (!stat) return;

    uint64_t target = 0;
    int count = qemu_plugin_read_register(cpu->reg_handles[stat->source_reg], reg_value);

    for (int i = 0; i < count; i++)
    {
        target |= (uint64_t)reg_value->data[i] << (i * 8);
    }

    record_target(vcpu_index, stat, target);
    qemu_plugin_u64_add(qemu_plugin_scoreboard_u64(stat->total_count),
                        vcpu_index, 1);
}

/* Check if the instruction is an indirect jump.
   And register the callback for the instruction if it is.  */
static void tb_callback(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t count = qemu_plugin_tb_n_insns(tb);

    struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, count - 1);
    uint64_t pc = qemu_plugin_insn_vaddr(insn);
    size_t sz = qemu_plugin_insn_size(insn);
    uint32_t insn_code;
    if (sz == 2)
        insn_code = *((uint16_t *)qemu_plugin_insn_data(insn));
    else
        insn_code = *((uint32_t *)qemu_plugin_insn_data(insn));

    if (is_indirect_jump(insn_code))
    {
        g_mutex_lock(&jump_stats_lock);
        jump_stat_t *stat = jump_stat_hash_lookup(pc);
        if (!stat)
            stat = jump_stat_hash_insert(pc);
        stat->source_reg = get_rs1(insn_code);
        g_mutex_unlock(&jump_stats_lock);
        qemu_plugin_register_vcpu_insn_exec_cb(insn, insn_callback, QEMU_PLUGIN_CB_R_REGS, (void *)stat);
    }
}

static void dump_results(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) report = g_string_new("");

    GList *counts, *it;

    counts = g_hash_table_get_values(jump_stats_table);
    it = g_list_sort(counts, cmp_jump_target_count);

    for (; it; it = it->next)
    {
        jump_stat_t *stat = (jump_stat_t *)it->data;
        uint64_t total_count =
            qemu_plugin_u64_sum (qemu_plugin_scoreboard_u64(stat->total_count));
        g_string_append_printf(report, "PC: 0x%" PRIx64 ", Total Count: %" PRIu64 "\n", stat->pc, total_count);

        GList *targets, *it2;
        targets = g_hash_table_get_values(stat->target_record_table);
        it2 = g_list_sort(targets, cmp_target_record_count);

        for (; it2; it2 = it2->next)
        {
            target_record_t *record = (target_record_t *)it2->data;
            uint64_t target_count =
                qemu_plugin_u64_sum (qemu_plugin_scoreboard_u64(record->count));
            g_string_append_printf(report, "  0x%" PRIx64 ", %17" PRIu64 ", %8.2f%%\n",
                                   record->target_addr, target_count,
                                   100.0 * target_count / total_count);
        }
        g_string_append_printf(report, "\n");
    }

    qemu_plugin_outs(report->str);

    g_hash_table_foreach(jump_stats_table, jump_stat_hash_free, NULL);
    g_hash_table_destroy(jump_stats_table);
    g_array_free(cpus, true);
}

static void plugin_init(const qemu_info_t *info)
{
    jump_stat_hash_init();
    cpus = g_array_sized_new(true, true, sizeof(CPU),
                             info->system_emulation ? info->system.max_vcpus : 1);
}

static void vcpu_init(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    CPU *cpu;

    g_rw_lock_writer_lock(&cpu_array_lock);
    if (vcpu_index >= cpus->len)
        g_array_set_size(cpus, vcpu_index + 1);
    g_rw_lock_writer_unlock(&cpu_array_lock);

    cpu = &g_array_index(cpus, CPU, vcpu_index);

    g_autoptr(GArray) reg_list = qemu_plugin_get_registers();
    g_assert(reg_list->len <= MAX_REGISTERS);
    for (int i = 0; i < reg_list->len; i++)
    {
        qemu_plugin_reg_descriptor *reg = &g_array_index(reg_list, qemu_plugin_reg_descriptor, i);
        cpu->reg_handles[i] = reg->handle;
    }
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    plugin_init(info);
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_callback);
    qemu_plugin_register_atexit_cb(id, dump_results, NULL);
    return 0;
}
