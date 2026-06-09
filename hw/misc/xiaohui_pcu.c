/*
 * Xiaohui PCU (Power Control Unit).
 *
 * Per-core power domain controller for Xiaohui platform.
 * Implements core power on/off (hotplug) and idle power management.
 *
 * Copyright (c) 2026 Alibaba Group. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "hw/core/cpu.h"
#include "target/riscv/riscv-power.h"
#include "qemu/bitops.h"
#include "hw/misc/xiaohui_pcu.h"

/* Register offsets */
#define PCU_RVBA_CONFIG_LO              0x020
#define PCU_RVBA_CONFIG_HI              0x028
#define PCU_PCHNL_ACK_COUNTER           0x030
#define PCU_SW_CTRL_CONFIG              0x038
#define PCU_SW_MODE_CTRL0               0x100
#define PCU_IRQ_EVENTS0                 0x300
#define PCU_IRQ_MASK0                   0x400
#define PCU_MODE_ENTRY_TIMER_CONFIG0_0  0x600
#define PCU_DBG_CTRL                    0x800
#define PCU_DBG_REQ_TRIGGER             0x808
#define PCU_DEV_PREQ_DISABLE            0x810
#define PCU_MODE_STATUS0                0x900
#define PCU_PCHNL_STATUS0               0xA00
#define PCU_PACTIVE_EN0                 0xB00
#define PCU_PWR_CTRL_STATUS0_0          0xC00

/* sw_pwr_mode encodings */
#define PCU_PWR_MODE_OFF                0x0
#define PCU_PWR_MODE_OFF_EMU            0x1
#define PCU_PWR_MODE_ON                 0x8
#define PCU_PWR_MODE_WARM_RST           0x9

/* pcu_sw_mode_ctrl0 fields */
#define PCU_SW_MODE_CTRL0_TRANS_MODE    BIT(31)
#define PCU_SW_MODE_CTRL0_EMU_EN        BIT(7)
#define PCU_SW_MODE_CTRL0_PWR_MODE_MASK 0xF

/* pcu_irq_events0 / pcu_irq_mask0 bits */
#define PCU_IRQ_TRANS_CMPLT             BIT(0)
#define PCU_IRQ_PCHNL_DENY             BIT(2)
#define PCU_IRQ_PCHNL_NORESP           BIT(6)
#define PCU_IRQ_INVALID_SW             BIT(7)
#define PCU_IRQ_WOCLR_MASK             (PCU_IRQ_TRANS_CMPLT | \
                                        PCU_IRQ_PCHNL_DENY | \
                                        PCU_IRQ_PCHNL_NORESP | \
                                        PCU_IRQ_INVALID_SW)

/* pcu_mode_status0 bits */
#define PCU_MODE_STATUS_OFF             BIT(0)
#define PCU_MODE_STATUS_OFF_EMU         BIT(1)
#define PCU_MODE_STATUS_ON              BIT(8)
#define PCU_MODE_STATUS_WARM_RST        BIT(9)

/* TYPE_XIAOHUI_PCU is defined in hw/misc/xiaohui_pcu.h */
#define XIAOHUI_PCU(obj)  OBJECT_CHECK(XiaohuiPcuState, (obj), \
                                       TYPE_XIAOHUI_PCU)

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    /* The CPU hart-id this PCU controls */
    uint32_t hart_id;

    /* Register state */
    uint32_t rvba_lo;
    uint32_t rvba_hi;
    uint32_t pchnl_ack_counter;
    uint32_t sw_ctrl_config;
    uint32_t sw_mode_ctrl0;
    uint32_t irq_events0;
    uint32_t irq_mask0;
    uint32_t mode_entry_timer_config0_0;
    uint32_t dbg_ctrl;
    uint32_t dbg_req_trigger;
    uint32_t dev_preq_disable;
    uint32_t pactive_en0;

    /* Current power mode status (read-only from software) */
    uint32_t mode_status0;

} XiaohuiPcuState;

