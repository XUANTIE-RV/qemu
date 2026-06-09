## XuanTie QEMU RISC-V 扩展实现总结（v9.0.0-rc4 之后）

本文档总结了在 QEMU v9.0.0-rc4 之后，`xuantie-v9.0-dev` 分支上由 XuanTie 团队新增的 RISC-V 扩展实现（共 788 个 commits）。v9.0.0-rc4 及之前的内容为上游社区工作，不在本文档范围内。

---

### 一、`isa_edata_arr` 新增扩展总览

v9.0.0-rc4 基线中 `isa_edata_arr` 已有约 90 个标准扩展和 11 个 XuanTie 扩展（`xtheadba`/`bb`/`bs`/`cmo`/`condmov`/`fmemidx`/`fmv`/`mac`/`memidx`/`mempair`/`sync`）。在 `xuantie-v9.0-dev` 分支上，我们新增了 **约 110 个条目**，分为以下几类。

#### 1. 标准扩展新增注册

以下扩展为 RISC-V 标准/草案扩展，在 v9.0.0-rc4 的 `isa_edata_arr` 中尚未注册，由我们补充实现并注册：

| 扩展名 | 最低特权版本 | 说明 |
|--------|-------------|------|
| `zicfilp` | 1.13.0 | 控制流完整性 - Landing Pad |
| `zicfiss` | 1.13.0 | 控制流完整性 - Shadow Stack |
| `zilsd` | 1.12.0 | Load/Store 双字（RV32） |
| `zpsfoperand` | 1.10.0 | Packed SIMD 浮点操作数 |
| `zama16b` | 1.13.0 | 原子操作 16 字节对齐 |
| `zabha` | 1.13.0 | 字节/半字原子操作 |
| `zalasr` | 1.13.0 | Load-Acquire / Store-Release |
| `zclsd` | 1.12.0 | 压缩 Load/Store 双字 |
| `zvbc32e` | 1.12.0 | 向量位操作（32 位元素） |
| `zve32x` | 1.10.0 | 向量整数 32 位 |
| `zve64x` | 1.10.0 | 向量整数 64 位 |
| `zvkgs` | 1.12.0 | 向量加密 GHASH（短） |
| `zvqdotq` | 1.12.0 | 向量四元组点积 |
| `zimop` | 1.13.0 | May-Be-Operations |
| `zcmop` | 1.13.0 | 压缩 May-Be-Operations |
| `smcdeleg` | 1.12.0 | 计数器委托 |
| `smclic_v0p8` | 1.10.0 | CLIC v0.8（M 模式） |
| `smclic_v0p10` | 1.11.0 | CLIC v0.10（M 模式） |
| `smclicconfig` | 1.11.0 | CLIC 配置 |
| `smclicshv` | 1.11.0 | CLIC 选择性硬件向量化 |
| `ssclic` | 1.12.0 | CLIC v0.10（S 模式） |
| `smcntrpmf` | 1.12.0 | 计数器/事件特权模式过滤 |
| `smcsrind` | 1.12.0 | 间接 CSR 访问（M 模式） |
| `smdbltrp` | 1.12.0 | 双陷阱（M 模式） |
| `smmtt` | 1.13.0 | MTT 内存隔离 |
| `smsdid` | 1.13.0 | 安全域标识符 |
| `ssccfg` | 1.12.0 | 计数器配置 |
| `ssdtso` | 1.12.0 | TSO 内存模型 |
| `ssqosid` | 1.12.0 | QoS 标识符 |
| `sscsrind` | 1.12.0 | 间接 CSR 访问（S 模式） |
| `ssdbltrp` | 1.12.0 | 双陷阱（S 模式） |
| `smctr` | 1.12.0 | 控制转移记录（M 模式） |
| `ssctr` | 1.12.0 | 控制转移记录（S 模式） |
| `svrsw60t59b` | 1.13.0 | 保留写集 60:59 位 |
| `svvptc` | 1.13.0 | 虚拟化页表缓存 |
| `ssnpm` | 1.12.0 | 指针掩码（S 模式） |
| `smnpm` | 1.12.0 | 指针掩码（M 模式） |
| `smmpm` | 1.12.0 | 指针掩码（机器模式） |

此外，部分已有扩展的 `PRIV_VERSION` 被修正以匹配 XuanTie CPU 的实际支持版本：
- `zfh`/`zfhmin`：1.11.0 → 1.10.0
- `zca`/`zcb`/`zcf`/`zcd`/`zce`/`zcmp`/`zcmt`：1.12.0 → 1.10.0
- `zba`/`zbb`/`zbs`：1.12.0 → 1.10.0
- `zvfh`/`zvfhmin`：1.12.0 → 1.10.0

