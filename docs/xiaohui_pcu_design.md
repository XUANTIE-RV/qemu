### Xiaohui PCU (Power Control Unit) 概要设计文档

#### 1. 概述

##### 1.1 设备简介
Xiaohui PCU (Power Control Unit) 是 Xiaohui V3 平台的 per-core 电源域控制器。每个 PCU 实例管理一个 CPU core 的电源状态，支持 Core Hotplug（热插拔）和 CPU Idle（空闲节能）两种低功耗切换场景。

##### 1.2 主要功能
- **Core 电源开关**：支持 core 的 power on / power off 控制
- **多种电源模式**：支持 OFF、OFF_EMU、ON、WARM_RST 四种电源模式
- **Reset 向量配置**：支持配置 core 上电后的 reset base address (RVBA)
- **中断管理**：支持电源状态切换完成、非法配置等中断事件
- **诊断模式**：支持 debug 诊断模式下的电源切换触发
- **Per-core 独立管理**：每个 PCU 实例独立管理一个 core，通过 `hart-id` 属性绑定

##### 1.3 参考实现
PCU 设备参考了 `xiaohui_ahb_cpr`（`hw/misc/xiaohui_ahb_cpr.c`）的 SysBusDevice 设备模型，复用了 RISC-V 电源管理接口（`target/riscv/riscv-power.h`）。

#### 2. 系统架构

##### 2.1 整体架构
```
┌──────────────────────────────────────────────────────────────┐
│                     Xiaohui V3 SoC                           │
│                                                              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │  Core 0  │  │  Core 1  │  │  Core 2  │  │  Core N  │    │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘    │
│       │              │              │              │          │
│  ┌────┴─────┐  ┌────┴─────┐  ┌────┴─────┐  ┌────┴─────┐    │
│  │  PCU 0   │  │  PCU 1   │  │  PCU 2   │  │  PCU N   │    │
│  │ 0x26808  │  │ 0x26828  │  │ 0x26888  │  │  ...     │    │
│  │   000    │  │   000    │  │   000    │  │          │    │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘    │
│       │              │              │              │          │
│       └──────────────┴──────────────┴──────────────┘          │
│                          System Bus                           │
└──────────────────────────────────────────────────────────────┘
```

##### 2.2 地址映射
- **基地址**：`0x26808000`（在 `xiaohui_v3_memmap[XIAOHUI_V3_PCU]` 中定义）
- **每个 PCU 大小**：4KB（`0x1000`）
- **最大支持**：16 个 PCU 实例
- **地址计算公式**：`addr(N) = 0x26808000 + (N / 2) * 0x80000 + (N % 2) * 0x20000`

每两个 PCU 为一组，组内间距 `0x20000`，组间间距 `0x80000`。完整地址表如下：

| PCU | 地址 | PCU | 地址 |
|-----|------|-----|------|
| PCU 0 | `0x26808000` | PCU 1 | `0x26828000` |
| PCU 2 | `0x26888000` | PCU 3 | `0x268A8000` |
| PCU 4 | `0x26908000` | PCU 5 | `0x26928000` |
| PCU 6 | `0x26988000` | PCU 7 | `0x269A8000` |
| PCU 8 | `0x26A08000` | PCU 9 | `0x26A28000` |
| PCU 10 | `0x26A88000` | PCU 11 | `0x26AA8000` |
| PCU 12 | `0x26B08000` | PCU 13 | `0x26B28000` |
| PCU 14 | `0x26B88000` | PCU 15 | `0x26BA8000` |

##### 2.3 设备实例化
在 `hw/riscv/xiaohui_v3.c` 的 `xiaohui_init()` 函数中，`!boot_linux` 分支下为每个 core 创建一个 PCU 实例：
```c
for (int i = 0; i < smp_cpus; i++) {
    hwaddr pcu_addr = xiaohui_v3_memmap[XIAOHUI_V3_PCU].base +
                      (i / 2) * 0x80000 + (i % 2) * 0x20000;
    DeviceState *pcu_dev = qdev_new("xiaohui_pcu");
    qdev_prop_set_uint32(pcu_dev, "hart-id", i);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(pcu_dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(pcu_dev), 0, pcu_addr);
}
```

