/*
 * RISC-V Capacity and Bandwidth QoS Register Interface
 * URL: https://github.com/riscv-non-isa/riscv-cbqri
 *
 * Copyright (c) 2023 BayLibre SAS
 *
 * This file contains the Capacity-controller QoS Register Interface.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/bitmap.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/cbqri.h"

/* Encodings of `AT` field */
enum {
    CC_AT_DATA = 0,
    CC_AT_CODE = 1,
};

/* Capabilities */
REG64(CC_CAPABILITIES, 0);
FIELD(CC_CAPABILITIES, VER, 0, 8);
FIELD(CC_CAPABILITIES, VER_MINOR, 0, 4);
FIELD(CC_CAPABILITIES, VER_MAJOR, 4, 4);
FIELD(CC_CAPABILITIES, NCBLKS, 8, 16);
FIELD(CC_CAPABILITIES, FRCID, 24, 1);

/* Usage monitoring control */
REG64(CC_MON_CTL, 8);
FIELD(CC_MON_CTL, OP, 0, 5);
FIELD(CC_MON_CTL, AT, 5, 3);
FIELD(CC_MON_CTL, MCID, 8, 12);
FIELD(CC_MON_CTL, EVT_ID, 20, 8);
FIELD(CC_MON_CTL, ATV, 28, 1);
FIELD(CC_MON_CTL, STATUS, 32, 7);
FIELD(CC_MON_CTL, BUSY, 39, 1);

/* Usage monitoring operations */
enum {
    CC_MON_OP_CONFIG_EVENT = 1,
    CC_MON_OP_READ_COUNTER = 2,
};

/* Usage monitoring event ID */
enum {
    CC_EVT_ID_None = 0,
    CC_EVT_ID_Occupancy = 1,
};

/* CC_MON_CTL.STATUS field encodings */
enum {
    CC_MON_CTL_STATUS_SUCCESS = 1,
    CC_MON_CTL_STATUS_INVAL_OP = 2,
    CC_MON_CTL_STATUS_INVAL_MCID = 3,
    CC_MON_CTL_STATUS_INVAL_EVT_ID = 4,
    CC_MON_CTL_STATUS_INVAL_AT = 5,
};

/* Monitoring counter value */
REG64(CC_MON_CTR_VAL, 16);
FIELD(CC_MON_CTR_VAL, CTR, 0, 63);
FIELD(CC_MON_CTR_VAL, INVALID, 63, 1);

/* Capacity allocation control */
REG64(CC_ALLOC_CTL, 24);
FIELD(CC_ALLOC_CTL, OP, 0, 5);
FIELD(CC_ALLOC_CTL, AT, 5, 3);
FIELD(CC_ALLOC_CTL, RCID, 8, 12);
FIELD(CC_ALLOC_CTL, STATUS, 32, 7);
FIELD(CC_ALLOC_CTL, BUSY, 39, 1);

/* Capacity allocation operations */
enum {
    CC_ALLOC_OP_CONFIG_LIMIT = 1,
    CC_ALLOC_OP_READ_LIMIT = 2,
    CC_ALLOC_OP_FLUSH_RCID = 3,
};

/* CC_ALLOC_CTL.STATUS field encodings */
enum {
    CC_ALLOC_STATUS_SUCCESS = 1,
    CC_ALLOC_STATUS_INVAL_OP = 2,
    CC_ALLOC_STATUS_INVAL_RCID = 3,
    CC_ALLOC_STATUS_INVAL_AT = 4,
    CC_ALLOC_STATUS_INVAL_BLKMASK = 5,
};

REG64(CC_BLOCK_MASK, 32);


typedef struct MonitorCounter {
    uint64_t ctr_val;
    int at;
    int evt_id;
    bool active;
} MonitorCounter;