static void xiaohui_pcu_update_irq(XiaohuiPcuState *s)
{
    uint32_t pending = s->irq_events0 & ~s->irq_mask0 & PCU_IRQ_WOCLR_MASK;
    qemu_set_irq(s->irq, pending != 0);
}

static void xiaohui_pcu_do_power_on(XiaohuiPcuState *s)
{
    CPUState *cpu = qemu_get_cpu(s->hart_id);

    if (cpu == NULL) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "xiaohui_pcu: hart %u does not exist\n", s->hart_id);
        return;
    }

    /*
     * PCU only records the configuration; actual core release is
     * performed by CPR when it sees this PCU is ready.
     */
    s->mode_status0 = PCU_MODE_STATUS_ON;

    /* Signal transition complete */
    s->irq_events0 |= PCU_IRQ_TRANS_CMPLT;
    xiaohui_pcu_update_irq(s);
}

static void xiaohui_pcu_do_power_off(XiaohuiPcuState *s)
{
    CPUState *cpu = qemu_get_cpu(s->hart_id);

    if (cpu == NULL) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "xiaohui_pcu: hart %u does not exist\n", s->hart_id);
        return;
    }

    s->mode_status0 = PCU_MODE_STATUS_OFF;
    riscv_cpu_set_power_off(cpu);

    /* Signal transition complete */
    s->irq_events0 |= PCU_IRQ_TRANS_CMPLT;
    xiaohui_pcu_update_irq(s);
}

static void xiaohui_pcu_handle_mode_change(XiaohuiPcuState *s,
                                           uint32_t new_pwr_mode)
{
    uint32_t old_pwr_mode = s->sw_mode_ctrl0 & PCU_SW_MODE_CTRL0_PWR_MODE_MASK;

    if (new_pwr_mode == old_pwr_mode) {
        return;
    }

    switch (new_pwr_mode) {
    case PCU_PWR_MODE_ON:
        xiaohui_pcu_do_power_on(s);
        break;
    case PCU_PWR_MODE_OFF:
        xiaohui_pcu_do_power_off(s);
        break;
    case PCU_PWR_MODE_OFF_EMU:
    {
        /* OFF_EMU: power off but keep debug access */
        CPUState *cpu = qemu_get_cpu(s->hart_id);
        if (cpu == NULL) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "xiaohui_pcu: hart %u does not exist\n",
                          s->hart_id);
            return;
        }
        s->mode_status0 = PCU_MODE_STATUS_OFF_EMU;
        riscv_cpu_set_power_off(cpu);
        s->irq_events0 |= PCU_IRQ_TRANS_CMPLT;
        xiaohui_pcu_update_irq(s);
        break;
    }
    case PCU_PWR_MODE_WARM_RST:
    {
        CPUState *cpu = qemu_get_cpu(s->hart_id);
        if (cpu == NULL) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "xiaohui_pcu: hart %u does not exist\n",
                          s->hart_id);
            return;
        }
        /* Record final state as ON; actual release is done by CPR */
        s->mode_status0 = PCU_MODE_STATUS_ON;
        s->irq_events0 |= PCU_IRQ_TRANS_CMPLT;
        xiaohui_pcu_update_irq(s);
        break;
    }
    default:
        /* Invalid mode: raise invalid_sw_set_pwr_mode_irq */
        s->irq_events0 |= PCU_IRQ_INVALID_SW;
        xiaohui_pcu_update_irq(s);
        return;
    }
}