#### 3. 电源模式

##### 3.1 模式定义

| 模式 | 编码 (sw_pwr_mode) | mode_status0 位 | 说明 |
|------|---------------------|------------------|------|
| OFF | `4'b0000` (0x0) | bit[0] | 核心完全断电，默认上电状态 |
| OFF_EMU | `4'b0001` (0x1) | bit[1] | 核心断电但保留调试访问 |
| ON | `4'b1000` (0x8) | bit[8] | 核心上电运行 |
| WARM_RST | `4'b1001` (0x9) | bit[9] | 热复位（先复位再上电） |

##### 3.2 模式转换状态机
```
                    ┌──────────────┐
         ┌─────────│     OFF      │←────────┐
         │         │  (reset=0x1) │         │
         │         └──────┬───────┘         │
         │                │                 │
    sw_pwr_mode=     sw_pwr_mode=      sw_pwr_mode=
     OFF_EMU(0x1)     ON(0x8)           OFF(0x0)
         │                │                 │
         ▼                ▼                 │
  ┌──────────────┐  ┌──────────────┐        │
  │   OFF_EMU    │  │      ON      │────────┘
  │              │  │              │
  └──────────────┘  └──────┬───────┘
                           │
                      sw_pwr_mode=
                       WARM_RST(0x9)
                           │
                           ▼
                    ┌──────────────┐
                    │   WARM_RST   │──→ 自动转为 ON
                    └──────────────┘
```

#### 4. 寄存器映射

##### 4.1 寄存器列表

| 偏移地址 | 寄存器名 | 访问类型 | 复位值 | 说明 |
|----------|----------|----------|--------|------|
| 0x020 | pcu_rvba_config_lo | RW | 0x0 | Core 上电 reset 地址低 32 位 |
| 0x028 | pcu_rvba_config_hi | RW | 0x0 | Core 上电 reset 地址高 32 位 |
| 0x030 | pcu_pchnl_ack_counter | RW | 0x8000 | P-Channel 握手等待时间 (bits[15:0]) |
| 0x038 | pcu_sw_ctrl_config | RW | 0x3 | 软件控制配置 (bits[1:0]) |
| 0x100 | pcu_sw_mode_ctrl0 | RW | 0x80000000 | 电源模式控制 |
| 0x300 | pcu_irq_events0 | WOCLR | 0x0 | 中断事件 |
| 0x400 | pcu_irq_mask0 | RW | 0xC4 | 中断掩码 |
| 0x600 | pcu_mode_entry_timer_config0_0 | RW | 0x8000 | OFF 模式进入等待时间 (bits[15:0]) |
| 0x800 | pcu_dbg_ctrl | RW | 0x0 | 诊断模式控制 (bit[0]) |
| 0x808 | pcu_dbg_req_trigger | RW | 0x0 | 诊断模式切换触发 (bit[0]) |
| 0x810 | pcu_dev_preq_disable | RW | 0x0 | P-Channel 禁用 (bit[0]) |
| 0x900 | pcu_mode_status0 | RO | 0x1 | 当前电源模式状态 |
| 0xA00 | pcu_pchnl_status0 | RO | 0x0 | P-Channel 状态 |
| 0xB00 | pcu_pactive_en0 | RW | 0x100 | Pactive 使能 (bit[8]) |
| 0xC00 | pcu_pwr_ctrl_status0_0 | RO | 0x0 | 电源控制信号状态 |

##### 4.2 关键寄存器详解