typedef struct RiscvCbqriCapacityState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    /* cached value of some registers */
    uint64_t cc_mon_ctl;
    uint64_t cc_mon_ctr_val;
    uint64_t cc_alloc_ctl;

    /* monitoring counters */
    MonitorCounter *mon_counters;

    /* allocation blockmasks (1st one is the CC_BLOCK_MASK register) */
    uint64_t *alloc_blockmasks;

    /* properties */

    uint64_t mmio_base;
    char *target;
    uint16_t nb_mcids;
    uint16_t nb_rcids;

    uint16_t ncblks;

    bool supports_at_data;
    bool supports_at_code;

    bool supports_alloc_op_config_limit;
    bool supports_alloc_op_read_limit;
    bool supports_alloc_op_flush_rcid;

    bool supports_mon_op_config_event;
    bool supports_mon_op_read_counter;

    bool supports_mon_evt_id_none;
    bool supports_mon_evt_id_occupancy;
} RiscvCbqriCapacityState;

#define RISCV_CBQRI_CC(obj) \
    OBJECT_CHECK(RiscvCbqriCapacityState, (obj), TYPE_RISCV_CBQRI_CC)

static uint64_t *get_blockmask_location(RiscvCbqriCapacityState *cc,
                                        uint32_t rcid, uint32_t at)
{
    /*
     * All blockmasks are contiguous to simplify allocation.
     * The first one is used to hold the CC_BLOCK_MASK register content,
     * followed by respective blockmasks for each AT per RCID.
     * Each blockmask is made of one or more uint64_t "slots".
     */
    unsigned int nb_ats = 0;
    nb_ats += !!cc->supports_at_data;
    nb_ats += !!cc->supports_at_code;
    nb_ats = MAX(nb_ats, 1);
    assert(at < nb_ats);

    unsigned int blockmask_slots = (cc->ncblks + 63) / 64;
    unsigned int blockmask_offset = blockmask_slots * (1 + rcid * nb_ats + at);

    return cc->alloc_blockmasks + blockmask_offset;
}

static uint32_t alloc_blockmask_config(RiscvCbqriCapacityState *cc,
                                       uint32_t rcid, uint32_t at,
                                       bool *busy)
{
    unsigned int blockmask_slots = (cc->ncblks + 63) / 64;

    if ((cc->ncblks % 64) != 0) {
        /* make sure provided mask isn't too large */
        uint64_t tail = cc->alloc_blockmasks[blockmask_slots - 1];
        if ((tail >> (cc->ncblks % 64)) != 0) {
            return CC_ALLOC_STATUS_INVAL_BLKMASK;
        }
    }

    /* for now we only preserve the current CC_BLOCK_MASK register content */
    memcpy(get_blockmask_location(cc, rcid, at),
           cc->alloc_blockmasks, blockmask_slots * 8);
    return CC_ALLOC_STATUS_SUCCESS;
}

static uint32_t alloc_blockmask_read(RiscvCbqriCapacityState *cc,
                                     uint32_t rcid, uint32_t at,
                                     bool *busy)
{
    unsigned int blockmask_slots = (cc->ncblks + 63) / 64;

    memcpy(cc->alloc_blockmasks,
           get_blockmask_location(cc, rcid, at),
           blockmask_slots * 8);
    return CC_ALLOC_STATUS_SUCCESS;
}

static uint32_t alloc_blockmask_init(RiscvCbqriCapacityState *cc,
                                     uint32_t rcid, uint32_t at, bool set,
                                     bool *busy)
{
    void *blockmask = get_blockmask_location(cc, rcid, at);

    if (set) {
        bitmap_fill(blockmask, cc->ncblks);
    } else {
        bitmap_zero(blockmask, cc->ncblks);
    }
    return CC_ALLOC_STATUS_SUCCESS;
}

static bool is_valid_at(RiscvCbqriCapacityState *cc, uint32_t at)
{
    switch (at) {
    case CC_AT_DATA:
        return cc->supports_at_data;
    case CC_AT_CODE:
        return cc->supports_at_code;
    default:
        return false;
    }
}