static uint64_t xiaohui_pcu_read(void *opaque, hwaddr offset, unsigned size)
{
    XiaohuiPcuState *s = XIAOHUI_PCU(opaque);

    switch (offset) {
    case PCU_RVBA_CONFIG_LO:
        return s->rvba_lo;
    case PCU_RVBA_CONFIG_HI:
        return s->rvba_hi;
    case PCU_PCHNL_ACK_COUNTER:
        return s->pchnl_ack_counter;
    case PCU_SW_CTRL_CONFIG:
        return s->sw_ctrl_config;
    case PCU_SW_MODE_CTRL0:
        return s->sw_mode_ctrl0;
    case PCU_IRQ_EVENTS0:
        return s->irq_events0;
    case PCU_IRQ_MASK0:
        return s->irq_mask0;
    case PCU_MODE_ENTRY_TIMER_CONFIG0_0:
        return s->mode_entry_timer_config0_0;
    case PCU_DBG_CTRL:
        return s->dbg_ctrl;
    case PCU_DBG_REQ_TRIGGER:
        return s->dbg_req_trigger;
    case PCU_DEV_PREQ_DISABLE:
        return s->dev_preq_disable;
    case PCU_MODE_STATUS0:
        return s->mode_status0;
    case PCU_PCHNL_STATUS0:
        return 0;
    case PCU_PACTIVE_EN0:
        return s->pactive_en0;
    case PCU_PWR_CTRL_STATUS0_0:
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "xiaohui_pcu: invalid read at offset 0x%" HWADDR_PRIx
                      "\n", offset);
        return 0;
    }
}

static void xiaohui_pcu_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    XiaohuiPcuState *s = XIAOHUI_PCU(opaque);

    switch (offset) {
    case PCU_RVBA_CONFIG_LO:
        s->rvba_lo = value;
        break;
    case PCU_RVBA_CONFIG_HI:
        s->rvba_hi = value;
        break;
    case PCU_PCHNL_ACK_COUNTER:
        s->pchnl_ack_counter = (s->pchnl_ack_counter & 0xFFFF0000) |
                               (value & 0xFFFF);
        break;
    case PCU_SW_CTRL_CONFIG:
        s->sw_ctrl_config = value & 0x3;
        break;
    case PCU_SW_MODE_CTRL0:
    {
        uint32_t new_pwr_mode = value & PCU_SW_MODE_CTRL0_PWR_MODE_MASK;
        xiaohui_pcu_handle_mode_change(s, new_pwr_mode);
        /* Update the full register preserving writable bits only */
        s->sw_mode_ctrl0 = (value & (PCU_SW_MODE_CTRL0_TRANS_MODE |
                                     PCU_SW_MODE_CTRL0_EMU_EN |
                                     PCU_SW_MODE_CTRL0_PWR_MODE_MASK));
        break;
    }
    case PCU_IRQ_EVENTS0:
        /* WOCLR: writing 1 clears the corresponding bit */
        s->irq_events0 &= ~(value & PCU_IRQ_WOCLR_MASK);
        xiaohui_pcu_update_irq(s);
        break;
    case PCU_IRQ_MASK0:
        s->irq_mask0 = value & (PCU_IRQ_INVALID_SW | PCU_IRQ_PCHNL_NORESP |
                                PCU_IRQ_PCHNL_DENY | PCU_IRQ_TRANS_CMPLT);
        xiaohui_pcu_update_irq(s);
        break;
    case PCU_MODE_ENTRY_TIMER_CONFIG0_0:
        s->mode_entry_timer_config0_0 = value & 0xFFFF;
        break;
    case PCU_DBG_CTRL:
        s->dbg_ctrl = value & 0x1;
        break;
    case PCU_DBG_REQ_TRIGGER:
        s->dbg_req_trigger = value & 0x1;
        break;
    case PCU_DEV_PREQ_DISABLE:
        s->dev_preq_disable = value & 0x1;
        break;
    case PCU_PACTIVE_EN0:
        s->pactive_en0 = value & BIT(8);
        break;
    case PCU_MODE_STATUS0:
    case PCU_PCHNL_STATUS0:
    case PCU_PWR_CTRL_STATUS0_0:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "xiaohui_pcu: write to read-only register at offset "
                      "0x%" HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "xiaohui_pcu: invalid write at offset 0x%" HWADDR_PRIx
                      "\n", offset);
        break;
    }
}