###### 4.2.1 pcu_sw_mode_ctrl0 (0x100)
| 位域 | 名称 | 访问 | 复位值 | 说明 |
|------|------|------|--------|------|
| 31 | trans_mode | RW | 0x1 | 置 1 时需要软件配置才能从 OFF 上电；否则硬件可通过中断自动上电 |
| 30:8 | - | RO | 0x0 | 保留 |
| 7 | emu_en | RW | 0x0 | 置 1 时启用 OFF_EMU 模式 |
| 6:4 | - | RO | 0x0 | 保留 |
| 3:0 | sw_pwr_mode | RW | 0x0 | 软件可配置的电源模式编码 |

###### 4.2.2 pcu_irq_events0 (0x300)
| 位域 | 名称 | 访问 | 复位值 | 说明 |
|------|------|------|--------|------|
| 7 | invalid_sw_set_pwr_mode_irq | WOCLR | 0x0 | 非法软件配置中断 |
| 6 | dev_pchnl_noresp_irq | WOCLR | 0x0 | P-Channel 无响应中断 |
| 2 | dev_pchnl_deny_irq | WOCLR | 0x0 | P-Channel 拒绝中断 |
| 0 | trans_cmplt_irq | WOCLR | 0x0 | 电源模式切换完成中断 |

WOCLR (Write 1 to Clear)：读取返回当前值，写 1 清除对应位。

###### 4.2.3 pcu_sw_ctrl_config (0x038)
| 位域 | 名称 | 访问 | 复位值 | 说明 |
|------|------|------|--------|------|
| 1 | trans_ign_wkup_trig | RW | 0x1 | 置 1 时忽略 pactive |
| 0 | wakeup_stay_on | RW | 0x1 | 置 1 时 core 从 OFF 唤醒后自动设置 sw_pwr_mode 为 ON（用于 CPU Idle 场景） |

###### 4.2.4 pcu_mode_status0 (0x900)
| 位域 | 名称 | 访问 | 复位值 | 说明 |
|------|------|------|--------|------|
| 9 | cur_mode_9 | RO | 0x0 | 当前模式为 WARM_RST 时置 1 |
| 8 | cur_mode_8 | RO | 0x0 | 当前模式为 ON 时置 1 |
| 1 | cur_mode_1 | RO | 0x0 | 当前模式为 OFF_EMU 时置 1 |
| 0 | cur_mode_0 | RO | 0x1 | 当前模式为 OFF 时置 1 |

#### 5. 工作流程

##### 5.1 Core Power Down (Hotplug) 流程
```
Software                          PCU                           Core
   │                               │                              │
   │  1. 写 sw_pwr_mode = OFF(0x0) │                              │
   │──────────────────────────────→│                              │
   │  2. 屏蔽并迁移所有中断        │                              │
   │  3. 关闭 MIE/SIE 中断使能     │                              │
   │  4. 关闭数据预取              │                              │
   │  5. 执行 fence iorw           │                              │
   │  6. 执行 WFI                  │                              │
   │                               │  7a. 禁用并刷新 cache         │
   │                               │─────────────────────────────→│
   │                               │  7b. 移出一致性域             │
   │                               │─────────────────────────────→│
   │                               │  7c. 等待下电完成             │
   │                               │  更新 mode_status0 = OFF     │
   │                               │  设置 trans_cmplt_irq        │
   │                               │                              │
```

##### 5.2 Core Power On (Hotplug) 流程
```
Software                          PCU                           Core
   │                               │                              │
   │  1. 设置 rvba_config_hi/lo    │                              │
   │──────────────────────────────→│                              │
   │  2. 写 sw_pwr_mode = ON(0x8)  │                              │
   │──────────────────────────────→│                              │
   │                               │  释放 core reset             │
   │                               │  (riscv_cpu_release)         │
   │                               │─────────────────────────────→│
   │                               │  更新 mode_status0 = ON      │
   │                               │  设置 trans_cmplt_irq        │
   │                               │                              │
   │                               │              3. 从 rvba 开始执行
   │                               │              4. 初始化并重配中断
   │                               │                              │
```

##### 5.3 Core Power Down (Idle) 流程
与 Hotplug 下电流程类似，但不需要软件屏蔽和迁移中断。Core 直接执行 `fence iorw` + `WFI`，PCU 自动完成下电。