static void riscv_cbqri_cc_write_mon_ctl(RiscvCbqriCapacityState *cc,
                                         uint64_t value)
{
    if (!cc->supports_mon_op_config_event &&
        !cc->supports_mon_op_read_counter) {
        /* monitoring not supported: leave mon_ctl set to 0 */
        return;
    }

    /* extract writable fields */
    uint32_t op = FIELD_EX64(value, CC_MON_CTL, OP);
    uint32_t at = FIELD_EX64(value, CC_MON_CTL, AT);
    uint32_t mcid = FIELD_EX64(value, CC_MON_CTL, MCID);
    uint32_t evt_id = FIELD_EX64(value, CC_MON_CTL, EVT_ID);
    bool atv = FIELD_EX64(value, CC_MON_CTL, ATV);

    /* extract read-only fields */
    uint32_t status = FIELD_EX64(cc->cc_mon_ctl, CC_MON_CTL, STATUS);
    bool busy = FIELD_EX64(cc->cc_mon_ctl, CC_MON_CTL, BUSY);

    if (busy) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: busy flag still set, ignored",
                      __func__);
        return;
    }

    if (!cc->supports_at_data &&
        !cc->supports_at_code) {
        /* AT not supported: hardwire to 0 */
        at = 0;
        atv = false;
    }

    if (mcid >= cc->nb_mcids) {
        status = CC_MON_CTL_STATUS_INVAL_MCID;
    } else if (op == CC_MON_OP_CONFIG_EVENT &&
               cc->supports_mon_op_config_event) {
        if (evt_id == CC_EVT_ID_None &&
            cc->supports_mon_evt_id_none) {
            cc->mon_counters[mcid].active = false;
            status = CC_MON_CTL_STATUS_SUCCESS;
        } else if (evt_id == CC_EVT_ID_Occupancy &&
                   cc->supports_mon_evt_id_occupancy) {
            if (atv && !is_valid_at(cc, at)) {
                status = CC_MON_CTL_STATUS_INVAL_AT;
            } else {
                cc->mon_counters[mcid].ctr_val =
                    FIELD_DP64(0, CC_MON_CTR_VAL, INVALID, 1);
                cc->mon_counters[mcid].evt_id = evt_id;
                cc->mon_counters[mcid].at = atv ? at : -1;
                cc->mon_counters[mcid].active = true;
                status = CC_MON_CTL_STATUS_SUCCESS;
            }
        } else {
            status = CC_MON_CTL_STATUS_INVAL_EVT_ID;
        }
    } else if (op == CC_MON_OP_READ_COUNTER &&
               cc->supports_mon_op_read_counter) {
        cc->cc_mon_ctr_val = cc->mon_counters[mcid].ctr_val;
        status = CC_MON_CTL_STATUS_SUCCESS;
    } else {
        status = CC_MON_CTL_STATUS_INVAL_OP;
    }

    /* reconstruct updated register value */
    value = 0;
    value = FIELD_DP64(value, CC_MON_CTL, OP, op);
    value = FIELD_DP64(value, CC_MON_CTL, AT, at);
    value = FIELD_DP64(value, CC_MON_CTL, MCID, mcid);
    value = FIELD_DP64(value, CC_MON_CTL, EVT_ID, evt_id);
    value = FIELD_DP64(value, CC_MON_CTL, ATV, atv);
    value = FIELD_DP64(value, CC_MON_CTL, STATUS, status);
    value = FIELD_DP64(value, CC_MON_CTL, BUSY, busy);
    cc->cc_mon_ctl = value;
}