#### 2. XuanTie 自定义扩展新增注册

以下为 v9.0.0-rc4 中 `isa_edata_arr` **完全不存在**的 XuanTie 自定义扩展，由我们新增（所有 `PRIV_VERSION` 均为 1.10.0）：

**基础指令扩展：**

| 扩展名 | 说明 |
|--------|------|
| `xtheadaioe` | 原子 I/O 扩展 |
| `xtheadcacherpl` | Cache 替换策略控制 |
| `xtheadcbop` | Cache 块操作预取 |
| `xtheadcei` | 协处理器扩展接口（整数） |
| `xtheadcef` | 协处理器扩展接口（浮点） |
| `xtheadcev` | 协处理器扩展接口（向量） |
| `xtheadcrc` | CRC 校验指令 |
| `xtheadcvwn` | 协处理器向量宽化/窄化 |
| `xtheadfastm` | 快速内存访问 |
| `xtheadisr` | 中断服务寄存器扩展 |
| `xtheadlpw` | 加载预取字 |
| `xtheadmaee` | 内存属性扩展引擎 |
| `xtheadmdma` | 矩阵 DMA 扩展 |
| `xtheadpbmt` | 页面属性内存类型 |

**向量增强扩展：**

| 扩展名 | 说明 |
|--------|------|
| `xtheadvdot` | 向量点积扩展 |
| `xtheadvector` | XuanTie 自定义向量扩展（v0.7.1） |
| `xtheadvarith` | 向量算术 Turbo 扩展 |
| `xtheadvcoder` | 向量编解码扩展 |
| `xtheadvcrypto` | 向量加密扩展 |
| `xtheadvfcvt` | 向量浮点转换扩展 |
| `xtheadvfreduction` | 向量浮点归约扩展 |
| `xtheadvsfa` | 向量特殊函数 A |
| `xtheadvsfb` | 向量特殊函数 B |
| `xtheadvfofp4min` | 向量 FP4 最小精度浮点 |
| `xtheadvfofp6min` | 向量 FP6 最小精度浮点 |
| `xtheadvfofp8min` | 向量 FP8 最小精度浮点 |
| `xtheadvfoe8m0min` | 向量 E8M0 最小精度浮点 |

**矩阵扩展（`xtheadmatrix` 及 30 个子扩展）：**

| 扩展名 | 说明 |
|--------|------|
| `xtheadmatrix` | 矩阵扩展主入口 |
| `xtheadmbf16bf16` | 矩阵 BF16×BF16 |
| `xtheadmbf16f32` | 矩阵 BF16×F32 |
| `xtheadmbf20f32` | 矩阵 BF20×F32 |
| `xtheadmew4b` / `mew8b` / `mew16b` / `mew32b` / `mew64b` | 矩阵元素宽度 4/8/16/32/64 位 |
| `xtheadmf16f16` / `mf16f32` | 矩阵 F16×F16 / F16×F32 |
| `xtheadmf32f32` / `mf32f64` | 矩阵 F32×F32 / F32×F64 |
| `xtheadmf64f64` | 矩阵 F64×F64 |
| `xtheadmf4bf16` / `mf4f16` / `mf4f32` | 矩阵 F4×BF16/F16/F32 |
| `xtheadmf8bf16` / `mf8f16` / `mf8f32` | 矩阵 F8×BF16/F16/F32 |
| `xtheadmfp_int_cvt` | 矩阵浮点-整数转换 |
| `xtheadmi4i32` / `mi8i32` | 矩阵 INT4/INT8×INT32 |
| `xtheadmmhp` | 矩阵高精度乘法 |
| `xtheadmmxf4` / `mmxf8` / `mmxf8mxf4` | 矩阵混合精度 MXF4/MXF8 |
| `xtheadmpwfp` / `mpwint` | 矩阵逐点浮点/整数运算 |
| `xtheadmred` | 矩阵归约运算 |
| `xtheadmsf` | 矩阵特殊函数 |

**Bug 修复：** `xtheadvfofp4min` 原错误映射到 `ext_xtheadvfofp6min`，已修正为 `ext_xtheadvfofp4min`。

---

### 二、主要扩展功能实现

#### 1. XuanTie 向量扩展（XTheadVector）