##### 5.4 Core Power On (Idle) 流程
与 Hotplug 上电流程相同：设置 rvba → 写 sw_pwr_mode = ON → core 从 rvba 开始执行。

#### 6. 数据结构

##### 6.1 XiaohuiPcuState（设备状态）
```c
typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;          /* 4KB MMIO 区域 */
    qemu_irq irq;               /* 中断输出 */

    uint32_t hart_id;            /* 绑定的 CPU hart-id */

    /* 寄存器状态 */
    uint32_t rvba_lo;            /* Reset 向量地址低 32 位 */
    uint32_t rvba_hi;            /* Reset 向量地址高 32 位 */
    uint32_t pchnl_ack_counter;  /* P-Channel 握手超时计数 */
    uint32_t sw_ctrl_config;     /* 软件控制配置 */
    uint32_t sw_mode_ctrl0;      /* 电源模式控制 */
    uint32_t irq_events0;        /* 中断事件 (WOCLR) */
    uint32_t irq_mask0;          /* 中断掩码 */
    uint32_t mode_entry_timer_config0_0; /* OFF 进入等待时间 */
    uint32_t dbg_ctrl;           /* 诊断模式控制 */
    uint32_t dbg_req_trigger;    /* 诊断切换触发 */
    uint32_t dev_preq_disable;   /* P-Channel 禁用 */
    uint32_t pactive_en0;        /* Pactive 使能 */

    uint32_t mode_status0;       /* 当前电源模式状态 (RO) */
} XiaohuiPcuState;
```

#### 7. 关键实现

##### 7.1 电源模式切换 (xiaohui_pcu_handle_mode_change)
当软件写入 `pcu_sw_mode_ctrl0` 寄存器的 `sw_pwr_mode` 字段时，触发模式切换：

| 目标模式 | 实现动作 |
|----------|----------|
| **ON** | 若 RVBA 已配置则立即调用 `riscv_cpu_release(cpu, rvba, hart_id)` 释放 core；否则标记为 pending，等待 RVBA 写入后自动触发 |
| **OFF** | 调用 `riscv_cpu_set_power_off(cpu)` 关闭 core 电源 |
| **OFF_EMU** | 调用 `riscv_cpu_set_power_off(cpu)` 关闭 core（保留调试访问语义） |
| **WARM_RST** | 先设置 WARM_RST 状态，再调用 `riscv_cpu_release()` 上电，最终状态转为 ON |
| **非法值** | 设置 `invalid_sw_set_pwr_mode_irq` 中断事件 |

每次成功的模式切换都会：
1. 更新 `mode_status0` 寄存器
2. 设置 `trans_cmplt_irq` 中断事件
3. 调用 `xiaohui_pcu_update_irq()` 更新中断线状态

##### 7.2 中断处理 (xiaohui_pcu_update_irq)
```
pending = irq_events0 & ~irq_mask0 & PCU_IRQ_WOCLR_MASK
IRQ 输出 = (pending != 0)
```
- `irq_mask0` 中对应位为 1 表示该中断被屏蔽
- 软件通过向 `irq_events0` 写 1 来清除对应中断事件

##### 7.3 WOCLR 寄存器语义
`pcu_irq_events0` 的可清除位（bits 7, 6, 2, 0）采用 WOCLR (Write 1 to Clear) 语义：
- **读**：返回当前中断事件状态
- **写**：写入值中为 1 的位将清除对应的中断事件位

##### 7.4 RVBA (Reset Vector Base Address)
64 位 reset 向量地址由两个 32 位寄存器组成：
```
rvba = (rvba_hi << 32) | rvba_lo
```
在 power on 时，通过 `riscv_cpu_release(cpu, rvba, hart_id)` 将此地址传递给 core 作为启动地址。

#### 8. 构建配置

##### 8.1 涉及文件