static void riscv_cbqri_cc_write_alloc_ctl(RiscvCbqriCapacityState *cc,
                                           uint64_t value)
{
    if (cc->ncblks == 0 ||
        (!cc->supports_alloc_op_config_limit &&
         !cc->supports_alloc_op_read_limit &&
         !cc->supports_alloc_op_flush_rcid)) {
        /* capacity allocation not supported: leave alloc_ctl set to 0 */
        return;
    }

    /* extract writable fields */
    uint32_t op = FIELD_EX64(value, CC_ALLOC_CTL, OP);
    uint32_t at = FIELD_EX64(value, CC_ALLOC_CTL, AT);
    uint32_t rcid = FIELD_EX64(value, CC_ALLOC_CTL, RCID);

    /* extract read-only fields */
    uint32_t status = FIELD_EX64(cc->cc_alloc_ctl, CC_ALLOC_CTL, STATUS);
    bool busy = FIELD_EX64(cc->cc_alloc_ctl, CC_ALLOC_CTL, BUSY);

    if (busy) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: busy flag still set, ignored",
                      __func__);
        return;
    }

    bool atv = true;
    if (!cc->supports_at_data &&
        !cc->supports_at_code) {
        /* AT not supported: hardwire to 0 */
        at = 0;
        atv = false;
    }

    if (rcid >= cc->nb_rcids) {
        status = CC_ALLOC_STATUS_INVAL_RCID;
    } else if (atv && !is_valid_at(cc, at)) {
        status = CC_ALLOC_STATUS_INVAL_AT;
    } else if (op == CC_ALLOC_OP_CONFIG_LIMIT &&
               cc->supports_alloc_op_config_limit) {
        status = alloc_blockmask_config(cc, rcid, at, &busy);
    } else if (op == CC_ALLOC_OP_READ_LIMIT &&
               cc->supports_alloc_op_read_limit) {
        status = alloc_blockmask_read(cc, rcid, at, &busy);
    } else if (op == CC_ALLOC_OP_FLUSH_RCID &&
               cc->supports_alloc_op_flush_rcid) {
        status = alloc_blockmask_init(cc, rcid, at, false, &busy);
    } else {
        status = CC_ALLOC_STATUS_INVAL_OP;
    }

    /* reconstruct updated register value */
    value = 0;
    value = FIELD_DP64(value, CC_ALLOC_CTL, OP, op);
    value = FIELD_DP64(value, CC_ALLOC_CTL, AT, at);
    value = FIELD_DP64(value, CC_ALLOC_CTL, RCID, rcid);
    value = FIELD_DP64(value, CC_ALLOC_CTL, STATUS, status);
    value = FIELD_DP64(value, CC_ALLOC_CTL, BUSY, busy);
    cc->cc_alloc_ctl = value;
}

static void riscv_cbqri_cc_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    RiscvCbqriCapacityState *cc = opaque;
    uint32_t blkmask_slot;

    assert((addr % 8) == 0);
    assert(size == 8);

    switch (addr) {
    case A_CC_CAPABILITIES:
        /* read-only register */
        break;
    case A_CC_MON_CTL:
        riscv_cbqri_cc_write_mon_ctl(cc, value);
        break;
    case A_CC_ALLOC_CTL:
        riscv_cbqri_cc_write_alloc_ctl(cc, value);
        break;
    case A_CC_MON_CTR_VAL:
        /* read-only register */
        break;
    case A_CC_BLOCK_MASK:
        if (cc->ncblks == 0) {
            break;
        }
        /* fallthrough */
    default:
        blkmask_slot = (addr - A_CC_BLOCK_MASK) / 8;
        if (blkmask_slot >= (cc->ncblks + 63) / 64) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: out of bounds (addr=0x%x)",
                          __func__, (uint32_t)addr);
            break;
        }
        cc->alloc_blockmasks[blkmask_slot] = value;
    }
}