static const MemoryRegionOps xiaohui_pcu_ops = {
    .read = xiaohui_pcu_read,
    .write = xiaohui_pcu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void xiaohui_pcu_reset(DeviceState *dev)
{
    XiaohuiPcuState *s = XIAOHUI_PCU(dev);

    s->rvba_lo = 0;
    s->rvba_hi = 0;
    s->pchnl_ack_counter = 0x8000;
    s->sw_ctrl_config = 0x3;
    s->sw_mode_ctrl0 = PCU_SW_MODE_CTRL0_TRANS_MODE; /* bit31 = 1 */
    s->irq_events0 = 0;
    s->irq_mask0 = PCU_IRQ_INVALID_SW | PCU_IRQ_PCHNL_NORESP |
                   PCU_IRQ_PCHNL_DENY;  /* 0xC4 */
    s->mode_entry_timer_config0_0 = 0x8000;
    s->dbg_ctrl = 0;
    s->dbg_req_trigger = 0;
    s->dev_preq_disable = 0;
    s->pactive_en0 = BIT(8);  /* 0x100 */
    s->mode_status0 = PCU_MODE_STATUS_OFF;  /* reset: OFF */
}

static void xiaohui_pcu_init(Object *obj)
{
    XiaohuiPcuState *s = XIAOHUI_PCU(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &xiaohui_pcu_ops, s,
                          TYPE_XIAOHUI_PCU, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static Property xiaohui_pcu_properties[] = {
    DEFINE_PROP_UINT32("hart-id", XiaohuiPcuState, hart_id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void xiaohui_pcu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = xiaohui_pcu_reset;
    device_class_set_props(dc, xiaohui_pcu_properties);
}

static const TypeInfo xiaohui_pcu_info = {
    .name          = TYPE_XIAOHUI_PCU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XiaohuiPcuState),
    .instance_init = xiaohui_pcu_init,
    .class_init    = xiaohui_pcu_class_init,
};

static void xiaohui_pcu_register_types(void)
{
    type_register_static(&xiaohui_pcu_info);
}

type_init(xiaohui_pcu_register_types)

/* ---- Public query interface used by CPR ---- */

typedef struct {
    uint32_t target_hart_id;
    DeviceState *found;
} PcuFindCtx;

static int pcu_find_walker(Object *obj, void *opaque)
{
    PcuFindCtx *ctx = opaque;

    if (object_dynamic_cast(obj, TYPE_XIAOHUI_PCU)) {
        XiaohuiPcuState *pcu = XIAOHUI_PCU(obj);
        if (pcu->hart_id == ctx->target_hart_id) {
            ctx->found = DEVICE(obj);
            return 1; /* stop iteration */
        }
    }
    return 0;
}

DeviceState *xiaohui_pcu_find_by_hart(uint32_t hart_id)
{
    PcuFindCtx ctx = { .target_hart_id = hart_id, .found = NULL };
    Object *machine = OBJECT(qdev_get_machine());

    object_child_foreach_recursive(machine, pcu_find_walker, &ctx);
    return ctx.found;
}

bool xiaohui_pcu_get_release_info(DeviceState *pcu_dev, uint64_t *rvba)
{
    XiaohuiPcuState *s = XIAOHUI_PCU(pcu_dev);
    uint32_t pwr_mode = s->sw_mode_ctrl0 & PCU_SW_MODE_CTRL0_PWR_MODE_MASK;

    /* Ready only when mode is ON or WARM_RST and RVBA has been written */
    if (pwr_mode != PCU_PWR_MODE_ON && pwr_mode != PCU_PWR_MODE_WARM_RST) {
        return false;
    }

    if (s->rvba_lo == 0 && s->rvba_hi == 0) {
        return false;
    }

    *rvba = ((uint64_t)s->rvba_hi << 32) | s->rvba_lo;
    return true;
}