| 文件路径 | 说明 |
|----------|------|
| `hw/misc/xiaohui_pcu.c` | PCU 设备实现 |
| `hw/misc/Kconfig` | 添加 `CONFIG_XIAOHUI_PCU` 配置项 |
| `hw/misc/meson.build` | 添加 `xiaohui_pcu.c` 编译规则 |
| `hw/riscv/Kconfig` | `RISCV_XIAOHUI_V3` 选择 `XIAOHUI_PCU` |
| `hw/riscv/xiaohui_v3.c` | PCU memmap 定义和设备实例化 |
| `include/hw/riscv/xiaohui_v3.h` | `XIAOHUI_V3_PCU` 枚举值定义 |

##### 8.2 依赖关系
```
RISCV_XIAOHUI_V3 (hw/riscv/Kconfig)
    └── select XIAOHUI_PCU (hw/misc/Kconfig)
            └── CONFIG_XIAOHUI_PCU
                    └── xiaohui_pcu.c (hw/misc/meson.build)
```

##### 8.3 QEMU 电源管理接口依赖
PCU 使用 `target/riscv/riscv-power.h` 提供的接口：
- `riscv_cpu_release(CPUState *cpu, uint64_t rvba, int hartid)`：释放 core 并从 rvba 启动
- `riscv_cpu_set_power_off(CPUState *cpu)`：关闭 core 电源
- `riscv_cpu_set_power_on(CPUState *cpu)`：开启 core 电源

#### 9. 设备属性

| 属性 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| hart-id | uint32_t | 0 | 该 PCU 管理的 CPU hart ID |

#### 10. 与 xiaohui_ahb_cpr 的对比

| 特性 | xiaohui_ahb_cpr | xiaohui_pcu |
|------|-----------------|-------------|
| **管理粒度** | 单个实例管理所有 core | 每个 core 一个独立实例 |
| **MMIO 大小** | 64KB | 4KB per core |
| **电源模式** | 简单的 release/power off | OFF / OFF_EMU / ON / WARM_RST |
| **RVBA 配置** | 每个 core 的 RVBA 在同一寄存器空间 | 每个 PCU 实例独立的 RVBA |
| **中断支持** | 无 | 支持 trans_cmplt / invalid_sw / pchnl_deny / pchnl_noresp |
| **诊断模式** | 无 | 支持 debug_mode / debug_trans_trigger |
| **P-Channel** | 无 | 支持 P-Channel 握手超时和状态寄存器 |
| **Idle 管理** | 无 | 支持 trans_mode / wakeup_stay_on 配置 |

#### 11. 注意事项

1. **Per-core 实例**：每个 PCU 实例只管理一个 core，通过 `hart-id` 属性绑定。实例化时需确保 `hart-id` 与实际 CPU 编号一致。最大支持 16 个 PCU。
2. **地址空间**：每个 PCU 占用 4KB MMIO 区域，但 PCU 之间的地址并非连续排列，而是按 `(N/2)*0x80000 + (N%2)*0x20000` 的规律分布。
3. **复位状态**：PCU 复位后 core 处于 OFF 状态（`mode_status0 = 0x1`），需要软件显式配置 RVBA 并设置 `sw_pwr_mode = ON` 才能启动 core。
4. **WARM_RST 行为**：WARM_RST 模式会先短暂进入 WARM_RST 状态，然后自动转为 ON 状态。
5. **中断掩码**：复位后 `irq_mask0 = 0xC4`，即 `invalid_sw`、`pchnl_noresp`、`pchnl_deny` 三个中断默认被屏蔽，仅 `trans_cmplt` 中断默认使能。
6. **只读寄存器保护**：对 `pcu_mode_status0`、`pcu_pchnl_status0`、`pcu_pwr_ctrl_status0_0` 的写操作会产生 `LOG_GUEST_ERROR` 日志。
7. **仅 bare-metal 模式**：PCU 设备仅在 `!boot_linux` 模式下创建，Linux 启动模式不使用 PCU。