static uint64_t riscv_cbqri_cc_read(void *opaque, hwaddr addr, unsigned size)
{
    RiscvCbqriCapacityState *cc = opaque;
    uint64_t value = 0;
    unsigned int blkmask_slot;

    assert((addr % 8) == 0);
    assert(size == 8);

    switch (addr) {
    case A_CC_CAPABILITIES:
        value = FIELD_DP64(value, CC_CAPABILITIES, VER_MAJOR,
                           RISCV_CBQRI_VERSION_MAJOR);
        value = FIELD_DP64(value, CC_CAPABILITIES, VER_MINOR,
                           RISCV_CBQRI_VERSION_MINOR);
        value = FIELD_DP64(value, CC_CAPABILITIES, NCBLKS,
                           cc->ncblks);
        value = FIELD_DP64(value, CC_CAPABILITIES, FRCID,
                           cc->supports_alloc_op_flush_rcid);
        break;
    case A_CC_MON_CTL:
        value = cc->cc_mon_ctl;
        break;
    case A_CC_ALLOC_CTL:
        value = cc->cc_alloc_ctl;
        break;
    case A_CC_MON_CTR_VAL:
        value = cc->cc_mon_ctr_val;
        break;
    case A_CC_BLOCK_MASK:
        if (cc->ncblks == 0) {
            break;
        }
        /* fallthrough */
    default:
        blkmask_slot = (addr - A_CC_BLOCK_MASK) / 8;
        if (blkmask_slot >= (cc->ncblks + 63) / 64) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: out of bounds (addr=0x%x)",
                          __func__, (uint32_t)addr);
            break;
        }
        value = cc->alloc_blockmasks[blkmask_slot];
    }

    return value;
}

static const MemoryRegionOps riscv_cbqri_cc_ops = {
    .read = riscv_cbqri_cc_read,
    .write = riscv_cbqri_cc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
};

static void riscv_cbqri_cc_realize(DeviceState *dev, Error **errp)
{
    RiscvCbqriCapacityState *cc = RISCV_CBQRI_CC(dev);

    if (!cc->mmio_base) {
        error_setg(errp, "mmio_base property not set");
        return;
    }

    assert(cc->mon_counters == NULL);
    cc->mon_counters = g_new0(MonitorCounter, cc->nb_mcids);

    assert(cc->alloc_blockmasks == NULL);
    uint64_t *end = get_blockmask_location(cc, cc->nb_rcids, 0);
    unsigned int blockmasks_size = end - cc->alloc_blockmasks;
    cc->alloc_blockmasks = g_new0(uint64_t, blockmasks_size);

    memory_region_init_io(&cc->mmio, OBJECT(dev), &riscv_cbqri_cc_ops,
                          cc, TYPE_RISCV_CBQRI_CC".mmio", 4 * 1024);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &cc->mmio);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, cc->mmio_base);
}

static void riscv_cbqri_cc_reset(DeviceState *dev)
{
    RiscvCbqriCapacityState *cc = RISCV_CBQRI_CC(dev);

    cc->cc_mon_ctl = 0;
    cc->cc_alloc_ctl = 0;

    /* assign all capacity only to rcid0 */
    for (unsigned int rcid = 0; rcid < cc->nb_rcids; rcid++) {
        bool any_at = false;

        if (cc->supports_at_data) {
            alloc_blockmask_init(cc, rcid, CC_AT_DATA,
                                 rcid == 0, NULL);
            any_at = true;
        }
        if (cc->supports_at_code) {
            alloc_blockmask_init(cc, rcid, CC_AT_CODE,
                                 rcid == 0, NULL);
            any_at = true;
        }
        if (!any_at) {
            alloc_blockmask_init(cc, rcid, 0,
                                 rcid == 0, NULL);
        }
    }
}