- 支持 XuanTie 自定义向量指令集（v0.7.1 规范）
- 实现 BFloat16 向量支持
- 向量点积扩展（`xtheadvdot`）
- 向量浮点归约扩展（`xtheadvfreduction`），包括 `th.vfredsum.dup.32/64` 和 `th.vbfredsum.dup.32/64`
- 向量加密/编解码/算术 Turbo 扩展（`xtheadvcrypto`/`xtheadvcoder`/`xtheadvarith`）
- 向量特殊函数（`xtheadvsfa`/`xtheadvsfb`）
- 向量浮点转换（`xtheadvfcvt`）
- 低精度浮点向量扩展：FP4/FP6/FP8/E8M0（`xtheadvfofp4min`/`xtheadvfofp6min`/`xtheadvfofp8min`/`xtheadvfoe8m0min`）
- 协处理器向量宽化/窄化扩展（`xtheadcvwn`）
- 向量特殊函数 A（`xtheadvsfa`）

#### 2. XuanTie 矩阵扩展（XTheadMatrix）

- 矩阵扩展 v0.3 规范初始实现，后升级至 v0.4 规范
- v0.4 更新包括：decodetree 更新、`fwmmacc.s` 重命名为 `fmmacc.d.s`、移除 legacy matrix-GPR 指令、矩阵配置不再返回 size 到 GPR
- 独立的矩阵 `mfrm` 和 `mfflags` MCSR 字段
- 覆盖多种数据类型组合：F16/F32/F64/BF16/BF20/INT4/INT8/INT32
- 矩阵配置、乘法、归约、slide/cast-move、max/min、mn4clip 等完整指令集
- 半字节整数转换、浮点转换、混合精度乘法累加
- 双精度浮点矩阵运算（f32f64 子扩展）
- 矩阵 GDB 调试支持
- 矩阵 Load/Store 指令 trace 支持

#### 3. CLIC 中断控制器

- 完整实现 CLIC 设备（`hw/intc`）
- 支持 CLIC v0.8 和 v0.10 两个版本，区分 `ext_smclic_v0p8` / `ext_smclic_v0p10` / `ext_ssclic_v0p10`
- 新增 `smclicconfig`（CLIC 配置）和 `smclicshv`（选择性硬件向量化）子扩展
- 区分不同版本的 `XINTSTATUS` CSR 地址
- v0.10 新增：`sintthresh`/`sintstatus` 访问、`stvt` 支持、tail-chaining 中断检查
- CLIC 设备存在性检查、中断路由版本选择
- CLINT 模式兼容性
- 为 e902/e906/e907 启用 CLIC v0.8，为 r908/r908a 启用 CLIC v0.10

#### 4. SPMP / PMP 扩展

- 实现 S 模式物理内存保护（SPMP）CSR 配置与检查
- `mpmpswitch` 支持按条目的 PMP 激活控制
- `spmpswitch` / `mpmpdeleg` CSR 支持
- 为 r908a 启用 `Xtheadmpmpswitch` 扩展
- PMP 地址范围初始化修复

#### 5. 控制流完整性（Zicfilp / Zicfiss）

- `Zicfilp`：Landing Pad 指令（LPAD）、CSR 支持、trap/ret 支持
- `Zicfiss`：Shadow Stack 指令和 MMU 实现

#### 6. 控制转移记录（Smctr / Ssctr）

- CTR CSR 定义和实现
- CTR 条目记录支持
- `sctrclr` 指令
- `ctrsource`/`ctrtarget`/`ctrdata` 寄存器访问

#### 7. 双陷阱扩展（Smdbltrp / Ssdbltrp）

- M 模式和 S 模式双陷阱支持

#### 8. 指针掩码扩展（Ssnpm / Smnpm / Smmpm）

- 新增 CSR 字段
- TB flags 支持
- 地址修改函数适配指针掩码

#### 9. 计数器委托与特权模式过滤

- `smcdeleg`/`ssccfg`：计数器委托和配置支持
- `smcntrpmf`：cycle/instret 特权模式过滤
- 间接 CSR 访问（`smcsrind`/`sscsrind`）

#### 10. 内存隔离（Smmtt / Smsdid）

- MTT 内存隔离扩展实现
- 安全域标识符支持

#### 11. SFU/SFA 特殊函数指令

- 新增 SFU/SFA 指令实现并重构代码

#### 12. Packed SIMD 扩展（P 扩展）

- 支持 Packed 扩展 0.9.4 规范
- 从命令行配置和启用 Packed 扩展

#### 13. 其他 XuanTie 自定义扩展