static Property riscv_cbqri_cc_properties[] = {
    DEFINE_PROP_UINT64("mmio_base", RiscvCbqriCapacityState, mmio_base, 0),
    DEFINE_PROP_STRING("target", RiscvCbqriCapacityState, target),

    DEFINE_PROP_UINT16("max_mcids", RiscvCbqriCapacityState, nb_mcids, 256),
    DEFINE_PROP_UINT16("max_rcids", RiscvCbqriCapacityState, nb_rcids, 64),
    DEFINE_PROP_UINT16("ncblks", RiscvCbqriCapacityState, ncblks, 16),

    DEFINE_PROP_BOOL("at_data", RiscvCbqriCapacityState,
                     supports_at_data, true),
    DEFINE_PROP_BOOL("at_code", RiscvCbqriCapacityState,
                     supports_at_code, true),

    DEFINE_PROP_BOOL("alloc_op_config_limit", RiscvCbqriCapacityState,
                     supports_alloc_op_config_limit, true),
    DEFINE_PROP_BOOL("alloc_op_read_limit", RiscvCbqriCapacityState,
                     supports_alloc_op_read_limit, true),
    DEFINE_PROP_BOOL("alloc_op_flush_rcid", RiscvCbqriCapacityState,
                     supports_alloc_op_flush_rcid, true),

    DEFINE_PROP_BOOL("mon_op_config_event", RiscvCbqriCapacityState,
                     supports_mon_op_config_event, true),
    DEFINE_PROP_BOOL("mon_op_read_counter", RiscvCbqriCapacityState,
                     supports_mon_op_read_counter, true),

    DEFINE_PROP_BOOL("mon_evt_id_none", RiscvCbqriCapacityState,
                     supports_mon_evt_id_none, true),
    DEFINE_PROP_BOOL("mon_evt_id_occupancy", RiscvCbqriCapacityState,
                     supports_mon_evt_id_occupancy, true),

    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_cbqri_cc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = riscv_cbqri_cc_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "RISC-V CBQRI Capacity Controller";
    device_class_set_props(dc, riscv_cbqri_cc_properties);
    dc->reset = riscv_cbqri_cc_reset;
    dc->user_creatable = true;
}

static const TypeInfo riscv_cbqri_cc_info = {
    .name          = TYPE_RISCV_CBQRI_CC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RiscvCbqriCapacityState),
    .class_init    = riscv_cbqri_cc_class_init,
};

static void riscv_cbqri_cc_register_types(void)
{
    type_register_static(&riscv_cbqri_cc_info);
}

DeviceState *riscv_cbqri_cc_create(hwaddr addr,
                                   const RiscvCbqriCapacityCaps *caps,
                                   const char *target_name)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_CBQRI_CC);

    qdev_prop_set_uint64(dev, "mmio_base", addr);
    qdev_prop_set_string(dev, "target", target_name);
    qdev_prop_set_uint16(dev, "max_mcids", caps->nb_mcids);
    qdev_prop_set_uint16(dev, "max_rcids", caps->nb_rcids);
    qdev_prop_set_uint16(dev, "ncblks", caps->ncblks);

    qdev_prop_set_bit(dev, "at_data",
                      caps->supports_at_data);
    qdev_prop_set_bit(dev, "at_code",
                      caps->supports_at_code);
    qdev_prop_set_bit(dev, "alloc_op_config_limit",
                      caps->supports_alloc_op_config_limit);
    qdev_prop_set_bit(dev, "alloc_op_read_limit",
                      caps->supports_alloc_op_read_limit);
    qdev_prop_set_bit(dev, "alloc_op_flush_rcid",
                      caps->supports_alloc_op_flush_rcid);
    qdev_prop_set_bit(dev, "mon_op_config_event",
                      caps->supports_mon_op_config_event);
    qdev_prop_set_bit(dev, "mon_op_read_counter",
                      caps->supports_mon_op_read_counter);
    qdev_prop_set_bit(dev, "mon_evt_id_none",
                      caps->supports_mon_evt_id_none);
    qdev_prop_set_bit(dev, "mon_evt_id_occupancy",
                      caps->supports_mon_evt_id_occupancy);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    return dev;
}

type_init(riscv_cbqri_cc_register_types)