| 扩展 | 说明 |
|------|------|
| `xtheadaioe` | 原子 I/O 扩展 |
| `xtheadcrc` | CRC 校验指令 |
| `xtheadlpw` | 加载预取字 |
| `xtheadfastm` | 快速内存访问 |
| `xtheadpbmt` | 页面属性内存类型 |
| `xtheadcbop` | Cache 块操作预取 |
| `xtheadcacherpl` | Cache 替换策略控制 |
| `xtheadmaee` | 内存属性扩展引擎 |
| `xtheadisr` | 中断服务寄存器 |
| `xtheadmdma` | 矩阵 DMA |
| `mxstatus.MM` | 非对齐访问支持 |
| `svrsw60t59b` | 保留写集 60:59 位 |

---

### 三、CPU 模型新增与更新

在 `xuantie-v9.0-dev` 分支上新增了大量 XuanTie CPU 模型定义：

**RV32 CPU 模型：**

| CPU 系列 | 具体型号 |
|----------|---------|
| **E901 系列** | e901-cp, e901zm-cp, e901b-cp, e901bzm-cp, e901plus-cp, e901plusm-cp, e901plusb-cp, e901plusbm-cp |
| **E902 系列** | e902, e902m |
| **E906 系列** | e906, e906f, e906fd, e906fdp, e906p, e906fp |
| **E907 系列** | e907, e907f, e907fd, e907fdp, e907p, e907fp |
| **C907 (RV32)** | c907-rv32, c907fd-rv32, c907fdv-rv32, c907fdvm-rv32 |
| **C908 (RV32)** | c908i-rv32, c908-rv32, c908v-rv32, 及 v2/cp/cp-xt 变体 |
| **R908 (RV32)** | r908-rv32, r908-cp-rv32, r908fd-rv32 等全系列变体, r908a-rv32 |

**RV64 CPU 模型：**

| CPU 系列 | 具体型号 |
|----------|---------|
| **C906 系列** | c906, c906fd, c906fdv |
| **C907 系列** | c907, c907fd, c907fdv, c907fdvm（含 rv64 后缀变体） |
| **C908 系列** | c908i, c908, c908v, c908i-v2, c908-v2, c908v-v2, c908vk-v2, 及 cp/cp-xt 变体 |
| **C908X 系列** | c908x, c908x-cp, c908x-cp-xt |
| **C910 系列** | c910, c910v, c910v2, c910v3, c910v3-cp, c910v3-cp-xt |
| **C920 系列** | c920, c920v2, c920v3, c920v3-cp, c920v3-cp-xt |
| **C925 系列** | c925, c925v |
| **C930 系列** | c930, c930v |
| **C960** | c960 |
| **R908 系列** | r908, r908-cp, r908-cp-xt, r908fd, r908fd-cp, r908fd-cp-xt, r908fdv, r908fdv-cp, r908fdv-cp-xt, r908fdvk-cp, r908fdvk-cp-xt |
| **R910 / R920** | r910, r920 |
| **zhijiang** | 之江 CPU（高性能） |
| **rvsp-ref** | RVSP 参考 CPU |

**CPU 模型关键配置：**
- 为所有 XuanTie CPU 添加 `mvendorid` 和 `marchid`
- 为 e906f 启用 Zc 和 B 扩展
- 为 r908a 添加 `smaia`/`ssaia`、IOPMP、Cache Lock CSR、`ssclic` 等
- 为 c930/zhijiang 启用 `svrsw60t59b`
- 为 c907fd* 启用 `zfbfmin`
- C930 支持动态 xthead 扩展控制

---

### 四、基础设施与修复

- **IOPMP**：实现 I/O 物理内存保护设备，支持 VMID 控制（`iopmp_vmid_en`/`iopmp_xtvmid_en`）
- **XuanTie CSR**：自定义 CSR 全面支持（`sxstatus`、`mxstatus`、`fxcr`、`monchipba`、`mapbaddr`、`mdtcmcr`/`mitcmcr` 等）
- **BFloat16**：FPU 和向量的 BFloat16 支持，nanbox 处理
- **Privilege 1.13**：支持 `ss1p13` 版本，新增 `MEDELEGH`/`HEDELEGH` CSR
- **版本修正**：多个扩展的 `PRIV_VERSION` 从 1.11.0/1.12.0 修正为 1.10.0，匹配 XuanTie CPU 实际支持
- **字母序排列**：`isa_edata_arr` 中所有 xthead 条目按字母序重新排列
- **Trace 支持**：内存 trace、异常 trace、调试退出 trace
- **GDB 调试**：矩阵寄存器 GDB 支持、Sstc CSR 注册修复
