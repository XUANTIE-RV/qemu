## XuanTie QEMU RISC-V Commit Changelog

**分支**: `xuantie-v9.0-dev`  
**基线**: `v9.0.0-rc4`  
**总计**: 1109 commits（包含所有目录：target/riscv、target/csky、hw、linux-user、fpu、disas 等）

> 本文档包含两部分：
> 1. **按子扩展分类的 Commit 表格**（带全局编号）
> 2. **全量 Commit 明细表**（按时间倒序，含日期、作者、子扩展、是否修复）

---

## 第一部分：按子扩展分类

### CLIC 中断控制器

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 1 | `ea7a554cbc` | target/riscv: Add riscv_clic_find_suitable_interrupt version routing | Feature | CLIC | 2026-03-12 | LIU Zhiwei |
| 4 | `9c79142f3d` | hw/intc/xt_clic_v0p10: Fix m/scliccfg parameter type to uint32_t | Fix | CLIC | 2026-03-11 | TANG Tiancheng |
| 6 | `151804e65e` | target/riscv: Fix sintstatus CSR read to preserve bit position | Fix | CLIC | 2026-03-03 | TANG Tiancheng |
| 7 | `7e886d8f3e` | target/riscv: Fix CLIC mode global consistency per spec | Fix | CLIC | 2026-03-05 | TANG Tiancheng |
| 8 | `6be7a6cf3d` | target/riscv: Fix riscv_cpu_has_work() and remove stale CLIC flag clear | Fix | CLIC | 2026-03-07 | TANG Tiancheng |
| 9 | `882dc803ca` | hw/intc: Reassert CLIC interrupt arbitration on pending bit cleared | Feature | CLIC | 2026-03-07 | TANG Tiancheng |
| 10 | `fc7462c07f` | Revert "hw/intc: Fix CLIC interrupt signal stickiness issue" | Fix | CLIC | 2026-03-07 | TANG Tiancheng |
| 11 | `4ad592a995` | Revert "target/riscv: Add exccode clean for clic" | Revert | CLIC | 2026-03-03 | TANG Tiancheng |
| 26 | `d0ed6c406e` | target/riscv: Fix wrong CSR number in read_sintstatus() | Fix | CLIC | 2026-02-12 | LIU Zhiwei |
| 37 | `d10995abe6` | target/riscv: Fix xtvec bit preservation for CLIC v0.8/v0.10 | Fix | CLIC | 2026-02-04 | TANG Tiancheng |
| 39 | `e32fc60700` | target/riscv: Enable ext_smclic_v0p8 for e902/e906/e907 CPUs | Feature | CLIC | 2026-02-03 | TANG Tiancheng |
| 41 | `455aa954e8` | target/riscv: Enable ext_smclic for r908 and r908afdvk_xt CPUs | Feature | CLIC | 2026-02-03 | TANG Tiancheng |
| 42 | `3d705edc53` | target/riscv: Distinguish XINTSTATUS CSR addresses for different CLIC versions | Feature | CLIC | 2026-02-02 | TANG Tiancheng |
| 43 | `b5a500b860` | target/riscv: Add CLIC device existence check for CLIC CSRs | Feature | CLIC | 2026-02-02 | TANG Tiancheng |
| 44 | `2f2c719e4e` | target/riscv: Check ext_smclic in clic() predicate instead of env->xt_clic_v0p8 | Feature | CLIC | 2026-02-02 | TANG Tiancheng |
| 45 | `6489d00035` | target/riscv: Rename ext_smclic/ext_ssclic to ext_smclic{_v0p8,_v0p10},ext_ssclic_v0p10 | Feature | CLIC | 2026-02-03 | TANG Tiancheng |
| 46 | `4e23b68651` | target/riscv: Remove clint_clic for clic v0.8/v.10 | Feature | CLIC | 2026-01-30 | TANG Tiancheng |
| 47 | `1fe4ac4665` | target/riscv: Rename env.clic to env.xt_clic_v0p8 for xt_clic | Feature | CLIC | 2026-01-28 | TANG Tiancheng |
| 48 | `7fbd14c8d0` | target/riscv: Fix rmw_mnxti() return value when not in CLIC mode | Fix | CLIC | 2026-02-02 | TANG Tiancheng |
| 62 | `2c74aedf20` | hw/intc: Fix CLIC interrupt signal stickiness issue | Fix | CLIC | 2026-01-27 | TANG Tiancheng |
| 63 | `85ed4595fb` | target/riscv: Add exccode clean for clic | Feature | CLIC | 2026-01-27 | TANG Tiancheng |
| 72 | `f1c010f381` | hw/timer: Improve code readability and fix clic irq connection | Fix | CLIC | 2026-01-22 | LIU Zhiwei |
| 73 | `620d892b96` | hw/intc: Always trigger a new irq selection when clic status changes | Feature | CLIC | 2026-01-22 | LIU Zhiwei |
| 75 | `09496a11e6` | hw/intc: Update active list when irq mode change | Feature | hw/intc | 2026-01-22 | LIU Zhiwei |
| 85 | `2dffe22edf` | hw/intc: Add tail-chaining check for clic v0.9 | Feature | CLIC | 2026-01-06 | TANG Tiancheng |
| 86 | `f4c9c89637` | hw/intc: Add tail-chaining check for clic v0.10 | Feature | CLIC | 2026-01-06 | TANG Tiancheng |
| 87 | `df4d5e9d82` | hw/intc: Use sintthresh instead of mintthresh in xt_clic_v0p10_next_interrupt | Feature | CLIC | 2026-01-14 | TANG Tiancheng |
| 88 | `154958716c` | hw/intc: Short-circuit nvbits check in SHV interrupt in clic 0.9/0.10 | Feature | CLIC | 2026-01-14 | TANG Tiancheng |
| 90 | `a29e8129a3` | target/riscv: Use decode_exccode to get exccode/mode/il in clic instead of encode these to exception_index | Feature | CLIC | 2026-01-06 | TANG Tiancheng |
| 125 | `e88e728218` | hw/intc: Remove sintstatus and fix level check for CLIC | Fix | CLIC | 2025-12-20 | TANG Tiancheng |
| 126 | `6406650ee1` | target/riscv: Encode hartid in the irq index for riscv_clic_set_irq | Feature | CLIC | 2025-12-19 | TANG Tiancheng |
| 127 | `3e34999176` | target/riscv: Fix bql lock in riscv_clic_set_irq | Fix | CLIC | 2025-12-19 | TANG Tiancheng |
| 131 | `7ef64608f6` | target/riscv: Interrupt compatibility with CLINT mode in XT-CLIC | Feature | CLIC | 2025-12-19 | TANG Tiancheng |
| 138 | `fa3faf3a28` | hw/riscv: Fix riscv_aclint connect to v0.10 clic | Fix | CLIC | 2025-12-16 | LIU Zhiwei |
| 155 | `4ab8b78172` | target/riscv: Add v0.10 clic sintthresh, sintstatus access | Feature | CLIC | 2025-12-05 | TANG Tiancheng |
| 156 | `51aced2477` | hw/intc: Fix the clicintattr write mask of CLIC v0.10 | Fix | CLIC | 2025-12-04 | TANG Tiancheng |
| 161 | `c1e8b7c91c` | target/riscv: Fix xnxti read/wirte in clic | Fix | CLIC | 2025-12-03 | TANG Tiancheng |
| 164 | `6b8d0fd4f2` | target/riscv: Enable ssclic for r908a | Feature | CLIC | 2025-12-03 | LIU Zhiwei |
| 165 | `a9c8935cae` | target/riscv: Add v0.10 clic stvt support | Feature | CLIC | 2025-12-03 | LIU Zhiwei |
| 166 | `84ea986492` | target/riscv: Add sub extensions for CLIC v0.10 | Feature | CLIC | 2025-12-03 | LIU Zhiwei |
| 167 | `a86ba4d60f` | target/riscv: Fix the mintthresh read and wirte in xt_clic_v0p10 | Fix | CLIC | 2025-12-03 | TANG Tiancheng |
| 169 | `90987463f9` | hw/intc: Add peripherals routing interrupts to CLIC via APLIC | Feature | CLIC | 2025-12-03 | TANG Tiancheng |
| 170 | `bfb74ea42d` | Revert "hw/intc: Add peripherals routing interrupts to CLIC via APLIC" | Revert | CLIC | 2025-12-03 | TANG Tiancheng |
| 171 | `dfa97f09fe` | hw/intc: Add peripherals routing interrupts to CLIC via APLIC | Feature | CLIC | 2025-12-01 | TANG Tiancheng |
| 172 | `812b7e1382` | hw/intc: Add error logging for WPRI field of clicintattr in clic_v0p10 | Feature | CLIC | 2025-12-01 | TANG Tiancheng |
| 173 | `3c5fecfb10` | hw/intc: Fix the clicintattr[i].mode read for clic_v0p10 | Fix | CLIC | 2025-12-01 | TANG Tiancheng |
| 174 | `6ff4905157` | Revert "hw/intc: Fix the clicintattr[i].mode read for clic_v0p10" | Fix | CLIC | 2025-12-01 | TANG Tiancheng |
| 175 | `785a0a5264` | hw/intc: Fix the clicintattr[i].mode read for clic_v0p10 | Fix | CLIC | 2025-12-01 | TANG Tiancheng |
| 181 | `25c98d98a0` | target/riscv: Lock xireg_clic as it may trigger interrupt | Feature | CLIC | 2025-11-27 | LIU Zhiwei |
| 185 | `0ccb9501e9` | targe/riscv: Fix clic v0.10 interrupt process | Fix | CLIC | 2025-11-20 | LIU Zhiwei |
| 186 | `1439255a51` | target/riscv: Fix riscv_xt_clic interfaces | Fix | CLIC | 2025-11-20 | LIU Zhiwei |
| 187 | `99e2e517fe` | hw/riscv: Fix clic v0.10 ip write and il/ipri calculation | Fix | CLIC | 2025-11-20 | LIU Zhiwei |
| 188 | `a74e817aef` | hw/riscv: Use xt_clic_v0p10 for aclint | Feature | CLIC | 2025-11-20 | LIU Zhiwei |
| 198 | `cb4296763f` | target/riscv: Fix csrw for clic 0.10 | Fix | CLIC | 2025-11-13 | LIU Zhiwei |
| 205 | `0de4cfc7fd` | target/riscv: Enable clint_clic for r908a | Feature | CLIC | 2025-11-11 | LIU Zhiwei |
| 206 | `c9617e498a` | target/riscv: Fix clic indirect csr access | Fix | CLIC | 2025-11-11 | LIU Zhiwei |
| 220 | `8620b89c06` | target/riscv: Fix v0.10 clic xireg access | Fix | CLIC | 2025-11-06 | LIU Zhiwei |
| 234 | `7a5a379676` | target/riscv: Use common clic interface for idle | Feature | CLIC | 2025-10-31 | LIU Zhiwei |
| 235 | `045f441695` | target/riscv: Use common clic interface for interrupt entry and return | Feature | CLIC | 2025-10-31 | LIU Zhiwei |
| 236 | `186616253c` | target/riscv: Add riscv_xt_clic.c for common clic interfaces | Feature | CLIC | 2025-10-31 | LIU Zhiwei |
| 237 | `4eb2c67a68` | configs/devices: Remove RISCV_CLIC as no machine use it | Feature | CLIC | 2025-10-31 | LIU Zhiwei |
| 238 | `af43ee6c40` | hw/intc: Always use interrupt level with no 1s padding | Feature | hw/intc | 2025-10-31 | LIU Zhiwei |
| 239 | `d69c3ba43c` | target/riscv: Add new smode CSR for v0.10 CLIC | Feature | CLIC | 2025-10-30 | LIU Zhiwei |
| 241 | `8488cf3571` | hw/riscv: Add v0.10 clic to xiaohuiv2 | Feature | CLIC | 2025-10-30 | LIU Zhiwei |
| 242 | `fbf26efbd8` | target/riscv: Add CSR support for v0.10 CLIC | Feature | CLIC | 2025-10-30 | LIU Zhiwei |
| 243 | `5fb54d4064` | hw/riscv: Fix xt_clic_v0p10_is_clic_mode | Fix | CLIC | 2025-10-30 | LIU Zhiwei |
| 246 | `f5f8f2f7da` | target/riscv: Support indirect CSR access for clic 0.10 | Feature | CLIC | 2025-10-27 | LIU Zhiwei |
| 247 | `17e8417966` | hw/intc: Support CLIC 0.10 specification | Feature | CLIC | 2025-10-27 | LIU Zhiwei |
| 248 | `88fe59415b` | hw/riscv: Fix clic int level according to hardware | Fix | CLIC | 2025-10-23 | LIU Zhiwei |
| 249 | `f79333f1eb` | hw/riscv: Fix clic mintthresh mth | Fix | CLIC | 2025-10-23 | LIU Zhiwei |
| 567 | `0416ffd7aa` | target/riscv: Don't update mip in CLIC mode | Feature | CLIC | 2025-03-27 | LIU Zhiwei |
| 568 | `d269df1855` | Merge branch b033459396a59ff661f6240d00816ee0211783d8 into xuantie-v9.0-dev Title: hw/intc: Fix ip write error in CLIC | Fix | CLIC | 2025-03-27 | lzw194868 |
| 569 | `b033459396` | hw/intc: Fix ip write error in CLIC | Fix | CLIC | 2025-03-27 | LIU Zhiwei |
| 696 | `f30fa0ce79` | hw/timer: Connect timer to all clic regions if need | Feature | CLIC | 2024-12-12 | LIU Zhiwei |
| 802 | `0ffbf94058` | hw/dummyh: Split plic_irqs and clic_irqs | Feature | CLIC | 2024-09-07 | LIU Zhiwei |
| 818 | `75eefd8ccb` | target/riscv: Fix mtvec for only CLIC mode | Fix | CLIC | 2024-08-29 | LIU Zhiwei |
| 819 | `2cbe490f3c` | hw/csky: Set clic_irq to NULL for csky | Feature | CLIC | 2024-08-29 | LIU Zhiwei |
| 820 | `fc0e333c07` | hw/csky_uart: Only set clic_irq when it is not NULL | Feature | CLIC | 2024-08-29 | LIU Zhiwei |
| 822 | `dab9298245` | hw/char: Fix set clic_irq at the same time | Fix | CLIC | 2024-08-29 | LIU Zhiwei |
| 823 | `e2e6c53f95` | hw/xt_clic: Hardwire clic to machine mode | Feature | CLIC | 2024-08-28 | LIU Zhiwei |
| 827 | `9a010befd1` | hw/xt_clic: Update clic base to 0xc010000 | Feature | CLIC | 2024-08-27 | LIU Zhiwei |
| 828 | `b59a27af1c` | hw/xt_clic: Fix error mask for mmio | Fix | CLIC | 2024-08-27 | LIU Zhiwei |
| 829 | `df4f7bd9ea` | hw/xt_clic: Support less than 4 byte rw on common mmio | Feature | CLIC | 2024-08-27 | LIU Zhiwei |
| 830 | `6434600e1b` | target/riscv: Using strict detect for clic as we may in clint mode | Feature | CLIC | 2024-08-26 | LIU Zhiwei |
| 831 | `fde495abf1` | hw/xt_clic: Fix irqs number | Fix | CLIC | 2024-08-26 | LIU Zhiwei |
| 833 | `dc9dd1c5fe` | hw/xt_clic: Remove a typo | Feature | CLIC | 2024-08-26 | LIU Zhiwei |
| 835 | `63be55d768` | hw/xt_clic: Allow more than 1 byte read | Feature | CLIC | 2024-08-22 | LIU Zhiwei |
| 836 | `8a76c3f640` | target/riscv: Always expose clic CSRs for cpu with clit_clic | Feature | CLIC | 2024-08-21 | LIU Zhiwei |
| 837 | `514e9dc67a` | hw/xt_clic: We can't use current cpu when debug | Feature | CLIC | 2024-08-21 | LIU Zhiwei |
| 838 | `21dc2c7beb` | target/riscv: Support mscratchcsl for CLIC | Feature | CLIC | 2024-08-19 | LIU Zhiwei |
| 839 | `48c4abb28f` | target/riscv: Support mscratchcsw for CLIC | Feature | CLIC | 2024-08-19 | LIU Zhiwei |
| 840 | `840d558fb5` | target/riscv: Support mclicbase for CLIC | Feature | CLIC | 2024-08-19 | LIU Zhiwei |
| 841 | `536a416ad1` | target/riscv: Support mnxti in CLIC | Feature | CLIC | 2024-08-19 | LIU Zhiwei |
| 842 | `44a158d300` | target/riscv: Add a clint_clic status for r908 | Feature | CLIC | 2024-08-19 | LIU Zhiwei |
| 843 | `bdf3e37bd7` |  hw/csky_timer: Attach to another clic port | Feature | CLIC | 2024-08-19 | LIU Zhiwei |
| 851 | `49b4a682be` | hw/intc/clic: Remove one debug print | Feature | CLIC | 2024-08-18 | LIU Zhiwei |
| 852 | `b11b83589c` | hw/csky_uart: Attach to another clic port | Feature | CLIC | 2024-08-18 | LIU Zhiwei |
| 854 | `202cbceafa` | hw/riscv: Xiaohui support Xuantie CLIC | Feature | CLIC | 2024-08-16 | LIU Zhiwei |
| 855 | `940bf901b6` | hw/intc: Support Xuantie CLIC | Feature | CLIC | 2024-08-15 | LIU Zhiwei |
| 1083 | `28004c91f8` | target/riscv: Log clic INT | Feature | CLIC | 2024-06-06 | Huang Tao |
| 1084 | `eb2ae18044` | target/riscv: Update interrupt return in CLIC mode | Feature | CLIC | 2024-06-06 | Huang Tao |
| 1085 | `d47c216cb9` | target/riscv: Update interrupt handling in CLIC mode | Feature | CLIC | 2024-06-06 | Huang Tao |
| 1086 | `f7cdda8dcc` | target/riscv: Update CSR xnxti in CLIC mode | Feature | CLIC | 2024-06-06 | Huang Tao |
| 1087 | `6b2d470156` | target/riscv: Update CSR xtvt in CLIC mode | Feature | CLIC | 2024-06-06 | Huang Tao |
| 1088 | `fd2d41f154` | target/riscv: Update CSR xtvec in CLIC mode | Feature | CLIC | 2024-06-06 | Huang Tao |
| 1089 | `23a077aefc` | target/riscv: Update CSR xip in CLIC mode | Feature | CLIC | 2024-06-06 | Huang Tao |
| 1090 | `56c1f25c56` | target/riscv: Update CSR xie in CLIC mode | Feature | CLIC | 2024-06-06 | Huang Tao |
| 1091 | `b1634ea9bf` | hw/intc: Add CLIC device | Feature | CLIC | 2024-06-06 | Huang Tao |
| 1092 | `8057e23031` | target/riscv: Add CLIC CSRs | Feature | CLIC | 2024-06-05 | Huang Tao |

---

### CPU 模型

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 2 | `6d052dba7b` |  target/riscv: Add r908fdvk-cp and r908fdvk-cp-xt CPU models | Feature | R908A | 2026-03-11 | LIU Zhiwei |
| 3 | `cb15849d0e` | target/riscv: Add c925 cpu | Feature | C925 | 2026-03-11 | LIU Zhiwei |
| 57 | `5a09fbd004` | target/riscv: Add marchid for Xuantie cpu | Feature | CPU | 2026-01-29 | LIU Zhiwei |
| 68 | `9747ec257a` | hw/riscv: Add xiaohui_v3 support for c930 | Feature | C930 | 2026-01-26 | LIU Zhiwei |
| 71 | `27a61f263f` | target/riscv: Add smaia and ssaia for r908a | Feature | R908A | 2026-01-22 | LIU Zhiwei |
| 79 | `102ad81a38` | target/riscv: Enable zc and b for e906f | Feature | E90x | 2026-01-13 | LIU Zhiwei |
| 99 | `11dff1829d` | target/riscv: Replace the cpu r908a type determination with object_dynamic_cast | Feature | R908A | 2025-12-30 | TANG Tiancheng |
| 108 | `1dfda88721` | target/riscv: Model monchipba and mapbaddr for r908a | Feature | R908A | 2025-12-26 | LIU Zhiwei |
| 112 | `62221488d9` | target/riscv: Add dynamic control for xthead extensions for c930 | Feature | C930 | 2025-12-23 | TANG Tiancheng |
| 118 | `ccf07051ab` | target/riscv: Add tcm_split to riscv_cpu_properties | Feature | CPU | 2025-12-19 | TANG Tiancheng |
| 119 | `6bc887f8b4` | target/riscv: Use c910 instead of c910v in xml | Feature | C910 | 2025-12-24 | LIU Zhiwei |
| 141 | `62edd7f8ce` | hw/riscv: Add default cpu to r908afdvk-xt | Feature | R908A | 2025-12-15 | LIU Zhiwei |
| 142 | `28819e83ef` | target/riscv: Add miss c908vk-cp_v2 cpu | Feature | C908 | 2025-12-12 | LIU Zhiwei |
| 145 | `ca2e6fd0c4` | target/riscv: Add c908vk_v2/c908vk-xt_v2 support | Feature | C908 | 2025-12-10 | TANG Tiancheng |
| 150 | `4b7a7c91e8` | target/riscv: Add the default size of mdtcmcr/mitcmcr for r908a | Feature | R908A | 2025-12-08 | TANG Tiancheng |
| 168 | `3c4d9101a2` | target/riscv: Add pmu_mask init for rv32_r908afdvk_xt_cpu_init | Feature | R908A | 2025-12-02 | TANG Tiancheng |
| 212 | `d8b73e7d6a` | target/riscv: Fix R908A CSR encoding | Fix | R908A | 2025-11-10 | LIU Zhiwei |
| 217 | `cf52873d57` | target/riscv: Update r908a priv version to 1.13 | Feature | R908A | 2025-11-07 | LIU Zhiwei |
| 225 | `3a0d4cb262` | target/riscv: Add new extensions for r908afdv-xt | Feature | R908A | 2025-09-04 | LIU Zhiwei |
| 226 | `c8eb2cba16` | target/riscv: Support r908afdvk-xt | Feature | R908A | 2025-08-12 | LIU Zhiwei |
| 227 | `63af90b24d` | target/riscv: Enable zfa for r908afdv-xt | Feature | R908A | 2025-08-08 | LIU Zhiwei |
| 228 | `6f23f689e2` | target/riscv: Add Zicsr for r908afdv-xt | Feature | R908A | 2025-08-08 | LIU Zhiwei |
| 229 | `59369fc3fa` | target/riscv: Use r908afdv-xt cpu as HRD requires | Feature | R908A | 2025-08-07 | LIU Zhiwei |
| 230 | `d95364f091` | target/riscv: Add some custom CSR for r908a | Feature | R908A | 2025-08-04 | LIU Zhiwei |
| 231 | `c43083ea67` | target/riscv: Add experimental r908a cpu | Feature | R908A | 2025-08-04 | LIU Zhiwei |
| 281 | `5a2a2f2ca1` | target/riscv: Fix c930 cpu names | Fix | C930 | 2025-09-25 | LIU Zhiwei |
| 288 | `9a6768fa80` | target/riscv: Ignore C930 CSRs we don't support | Feature | C930 | 2025-09-15 | LIU Zhiwei |
| 289 | `b29df72fb5` | target/riscv: Support C930 cpu | Feature | C930 | 2025-09-15 | LIU Zhiwei |
| 325 | `8f62011d49` | hw/riscv: Set max cpus of xiaohui to 16 | Feature | CPU | 2025-08-28 | LIU Zhiwei |
| 354 | `2340721348` | target/riscv: Make hpm19-31 readonly zero for c908x | Feature | C908 | 2025-08-13 | LIU Zhiwei |
| 420 | `f626a55826` | target/riscv: Set vlen 256 for zhijiang | Feature | zhijiang | 2025-06-25 | LIU Zhiwei |
| 437 | `ce362794c7` | hw/riscv: Set zhijiang as valid cpu for rvsp-ref | Feature | zhijiang | 2025-06-16 | LIU Zhiwei |
| 441 | `f263bd56fc` | target/riscv: Add jvt property for e901 | Feature | E90x | 2025-06-13 | LIU Zhiwei |
| 443 | `30d868424e` | target/riscv: Enable svvptc for zhijiang | Feature | zhijiang | 2025-06-10 | LIU Zhiwei |
| 445 | `42c7c9045b` | target/riscv: Disable zihpm for e901 | Feature | E90x | 2025-06-09 | LIU Zhiwei |
| 453 | `d0d7815aa8` | target/riscv: Fix mtvec/mtvt setting for e901 rename | Fix | E90x | 2025-06-06 | LIU Zhiwei |
| 454 | `966ad37d82` | target/riscv: Rename e901 | Feature | E90x | 2025-06-05 | LIU Zhiwei |
| 464 | `4612f242e0` | target/riscv: Support coprocessor for zhijiang | Feature | zhijiang | 2025-06-02 | LIU Zhiwei |
| 480 | `4203c3e648` | target/riscv: Support mtvt and mtvec for e901mini | Feature | E90x | 2025-05-23 | LIU Zhiwei |
| 487 | `57bdf3bce7` | target/riscv: Support e901/e901mini | Feature | E90x | 2025-05-14 | LIU Zhiwei |
| 514 | `f4d9c54441` | target/riscv: Remove c908x rv32 support | Feature | C908 | 2025-04-15 | LIU Zhiwei |
| 571 | `3fe72d7e28` | target/riscv: Fix cop subsets in cpu | Fix | CPU | 2025-03-27 | LIU Zhiwei |
| 573 | `aa4a88d016` | target/riscv: Support sscofpmf for c908/r908/c908x/c908v2 | Feature | R908A | 2025-03-26 | LIU Zhiwei |
| 574 | `08b4c6f0a2` | target/riscv: Fix c908x-cp isa model | Fix | C908 | 2025-03-26 | LIU Zhiwei |
| 575 | `17e25322cb` | target/riscv: Support r908/c908/c910/c920-cp-xt | Feature | R908A | 2025-03-26 | LIU Zhiwei |
| 582 | `e7102f895f` | target/riscv: Fix coprocessor encoding | Fix | CPU | 2025-03-19 | LIU Zhiwei |
| 583 | `84fc64a33c` | target/riscv: Update mcmov* encoding | Feature | CPU | 2025-03-19 | LIU Zhiwei |
| 605 | `303824212e` | target/riscv: Fix c908v2 program model | Fix | C908 | 2025-02-17 | LIU Zhiwei |
| 607 | `b3d96d725a` | target/riscv: Add correct marchid for zhijiang | Feature | zhijiang | 2025-02-14 | LIU Zhiwei |
| 610 | `200bdfcefe` | target/riscv: Set c908x default vlen to 1024 | Feature | C908 | 2025-02-14 | LIU Zhiwei |
| 616 | `3775bb3636` | target/riscv: Add c908_v2/c908cp-v2 support | Feature | C908 | 2025-02-12 | LIU Zhiwei |
| 628 | `6dd2a8ce61` | target/riscv: Set max vlen to 4096 | Feature | CPU | 2025-01-20 | LIU Zhiwei |
| 630 | `38e6454b33` | target/riscv: Fix c908x convert instruction check | Fix | C908 | 2025-01-20 | LIU Zhiwei |
| 681 | `bd08ccac0c` | target/riscv: Set marchid for c908x | Feature | C908 | 2024-12-24 | LIU Zhiwei |
| 683 | `ae8574aaf2` | target/riscv: Add c908x csr | Feature | C908 | 2024-12-24 | LIU Zhiwei |
| 690 | `26cb94c5d4` | target/riscv: Support c908x | Feature | C908 | 2024-12-23 | LIU Zhiwei |
| 692 | `66af3ce58d` | target/riscv: C908x is based on r908fdv | Feature | R908A | 2024-12-23 | LIU Zhiwei |
| 698 | `e4d5067bbd` | target/riscv: Add c908x cpu | Feature | C908 | 2024-12-19 | LIU Zhiwei |
| 718 | `e976e15266` | target/riscv: Use a nonzero mimpid for zhijiang | Feature | zhijiang | 2024-12-05 | LIU Zhiwei |
| 763 | `2a9fa949e9` | target/riscv: Add a temp arch id for zhijiang | Feature | zhijiang | 2024-10-30 | LIU Zhiwei |
| 764 | `30dce35e17` | target/riscv: Fix add RVC to zhijiang | Fix | zhijiang | 2024-10-30 | LIU Zhiwei |
| 790 | `07c4d8278e` | target/riscv: Update marchid for c908 and r908 | Feature | R908A | 2024-09-21 | LIU Zhiwei |
| 821 | `b73da07d9e` | target/riscv: Add cpu for r908-rv32 | Feature | R908A | 2024-08-29 | LIU Zhiwei |
| 825 | `3d17b1f7f4` | target/riscv: Update priv spec to 1.13 for c910v3 | Feature | C910 | 2024-08-28 | LIU Zhiwei |
| 849 | `1176893a75` | target/riscv: Add tcm support for r908 | Feature | R908A | 2024-08-18 | LIU Zhiwei |
| 850 | `97cce5bece` | target/riscv: Initialize e907 TCM to 32KB | Feature | E90x | 2024-08-18 | LIU Zhiwei |
| 853 | `9dc073a6b1` | target/riscv: add r908 CPU type and initialization | Feature | R908A | 2024-08-15 | Huang Tao |
| 858 | `3d0907793c` | target/riscv: Add coprocessor support in c910v3_cp/c920v3_cp | Feature | C920 | 2024-08-12 | LIU Zhiwei |
| 859 | `9792861349` | target/riscv: Add Xuantie coprocessor support | Feature | CPU | 2024-08-12 | LIU Zhiwei |
| 860 | `008a761413` | target/riscv: Add c910v3/c920v3 cpu support | Feature | C920 | 2024-08-12 | LIU Zhiwei |
| 861 | `efa89d4478` | target/riscv: Support rvb23 profile | Feature | RVB23 | 2024-08-12 | LIU Zhiwei |
| 864 | `d724ff4f94` | target/riscv: Add a cpu named zhijiang | Feature | zhijiang | 2024-08-01 | LIU Zhiwei |
| 1067 | `d885f9064a` | target/riscv: Reset MPP to 3 as Xuantie CPUs do | Feature | CPU | 2024-06-14 | Huang Tao |
| 1081 | `3ea250e529` | target/riscv: update max cpu | Feature | CPU | 2024-06-06 | Huang Tao |
| 1096 | `290e880ff6` | target/riscv: Add Xuantie CPUs support | Feature | CPU | 2024-06-05 | Huang Tao |

---

### RISC-V 其他

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 5 | `c22eb1176c` | target/riscv: Remove mtvec dependency from stvec read | Feature | target/riscv | 2026-03-10 | TANG Tiancheng |
| 30 | `e90c0a9e2f` | target/riscv: Fix snxti | Fix | target/riscv | 2026-02-10 | LIU Zhiwei |
| 33 | `4f46051345` | target/riscv: Fix irq loss when xstatus.xie enabled | Fix | target/riscv | 2026-02-06 | LIU Zhiwei |
| 38 | `607c1e2f0b` | target/riscv: Fix reserved mode (0b10) error handling in clint mode | Fix | target/riscv | 2026-02-04 | TANG Tiancheng |
| 58 | `e283e82d25` | target/riscv: Extend cpu name length for dynamic cpu | Feature | target/riscv | 2026-01-28 | LIU Zhiwei |
| 89 | `d91a9fb5bd` | target/riscv: Fix scause update in rmw_mnxti | Fix | target/riscv | 2026-01-06 | TANG Tiancheng |
| 95 | `5225397a94` | target/riscv: Fix function 'smode' parameter passing | Fix | target/riscv | 2026-01-04 | TANG Tiancheng |
| 114 | `4aaafd2dd7` | target/riscv: Fix CPURISCVTBFlags.flags2 type mismatch | Fix | target/riscv | 2025-12-23 | TANG Tiancheng |
| 121 | `8598e8b7f9` | target/riscv: Fix xtvec write | Fix | target/riscv | 2025-12-24 | LIU Zhiwei |
| 133 | `37b99b7da4` | target/riscv: fix exccode clearing in rmw_mnxti | Fix | target/riscv | 2025-12-18 | TANG Tiancheng |
| 147 | `eb83930721` | target/riscv: Fix mmext_fp_cvt for high part only insn | Fix | target/riscv | 2025-12-10 | LIU Zhiwei |
| 149 | `a2f2d000c8` | target/riscv: Add xt_pmaaddr32[0-31] and rename xt_pmaaddr to xt_pmaaddr64 | Feature | target/riscv | 2025-12-09 | TANG Tiancheng |
| 157 | `0e1692f1be` | target/riscv: Fix mmext_fp_cvt for ms1==md case | Fix | target/riscv | 2025-12-05 | LIU Zhiwei |
| 179 | `89a97ba400` | Revert "target/riscv: Nanbox for mffloor" | Revert | target/riscv | 2025-11-28 | LIU Zhiwei |
| 191 | `d87bf0093e` | target/riscv: Nanbox for mffloor | Feature | target/riscv | 2025-11-14 | LIU Zhiwei |
| 203 | `689fcd8904` | target/riscv: Only use rs1 log2(rlen-elen) bit | Feature | target/riscv | 2025-11-11 | LIU Zhiwei |
| 298 | `fc522e3f99` | target/riscv: Give different names to each TCM MR and AS | Feature | target/riscv | 2025-09-05 | LIU Zhiwei |
| 330 | `f8b60bd750` | target/riscv: Free temp dest buffer | Feature | target/riscv | 2025-08-15 | LIU Zhiwei |
| 331 | `7e17daa6f8` | target/riscv: Fix whole reduction operations | Fix | target/riscv | 2025-08-15 | LIU Zhiwei |
| 343 | `811a6bfe93` | target/riscv: Always init src1 | Feature | target/riscv | 2025-08-14 | LIU Zhiwei |
| 347 | `65b26ee0c3` | target/riscv: Add abs parameter for xt reduction | Feature | target/riscv | 2025-08-14 | LIU Zhiwei |
| 348 | `afaac935ab` | target/riscv: Rename base to init_val for xt reduction | Feature | target/riscv | 2025-08-14 | LIU Zhiwei |
| 351 | `2e357be80c` | target/riscv: Remove row index from xt reduction funcs | Feature | target/riscv | 2025-08-13 | LIU Zhiwei |
| 352 | `cde028a49b` | target/riscv: Add base element for xt reduction | Feature | target/riscv | 2025-08-13 | LIU Zhiwei |
| 353 | `df34453942` | target/riscv: Add row len parameter for xt reduction | Feature | target/riscv | 2025-08-13 | LIU Zhiwei |
| 355 | `5ff719bf0b` | target/riscv: Add frm and sat parameters for xt reduction funcs | Feature | target/riscv | 2025-08-13 | LIU Zhiwei |
| 356 | `651136232b` | target/riscv: Move xuantie reduction to xt_reduction.c | Feature | target/riscv | 2025-08-13 | LIU Zhiwei |
| 369 | `fd33ea0e22` | target/riscv: Use stvec_hs for debug in vs mode | Feature | target/riscv | 2025-08-08 | LIU Zhiwei |
| 382 | `0f099741af` | target/riscv: Always init oprd_b to quiet compiler warning | Feature | target/riscv | 2025-07-28 | LIU Zhiwei |
| 384 | `bd1bf05121` | target/riscv: Fix mfcvt*.e5.s | Fix | target/riscv | 2025-07-26 | LIU Zhiwei |
| 385 | `d6a88e224e` | target/riscv: Fix mfcvth.d.s encoding | Fix | target/riscv | 2025-07-26 | LIU Zhiwei |
| 386 | `80bb43d600` | target/riscv: Support bf16 for pointwise | Feature | target/riscv | 2025-07-26 | LIU Zhiwei |
| 387 | `c1277a6955` | target/riscv: target/riscv: Support mc.i and mc for fused pointwise | Feature | target/riscv | 2025-07-25 | LIU Zhiwei |
| 388 | `d6ab4c43d3` | target/riscv: target/riscv: Support mc.i and mc for fp pointwise | Feature | target/riscv | 2025-07-25 | LIU Zhiwei |
| 389 | `370b0760b1` | target/riscv: Support mv for float pointwise | Feature | target/riscv | 2025-07-25 | LIU Zhiwei |
| 390 | `6f981cbe06` | target/riscv: Support mc.i and mc for int pointwise | Feature | target/riscv | 2025-07-25 | LIU Zhiwei |
| 391 | `7ed1ee08e5` | target/riscv: Support mfma | Feature | target/riscv | 2025-07-24 | LIU Zhiwei |
| 397 | `d881466e26` | target/riscv: Pass utn to fpstatus as saturate flag | Feature | target/riscv | 2025-07-11 | LIU Zhiwei |
| 404 | `56d4ebec80` | target/riscv: Fix compile warning | Fix | target/riscv | 2025-07-06 | LIU Zhiwei |
| 405 | `5ff560879d` | target/riscv: Fix mfence.spa encode | Fix | target/riscv | 2025-07-04 | LIU Zhiwei |
| 406 | `52ea27a7a3` | target/riscv: Enable  M mode when debug | Feature | target/riscv | 2025-07-04 | LIU Zhiwei |
| 414 | `5db19b9c4e` | target/riscv: Fix mx colidx setting | Fix | target/riscv | 2025-06-25 | LIU Zhiwei |
| 415 | `39d9f11099` | target/riscv: Fix set_elem_p | Fix | target/riscv | 2025-06-25 | LIU Zhiwei |
| 431 | `89dbe7bb79` | target/riscv: Fix mfcvth/l.h.e4 encoding | Fix | target/riscv | 2025-06-19 | LIU Zhiwei |
| 438 | `3c085c1268` | target/riscv: Support altfmt field in vtype | Feature | target/riscv | 2025-06-16 | LIU Zhiwei |
| 442 | `219c9df0e3` | target/riscv: Default mtvec set to 0x43 | Feature | target/riscv | 2025-06-11 | LIU Zhiwei |
| 447 | `bf23050131` | target/riscv: Fix mtvec/mtvt for other cpus | Fix | target/riscv | 2025-06-09 | LIU Zhiwei |
| 449 | `22dc94c62b` | target/riscv: Set default mtvt and mtvec | Feature | target/riscv | 2025-06-09 | LIU Zhiwei |
| 451 | `17e4e12f15` | target/riscv: Fix mtvt/mtvec prop not exist error | Fix | target/riscv | 2025-06-06 | LIU Zhiwei |
| 455 | `8033e03582` | target/riscv: Fix smmpt64 | Fix | target/riscv | 2025-06-03 | LIU Zhiwei |
| 457 | `96fc089834` | target/riscv: Fix a coverity false warning | Fix | target/riscv | 2025-06-02 | LIU Zhiwei |
| 458 | `739825d9d8` | target/riscv: Fix froundnx.h | Fix | target/riscv | 2025-06-02 | LIU Zhiwei |
| 459 | `550e4cb809` | target/riscv: Fix aia logically dead code by coverity | Fix | target/riscv | 2025-06-02 | LIU Zhiwei |
| 460 | `0ffc7a066d` | target/riscv: Fix logically dead code by coverity | Fix | target/riscv | 2025-06-02 | LIU Zhiwei |
| 465 | `2f38398a67` | target/riscv: Fix vqdotsu/us | Fix | target/riscv | 2025-06-02 | LIU Zhiwei |
| 467 | `d7605534e4` | target/riscv: Use mew*b check for xtheadfp4 and xtheadint4 | Feature | target/riscv | 2025-05-31 | LIU Zhiwei |
| 468 | `28e6bc913c` | target/riscv: Use v0.5 instruction names for pw | Feature | target/riscv | 2025-05-31 | LIU Zhiwei |
| 469 | `44dd059722` | target/riscv: Use mew*b subextensions for check | Feature | target/riscv | 2025-05-31 | LIU Zhiwei |
| 470 | `e660242d72` | target/riscv: Add point wise width subextension | Feature | target/riscv | 2025-05-31 | LIU Zhiwei |
| 471 | `99ec7db58e` | target/riscv: Fix napot check when N bit is zero | Fix | target/riscv | 2025-05-30 | LIU Zhiwei |
| 472 | `4efbf4d719` | target/riscv: Fix style check error for mpt | Fix | target/riscv | 2025-05-30 | LIU Zhiwei |
| 473 | `8007595019` | target/riscv: Fix pasid update error | Fix | target/riscv | 2025-05-29 | yfy_lazy |
| 474 | `a4bad99fff` | target/riscv: Fix an warning | Fix | target/riscv | 2025-05-29 | LIU Zhiwei |
| 475 | `5b81863303` | target/riscv: Support smmptnlnapot | Feature | target/riscv | 2025-05-28 | LIU Zhiwei |
| 477 | `2dfd53b499` | target/riscv: Update mpt lookup to v0.3.4 | Feature | target/riscv | 2025-05-28 | LIU Zhiwei |
| 478 | `ae5b23ff81` | target/riscv: Save opcode for wrs.nto as it may be illegal | Feature | target/riscv | 2025-05-26 | LIU Zhiwei |
| 481 | `25e509592b` | target/riscv: Zce depends on at least 1.12 priv spec | Feature | target/riscv | 2025-05-22 | LIU Zhiwei |
| 482 | `f3de86df73` | target/riscv: Fix ctx->f4f32 flag | Fix | target/riscv | 2025-05-22 | LIU Zhiwei |
| 483 | `c5f40c82ae` | target/riscv: Fix mfcvth/l.s.h encoding | Fix | target/riscv | 2025-05-21 | LIU Zhiwei |
| 484 | `77a5f3bbd9` | target/riscv: Fix float-point conversion instructions check | Fix | target/riscv | 2025-05-21 | LIU Zhiwei |
| 489 | `99a07d6662` | target/riscv: Support int4 to float8 | Feature | target/riscv | 2025-05-13 | LIU Zhiwei |
| 498 | `5373c3720e` | target/riscv: Support clean persist check bit | Feature | target/riscv | 2025-04-27 | LIU Zhiwei |
| 501 | `17c7411820` | target/riscv: Always use mxa to calculate total elements | Feature | target/riscv | 2025-04-21 | LIU Zhiwei |
| 504 | `ca9e274b40` | target/riscv: Fix l_k2_blocksize | Fix | target/riscv | 2025-04-18 | LIU Zhiwei |
| 505 | `83bcb30f5e` | target/riscv: Support mxa/b with different blksize | Feature | target/riscv | 2025-04-18 | LIU Zhiwei |
| 526 | `b24366728f` | target/riscv: Parse csky-extend/cpu-prop opts for each cpu | Feature | target/riscv | 2025-04-10 | LIU Zhiwei |
| 530 | `ed0ec1956b` | target/riscv: Support fp8/fp4 conversion | Feature | target/riscv | 2025-04-08 | LIU Zhiwei |
| 532 | `f32c18e2ab` | target/riscv: Fix fp4 hp check | Fix | target/riscv | 2025-04-08 | LIU Zhiwei |
| 535 | `c183a76959` | target/riscv: Support fp half-precison extension | Feature | target/riscv | 2025-04-08 | LIU Zhiwei |
| 585 | `d7a23fc707` | target/riscv: Set tail and masked elements to 1 | Feature | target/riscv | 2025-03-18 | LIU Zhiwei |
| 589 | `267102ecd4` | target/riscv: Fix frac mask setting when not overflow | Fix | target/riscv | 2025-03-06 | LIU Zhiwei |
| 591 | `35d0fc88c7` | target/riscv: Fix xt_round_* | Fix | target/riscv | 2025-03-06 | LIU Zhiwei |
| 612 | `79b0d2187a` | target/riscv: Fix e5 inf encoding | Fix | target/riscv | 2025-02-12 | LIU Zhiwei |
| 618 | `6ea3c36d91` | target/riscv: Fix fresum.64.w calculation | Fix | target/riscv | 2025-02-11 | LIU Zhiwei |
| 619 | `5f965308b7` | target/riscv: Fix lmul check for vfreduction | Fix | target/riscv | 2025-02-11 | LIU Zhiwei |
| 633 | `92245e183f` | target/riscv: Add isa check for Xuantie trubo | Feature | target/riscv | 2025-01-16 | LIU Zhiwei |
| 643 | `b6ae8b7a39` | target/riscv: Support Xuantie vcrypto extension | Feature | target/riscv | 2025-01-07 | LIU Zhiwei |
| 649 | `1ae58724ea` | target/riscv: Fix wfe | Fix | target/riscv | 2024-12-30 | LIU Zhiwei |
| 650 | `74c3331a31` | target/riscv: Fix denormal_shift may be used uninitialized | Fix | target/riscv | 2024-12-30 | LIU Zhiwei |
| 651 | `3b1d23c409` | target/riscv: Allow exec wfe in user mode | Feature | target/riscv | 2024-12-30 | LIU Zhiwei |
| 656 | `8d955e85ba` | target/riscv: Fix denormal exp_max align | Fix | target/riscv | 2024-12-30 | LIU Zhiwei |
| 658 | `82ae39aeb0` | target/riscv: Fix th.vfrec.v for inf | Fix | target/riscv | 2024-12-28 | LIU Zhiwei |
| 666 | `f5a9a9611f` | target/riscv: Don't overide special result in round | Feature | target/riscv | 2024-12-27 | LIU Zhiwei |
| 668 | `55a75896dc` | target/riscv: Increse f->exp when fraction overflow after round | Feature | target/riscv | 2024-12-27 | LIU Zhiwei |
| 679 | `185f3108f0` | target/riscv: Using 4M or 2M pages according to mxl | Feature | target/riscv | 2024-12-24 | LIU Zhiwei |
| 706 | `261867ec45` | target/riscv: Add *max_c_* instructions | Feature | target/riscv | 2024-12-12 | LIU Zhiwei |
| 707 | `a0efb884a9` | target/riscv: Add *_64 max dup instructions | Feature | target/riscv | 2024-12-12 | LIU Zhiwei |
| 759 | `f19bd0ae66` | target/riscv: Only clean edge pending when cpu ack | Feature | target/riscv | 2024-11-05 | LIU Zhiwei |
| 760 | `41ae9a4f95` | target/riscv: Allow write to htinst in M mode | Feature | target/riscv | 2024-11-04 | LIU Zhiwei |
| 765 | `bf9fc79957` | target/riscv: Fix MDT field define | Fix | target/riscv | 2024-10-29 | LIU Zhiwei |
| 778 | `42edb99fef` | target/riscv: Fix server platform compile | Fix | target/riscv | 2024-10-23 | LIU Zhiwei |
| 779 | `a10eb6e535` | target/riscv: Add server platform reference cpu | Feature | target/riscv | 2024-03-04 | Fei Wu |
| 782 | `220fa0a7ce` | target/riscv: Fix an g-stage-fail when merge cfi patch | Fix | target/riscv | 2024-10-16 | LIU Zhiwei |
| 789 | `a58d6f3b32` | target/riscv: Expose vlen and elen for vendor cpus | Feature | target/riscv | 2024-09-23 | LIU Zhiwei |
| 795 | `c86f580e74` | target/riscv: Delete the user_mode_only constraints | Feature | target/riscv | 2024-09-06 | Huang Tao |
| 808 | `263e88f0c3` | target/riscv: Add lmul trace for vector | Feature | target/riscv | 2024-09-04 | LIU Zhiwei |
| 811 | `a962813a5d` | target/riscv: Fix wfi in not influenced by mstatus.mie | Fix | target/riscv | 2024-09-03 | LIU Zhiwei |
| 813 | `4fd3c4987f` | target/riscv: Fix using sizek instead of all columns | Fix | target/riscv | 2024-08-31 | LIU Zhiwei |
| 824 | `20fc83eec2` | target/riscv: Update xtvec for a system supports both modes | Feature | target/riscv | 2024-08-28 | LIU Zhiwei |
| 846 | `024307b3c0` | target/riscv: Make mdtcmcr/mitcmcr WARL | Feature | target/riscv | 2024-08-18 | LIU Zhiwei |
| 847 | `851ceb2f08` | target/riscv: Fix tcm size as read only | Fix | target/riscv | 2024-08-18 | LIU Zhiwei |
| 848 | `11ca7340e3` | target/riscv: Use default 32KB TCM for r910 | Feature | target/riscv | 2024-08-18 | LIU Zhiwei |
| 929 | `a402c60c98` | target/riscv: Add f16f32 and f32f64 to tbflags | Feature | target/riscv | 2024-06-21 | Huang Tao |
| 959 | `4d858a4feb` | target/riscv: Enforce WARL behavior for scounteren/hcounteren | Feature | target/riscv | 2024-04-29 | Atish Patra |
| 968 | `663a859c22` | target/riscv: rvzicbo: Fixup CBO extension register calculation | Fix | target/riscv | 2024-05-14 | Alistair Francis |
| 970 | `fde1bfea9d` | target/riscv: do not set mtval2 for non guest-page faults | Feature | target/riscv | 2024-05-03 | Alexei Filippov |
| 974 | `82724c6c4f` | target/riscv: rvv: Check single width operator for vector fp widen instructions | Feature | target/riscv | 2024-03-22 | Max Chou |
| 978 | `f98604ebce` | target/riscv/cpu.c: fix Zvkb extension config | Fix | target/riscv | 2024-05-11 | Yangyu Chen |
| 985 | `4d6a2c765c` | target/riscv/debug: set tval=pc in breakpoint exceptions | Feature | target/riscv | 2024-04-16 | Daniel Henrique Barboza |
| 987 | `7e7e0d8fe2` | target/riscv: fix instructions count handling in icount mode | Fix | target/riscv | 2024-04-11 | Clément Léger |
| 990 | `74b49615a1` | target/riscv: Raise exceptions on wrs.nto | Feature | target/riscv | 2024-04-24 | Andrew Jones |
| 1022 | `431ee92407` | target/riscv: Reserve exception codes for sw-check and hw-err | Feature | target/riscv | 2024-06-06 | Fea.Wang |
| 1041 | `e2f38b16c7` | target/riscv: Add ms mask for read_sstatus | Feature | target/riscv | 2024-06-15 | Huang Tao |
| 1046 | `e6930a8c39` | target/riscv: Fix tbflags MS field setting | Fix | target/riscv | 2024-06-15 | Huang Tao |

---

### XuanTie 向量扩展 - XTheadCvwn

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 12 | `8a6e717ff7` | tests/riscv: Add cvwn test suite to semihost framework | Feature | xtheadcvwn | 2026-03-02 | TANG Tiancheng |
| 17 | `4607fed5bf` | target/riscv: Enable XTcvwn extension for c908x-cp-xt CPU | Feature | xtheadcvwn | 2026-02-27 | TANG Tiancheng |
| 18 | `98045cbccb` | disas/riscv: Add disassembler support for XTcvwn extension | Feature | xtheadcvwn | 2026-02-27 | TANG Tiancheng |
| 19 | `2026f823b7` | target/riscv: Add XTheadcvwn coprocessor vector widen/narrow extension | Feature | xtheadcvwn | 2026-02-27 | TANG Tiancheng |

---

### SPMP / PMP 扩展

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 13 | `878bd63b1e` | tests/riscv: Add semihost test framework and mpmpswitch test suite | Feature | SPMP | 2026-03-02 | TANG Tiancheng |
| 14 | `7ecd0128c9` | target/riscv: Enable Xtheadmpmpswitch extension for r908a CPU | Feature | SPMP | 2026-03-02 | TANG Tiancheng |
| 15 | `a787c78d34` | target/riscv: add mpmpswitch support for per-entry PMP activation control | Feature | SPMP | 2026-03-02 | TANG Tiancheng |
| 16 | `ccefdfbb72` | target/riscv: Fixup typo error for spmpswitch | Fix | SPMP | 2026-02-28 | TANG Tiancheng |
| 35 | `ce36dc8f7e` | target/riscv: Fix mmu_idx parameter passing for SUM bit check in SPMP | Fix | SPMP | 2026-02-05 | TANG Tiancheng |
| 36 | `49c54e2c1f` | target/riscv: Initialize PMP address ranges on CPU reset | Feature | PMP | 2026-02-04 | TANG Tiancheng |
| 40 | `82a179ace2` | target/riscv: Fix SPMP CSR write operations with zero mask | Fix | SPMP | 2026-02-02 | TANG Tiancheng |
| 76 | `e4af70af97` | target/riscv: Spmp rule is addressed from 0 | Feature | SPMP | 2026-01-21 | LIU Zhiwei |
| 148 | `2bc6f69d15` | target/riscv: Fix spmpswitch access by mireg/2 | Fix | SPMP | 2025-12-09 | TANG Tiancheng |
| 151 | `27a7aacf77` | target/riscv: Replace pmpaddr_csr_read with spmpaddr_csr_read in rmw_spmpaddr to fix the spmpaddr[0] read | Fix | SPMP | 2025-12-08 | TANG Tiancheng |
| 152 | `b8ba223532` | target/riscv: Fix return errno for spmp csr | Fix | SPMP | 2025-12-09 | HTT |
| 189 | `f61d494478` | target/riscv: Fix mpmpdeleg and spmpswitch csr number | Fix | SPMP | 2025-11-17 | LIU Zhiwei |
| 193 | `d0af11310f` | target/riscv: Use spmp_start instead of RISCV_MAX_PMPS for pmp only rules | Feature | SPMP | 2025-11-14 | LIU Zhiwei |
| 197 | `4880608fef` | target/riscv: Fix csrw for spmp | Fix | SPMP | 2025-11-13 | LIU Zhiwei |
| 209 | `838190b93f` | target/riscv: Set mpmpdeleg value reset value | Feature | SPMP | 2025-11-10 | LIU Zhiwei |
| 218 | `7ea7efdb1c` | target/riscv: Enable Smpmpdeleg for r908a | Feature | SPMP | 2025-11-07 | LIU Zhiwei |
| 219 | `03220bc337` | target/riscv: Add spmp CSR config | Feature | SPMP | 2025-11-07 | LIU Zhiwei |
| 221 | `1f24b4ccb8` | target/riscv: Add Smpmpdeleg support | Feature | SPMP | 2025-11-06 | LIU Zhiwei |
| 222 | `a9a0d8b555` | target/riscv: Update pmp entry to 64 | Feature | PMP | 2025-11-05 | LIU Zhiwei |
| 223 | `be20d3b2d6` | target/riscv: Support spmp check | Feature | SPMP | 2025-11-05 | LIU Zhiwei |
| 562 | `81d7970f7f` | target/riscv: pmp: remove redundant check in pmp_is_locked | Feature | PMP | 2025-03-13 | Loïc Lefort |
| 563 | `34d5aa3a12` | target/riscv: pmp: exit csr writes early if value was not changed | Feature | PMP | 2025-03-13 | Loïc Lefort |
| 564 | `105304c841` | target/riscv: pmp: fix checks on writes to pmpcfg in Smepmp MML mode | Fix | PMP | 2025-03-13 | Loïc Lefort |
| 565 | `93c2537d63` | target/riscv: pmp: move Smepmp operation conversion into a function | Feature | PMP | 2025-03-13 | Loïc Lefort |
| 566 | `d40a760257` | target/riscv: pmp: don't allow RLB to bypass rule privileges | Feature | PMP | 2025-03-13 | Loïc Lefort |
| 971 | `e3c343db8e` | target/riscv: prioritize pmp errors in raise_mmu_exception() | Feature | PMP | 2024-04-13 | Daniel Henrique Barboza |

---

### XuanTie 自定义扩展

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 20 | `ca9eb3a221` | target/riscv: Add XuanTie mxstatus.MM unaligned access support | Feature | mxstatus | 2026-02-26 | TANG Tiancheng |
| 22 | `a90c05b416` | target/riscv: add missing xthead extensions to isa_edata_arr | Feature | isa_edata_arr | 2026-02-28 | LIU Zhiwei |
| 29 | `2c634814ed` | target/riscv: Remove xtheadvsfb | Feature | isa_edata_arr | 2026-02-10 | LIU Zhiwei |
| 81 | `ba24bc7b66` | target/riscv: Add ext_xtheadcacherpl extenion for r908a/r908 | Feature | CacheLock/xtheadcacherpl | 2026-01-13 | TANG Tiancheng |
| 98 | `c7db9403e0` | target/riscv: Remove MXSTATUS_AIOE control | Feature | xtheadaioe | 2025-12-30 | TANG Tiancheng |
| 105 | `c153b5acdc` | target/riscv: Enable svrsw60t59b for c930 and zhijiang | Feature | svrsw60t59b | 2025-12-29 | LIU Zhiwei |
| 111 | `3fc4d8be44` | target/riscv: Add new ignore csrs of CacheLock for R908A | Feature | CacheLock/xtheadcacherpl | 2025-12-23 | TANG Tiancheng |
| 115 | `2c90182d93` | target: riscv: Add Svrsw60t59b extension support | Feature | svrsw60t59b | 2025-12-22 | TANG Tiancheng |
| 117 | `e5a2bfce3b` | target/riscv: Add zvkgs and zvbc32e to r908a | Feature | zvkgs/zvbc32e/zbc | 2025-12-19 | TANG Tiancheng |
| 208 | `e625b50a74` | target/riscv: Add zbc for r908a | Feature | zvkgs/zvbc32e/zbc | 2025-11-10 | LIU Zhiwei |
| 232 | `3bdb1a5e56` | target/riscv: Add Zilsd and Zclsd extension support | Feature | Zilsd/Zclsd | 2025-08-04 | LIU Zhiwei |
| 305 | `488e377951` | target/riscv: Fix crc32 insn | Fix | xtheadcrc | 2025-09-03 | LIU Zhiwei |
| 329 | `0d192a4e5e` | target/riscv: Fix aioe encode | Fix | xtheadaioe | 2025-08-15 | LIU Zhiwei |
| 374 | `eb00fb4589` | target/riscv: Fix crc compile error | Fix | xtheadcrc | 2025-07-30 | LIU Zhiwei |
| 378 | `2676507245` | target/riscv: Enable xtheadcrc for zhijiang | Feature | xtheadcrc | 2025-07-30 | LIU Zhiwei |
| 379 | `2f67a9c119` | target/riscv: Support xtheadcrc | Feature | xtheadcrc | 2025-07-30 | LIU Zhiwei |
| 495 | `2367d9f56b` | targe/riscv: Add inital support xtheadaioe | Feature | xtheadaioe | 2025-04-28 | LIU Zhiwei |
| 499 | `c446579790` | target/riscv: Add support zvqdotq | Feature | zvqdotq | 2025-04-25 | LIU Zhiwei |
| 528 | `30b488b8a6` | target/riscv: Fix ssdtso for zhijiang | Fix | ssdtso | 2025-04-08 | LIU Zhiwei |
| 551 | `141ba858bb` | target/riscv: Support ssdtso | Feature | ssdtso | 2025-04-03 | LIU Zhiwei |
| 598 | `4aa5d83eb1` | target/riscv: Fix mtnfastmba predicate | Fix | xtheadfastm | 2025-02-22 | LIU Zhiwei |
| 617 | `eb930e54e5` | target/riscv: Fix fastm for smp | Fix | xtheadfastm | 2025-02-11 | LIU Zhiwei |
| 627 | `fd59d5d24a` | target/riscv: Fix fast memory size | Fix | xtheadfastm | 2025-01-26 | LIU Zhiwei |
| 631 | `f23e232b22` | target/riscv: Enable zvkgs for zhijiang | Feature | zvkgs/zvbc32e/zbc | 2025-01-18 | LIU Zhiwei |
| 632 | `1cdd63b9fc` | target/riscv: Fix crc instruction | Fix | xtheadcrc | 2025-01-16 | LIU Zhiwei |
| 671 | `c9a85d2943` | target/riscv: Remove mfastmbah define | Feature | xtheadfastm | 2024-12-25 | LIU Zhiwei |
| 673 | `86169399b3` | target/riscv: Support xtheadpbmt for c907 rv32 | Feature | xtheadpbmt | 2024-12-25 | LIU Zhiwei |
| 682 | `9f9a2dbcb1` | target/riscv: Using new encoding for fast memory | Feature | xtheadfastm | 2024-12-24 | LIU Zhiwei |
| 699 | `14685ffc6d` | target/riscv: Add support for Xuantie fast memory | Feature | xtheadfastm | 2024-12-19 | LIU Zhiwei |
| 748 | `88f700b1b3` | target/riscv: Disable xtheadfmv and xtheadfmemidx for cpu without float | Feature | xtheadfmv/xtheadfmemidx | 2024-11-21 | LIU Zhiwei |
| 749 | `f37e42e92a` | target/riscv: Disable xtheadfmv for 32bit r908 | Feature | xtheadfmv/xtheadfmemidx | 2024-11-21 | LIU Zhiwei |
| 754 | `8fe3bfe6f4` | target/riscv: Support xtheadlpw | Feature | xtheadlpw | 2024-11-15 | LIU Zhiwei |
| 762 | `722323466d` | target/riscv: Enable ssqosid for zhijiang | Feature | ssqosid | 2024-10-30 | LIU Zhiwei |
| 777 | `c45d6ae7a2` | riscv: implement Ssqosid extension and sqoscfg CSR | Feature | ssqosid | 2023-04-25 | Kornel Dulęba |
| 787 | `fc546c46d8` | target/riscv: Update maee when write mxstatus | Feature | xtheadmaee | 2024-09-29 | LIU Zhiwei |
| 812 | `e3924a0c96` | target/riscv: Add xtheadfmv support for r908-rv32 | Feature | xtheadfmv/xtheadfmemidx | 2024-09-01 | LIU Zhiwei |
| 844 | `3470956453` | target/riscv: Add Zvkgs ISA extension support | Feature | zvkgs/zvbc32e/zbc | 2024-08-13 | Huang Tao |
| 845 | `2c96bf1751` | target/riscv: Add Zvbc32e ISA extension support | Feature | zvkgs/zvbc32e/zbc | 2024-08-13 | Huang Tao |
| 927 | `33cca187a2` | target/riscv: Disable xtheadfmemidx for e906-rv32 | Feature | xtheadfmv/xtheadfmemidx | 2024-06-21 | Huang Tao |
| 1055 | `379e778a4e` | target/riscv: Always use xtheadmaee first as linux do | Feature | xtheadmaee | 2024-06-15 | Huang Tao |
| 1077 | `8500d91696` | target/riscv: Add xtheadmaee extension | Feature | xtheadmaee | 2024-06-12 | Huang Tao |
| 1082 | `363566b8fe` | target/riscv: Add xtheadisr for e906/e907 | Feature | xtheadisr | 2024-06-06 | Huang Tao |

---

### XuanTie 向量扩展 - XTheadVector

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 21 | `48156446c9` | target/riscv: Fix cache lock CSR privilege checks and compilation scope | Fix | xtheadvector | 2026-02-06 | TANG Tiancheng |
| 80 | `f6fcca19d9` | target/riscv: Enable cache lock CSRs with privilege control | Feature | xtheadvector | 2026-01-13 | TANG Tiancheng |
| 102 | `e395f674fb` | target/riscv: Fix ext_xtheadvector with vector extensions in c930v | Fix | xtheadvector | 2025-12-30 | TANG Tiancheng |
| 104 | `6ce1170884` | target/riscv: Fix th_vfsig_w calculation for infinity | Fix | xtheadvector | 2025-12-29 | TANG Tiancheng |
| 122 | `fd08f33fe2` | target/riscv: Fix vlm.v/vsm.v for vlen 4096 | Fix | xtheadvector | 2025-12-24 | LIU Zhiwei |
| 123 | `cb20360f9d` | target/riscv: Fix vmacc54h | Fix | xtheadvector | 2025-12-23 | LIU Zhiwei |
| 268 | `3e9c9b2d93` | target/riscv: Fix th_vs predicate | Fix | xtheadvector | 2025-10-11 | LIU Zhiwei |
| 269 | `22348017a8` | target/riscv: Fix vfmv.s.f tail process | Fix | xtheadvector | 2025-10-10 | HTT |
| 552 | `de96799c15` | target/riscv: Fix vaba/vwaba | Fix | xtheadvector | 2025-04-02 | LIU Zhiwei |
| 553 | `99c2a14f91` | target/riscv: Fix vabd/vwabd | Fix | xtheadvector | 2025-04-02 | LIU Zhiwei |
| 576 | `3ccf9822cb` | target/riscv: Fix vmv.s.x tail process | Fix | xtheadvector | 2025-03-25 | LIU Zhiwei |
| 577 | `57977e7e08` | target/riscv: Fix vmv.s.x | Fix | xtheadvector | 2025-03-22 | LIU Zhiwei |
| 578 | `0b3fcbb547` | target/riscv: Fix vabau.vi and vabdu.vi | Fix | xtheadvector | 2025-03-22 | LIU Zhiwei |
| 579 | `dcaad37756` | target/riscv: Fix vmacc54l/h.vs tail process | Fix | xtheadvector | 2025-03-22 | LIU Zhiwei |
| 580 | `23b9604220` | target/riscv: Fix vabau.vi and vabdu.vi unsigned imm | Fix | xtheadvector | 2025-03-22 | LIU Zhiwei |
| 584 | `099a4af2d5` | target/riscv: Fix vmv.s.x tail process | Fix | xtheadvector | 2025-03-18 | LIU Zhiwei |
| 586 | `9a9dfa4978` | target/riscv: Fix vabau* encoding | Fix | xtheadvector | 2025-03-18 | LIU Zhiwei |
| 588 | `ca69ed7f43` | target/riscv: Fix allow vs1 overide vd for vile | Fix | xtheadvector | 2025-03-07 | LIU Zhiwei |
| 601 | `3713894f2f` | target/riscv: Fix vmacc54* | Fix | xtheadvector | 2025-02-19 | LIU Zhiwei |
| 602 | `c3af699549` | target/riscv: Fix vile/vilo | Fix | xtheadvector | 2025-02-19 | LIU Zhiwei |
| 603 | `4d1a811f04` | target/riscv: Fix vabdu* encode | Fix | xtheadvector | 2025-02-19 | LIU Zhiwei |
| 606 | `60869eac70` | target/riscv: Fix vmv.r* check | Fix | xtheadvector | 2025-02-17 | LIU Zhiwei |
| 629 | `22be861965` | target/riscv: Fix vaba* | Fix | xtheadvector | 2025-01-20 | LIU Zhiwei |
| 639 | `6c375c7df8` | target/riscv: Fix tail process for vlie/vlio | Fix | xtheadvector | 2025-01-13 | LIU Zhiwei |
| 640 | `0633bc24ca` | target/riscv: Fix vile/vlio tail process | Fix | xtheadvector | 2025-01-13 | LIU Zhiwei |
| 709 | `3968a51650` | target/riscv: Support th_vfredmax.dup.32 | Feature | xtheadvector | 2024-12-12 | LIU Zhiwei |
| 715 | `58a4126e95` | target/riscv: Support th_vfredsum.dup.32 for xtheadvfreduction | Feature | xtheadvector | 2024-12-09 | LIU Zhiwei |
| 856 | `5d5187ebfd` | target/riscv: Add privilege mode check in MTT | Feature | xtheadvector | 2024-08-16 | Huang Tao |
| 961 | `5418511c58` | target/riscv: Implement privilege mode filtering for cycle/instret | Feature | xtheadvector | 2023-12-28 | Atish Patra |
| 962 | `2cc9803d9c` | target/riscv: Add cycle & instret privilege mode filtering support | Feature | xtheadvector | 2023-06-13 | Kaiwen Xue |
| 963 | `2da4b21f87` | target/riscv: Add cycle & instret privilege mode filtering definitions | Feature | xtheadvector | 2023-06-12 | Kaiwen Xue |
| 964 | `5ca114cf5d` | target/riscv: Add cycle & instret privilege mode filtering properties | Feature | xtheadvector | 2023-06-16 | Kaiwen Xue |
| 984 | `3911c5d1c8` | trans_privileged.c.inc: set (m | Feature | xtheadvector | s)tval | 2024-04-16 20:04:37 -0300|Daniel Henrique Barboza |
| 1054 | `5381bd1303` | target/riscv: Fix the sstatus writing for xtheadvector | Fix | xtheadvector | 2024-06-15 | Huang Tao |
| 1080 | `0ee14b7b64` | Relax the priviledge check for zvfh and zvfmin | Feature | xtheadvector | 2024-06-12 | Huang Tao |
| 1093 | `6cc6c15bc5` | target/riscv: Relax the priviledge check for xthead ext, zca/f/d and zfh/hmin | Feature | xtheadvector | 2024-06-05 | Huang Tao |
| 1100 | `65c9f0ded0` | target/riscv: Add bfloat16 support for xtheadvector and RVV | Feature | xtheadvector | 2024-06-03 | Huang Tao |
| 1104 | `3819aeb836` | target/riscv: Support XTheadVector extension | Feature | xtheadvector | 2024-05-30 | Huang Tao |

---

### 插件 (plugins)

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 23 | `9b3d090b90` | contrib/plugins/hotblocks: fix deadlock in vcpu_tb_trans | Fix | plugins | 2026-02-27 | LIU Zhiwei |
| 24 | `ae04ec758e` | plugins: fix race condition with scoreboards | Fix | plugins | 2026-02-27 | LIU Zhiwei |
| 69 | `e1103eac31` | Revert "plugins/bbv: Use exception instead of sret as monitor exit" | Revert | plugins | 2026-01-26 | TANG Tiancheng |
| 240 | `5443beb843` | add isatrace plugin, add interface qemu_plugin_read_memory_vaddr | Feature | plugins | 2025-10-30 | jianchang.xjc |
| 625 | `f0d00ca7bb` | plugins/bbv: Make it compilable under 32-bit host | Feature | plugins | 2025-02-10 | LIU Zhiwei |
| 691 | `05c9eda3ba` | plugins/hotblocks: New plugin for analyzing indirect branch jumps. | Feature | plugins | 2024-12-21 | Cooper Qu |
| 725 | `227d73db90` | plugins/hotblocks: Don't filter if filter_by_func is not enabled | Feature | plugins | 2024-11-25 | LIU Zhiwei |
| 726 | `89cbca9131` | plugins/hotblock: Add a new parameter func_by_filter | Feature | plugins | 2024-11-24 | LIU Zhiwei |
| 727 | `8d04fcca2f` | plugins/hotblock: Fix assert for hash conflict | Fix | plugins | 2024-11-24 | LIU Zhiwei |
| 728 | `6fa435c71a` | plugins/hotblocks: Fix a typo | Fix | plugins | 2024-11-24 | LIU Zhiwei |
| 729 | `bdb7db403d` | plugins/hotblocks: Assert hash conflict | Feature | plugins | 2024-11-24 | LIU Zhiwei |
| 730 | `85142a4290` | plugins/hotblock: Avoid hash conflict for tbs | Feature | plugins | 2024-11-24 | LIU Zhiwei |
| 731 | `eaa4f56f4f` | plugins/hotblock: Append function name to hotblock | Feature | plugins | 2024-11-23 | LIU Zhiwei |
| 732 | `da45682c25` | plugins/hotblck: Output hotblocks by (rate,ecount) | Feature | plugins | 2024-11-23 | LIU Zhiwei |
| 733 | `68dcefa685` | plugins/hotblock: Sort hot blocks by (exec,rate) | Feature | plugins | 2024-11-23 | LIU Zhiwei |
| 734 | `0fc82dfc40` | plugins/hotblock: Use scoreboard for total instructions | Feature | plugins | 2024-11-23 | LIU Zhiwei |
| 735 | `7777a60075` | plugins: Use scoreboard for func profile | Feature | plugins | 2024-11-23 | LIU Zhiwei |
| 736 | `582cc75976` | plugins: Use scoreboard for smp block | Feature | plugins | 2024-11-23 | LIU Zhiwei |
| 740 | `eb1c8188d1` | plugins: Enhance hotblock by functions and instructions | Feature | plugins | 2024-11-22 | LIU Zhiwei |
| 874 | `fb4957efb7` | target/riscv: Don't expose plugin API under windows | Feature | plugins | 2024-07-30 | LIU Zhiwei |
| 1036 | `1d494d93ef` | plugins/bbv: Use exception instead of sret as monitor exit | Feature | plugins | 2024-06-15 | Huang Tao |

---

### 启动 / Boot

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 25 | `b56eb905bd` | target/riscv: Fix release address clobbering on multi-core boot | Fix | Boot | 2026-02-25 | LIU Zhiwei |
| 886 | `4f742d1b3d` | target/riscv: Fix boot guest linux error | Fix | Boot | 2024-07-26 | LIU Zhiwei |
| 928 | `f372bad070` | target/riscv: Add RVV misa to boot linux | Feature | Boot | 2024-06-21 | Huang Tao |

---

### 杂项

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 27 | `06b2810989` | hw/core/loader: Use qemu_read_full() for ROM file loading | Feature | 其他 | 2026-02-11 | LIU Zhiwei |
| 28 | `5e1765e8e4` | util/osdep: Add qemu_read_full() helper function | Feature | 其他 | 2026-02-11 | LIU Zhiwei |
| 54 | `58ae2e8110` | scripts: Add Git pre-commit hook installer and documentation | Feature | 其他 | 2026-01-29 | LIU Zhiwei |
| 61 | `de723be7c6` | hw/char: Fix format error in csky_uart.c | Fix | 其他 | 2026-01-28 | TANG Tiancheng |
| 65 | `718178b749` | hw/char: Refactor csky_uart.c following DW_apb_uart specification | Feature | 其他 | 2026-01-26 | LIU Zhiwei |
| 77 | `056c10fa18` | target/riscv: Remove redundent semicolon | Feature | Misc | 2026-01-21 | LIU Zhiwei |
| 92 | `73d28ab11b` | hw/timer: Each csky timer triggers irq to all harts | Feature | 其他 | 2026-01-06 | LIU Zhiwei |
| 93 | `63934cfef0` | physmem: Use (*plen - 1) as mask for orig_addr when translate | Feature | 其他 | 2026-01-06 | LIU Zhiwei |
| 113 | `e2e5520d62` | target/riscv: Fix typo in csr.c | Fix | Misc | 2025-12-23 | TANG Tiancheng |
| 120 | `90bea94b19` | target/riscv: Fix jcount error | Fix | Misc | 2025-12-24 | LIU Zhiwei |
| 136 | `34b5d8de6b` | Revert "hw/dummyh: Fix resource leak by coverity" | Fix | 其他 | 2025-12-16 | LIU Zhiwei |
| 146 | `0e31d5a0a3` | accel/tcg: Store section pointer in CPUTLBEntryFull | Feature | 其他 | 2025-01-21 | Ethan Chen |
| 254 | `4a22f0677b` | include: Fix a miss file | Fix | 其他 | 2024-11-26 | LIU Zhiwei |
| 264 | `0bb2ac8732` | memory: Introduce memory region fetch operation | Feature | 其他 | 2025-03-12 | Ethan Chen |
| 265 | `baf45e28d6` | hw/core: Add config stream | Feature | 其他 | 2025-03-12 | Ethan Chen |
| 303 | `6caf526302` | Merge branch c358a2bc9094fc2af1a4ce63f88dbaadb6eecc36 into xuantie-v9.0-dev Title: Merge request from 甲一 | Merge | Merge | 2025-09-03 | lzw194868 |
| 444 | `36568e597b` | target: riscv: Add Svvptc extension support | Feature | 其他 | 2024-08-28 | Alexandre Ghiti |
| 456 | `9aff9efbfd` | target/riscv: Fix a typo | Fix | Misc | 2025-06-03 | LIU Zhiwei |
| 462 | `8994983c33` | hw/dummyh: Fix resource leak by coverity | Fix | 其他 | 2025-06-02 | LIU Zhiwei |
| 587 | `7c66e09c0f` | target/riscv: Fix uninitialize warning | Fix | Misc | 2025-03-18 | LIU Zhiwei |
| 626 | `16c13773a5` | Merge commit '62859b91016666102a15eba3a436f95c7fdfd7c2' into xuantie-v9.0-dev | Merge | Merge | 2025-02-07 | LIU Zhiwei |
| 646 | `ee8ea2db02` | gpex-acpi: Support PCI link devices outside the host bridge | Feature | 其他 | 2024-04-15 | Sunil V L |
| 648 | `54d584efa0` | hw/csky: Fix dummyh timer creation | Fix | 其他 | 2025-01-03 | LIU Zhiwei |
| 700 | `cf78236907` | target/riscv: Remove unused code | Feature | Misc | 2024-12-19 | LIU Zhiwei |
| 745 | `1922c4cec0` | virtio-pci: PRI support | Feature | 其他 | 2024-11-04 | yunying.yyp |
| 746 | `9554d747a1` | virtio-pci: PASID support | Feature | 其他 | 2022-01-04 | Jason Wang |
| 747 | `0bc95d4796` | pcie: pasid support | Feature | 其他 | 2021-11-18 | Jason Wang |
| 761 | `37d90dc5dd` | Fix QOS | Fix | 其他 | 2024-10-31 | hanyu.cp |
| 784 | `3190e3d755` | hw/net: Use device_class_set_props instead of direct setting | Feature | 其他 | 2024-10-11 | LIU Zhiwei |
| 786 | `69eaaf933b` | targer/riscv: Add a property frac_elen_check | Feature | 其他 | 2024-09-29 | LIU Zhiwei |
| 792 | `fae7a8b7c1` | hw/csky_uart: Fix a typo | Fix | Misc | 2024-09-09 | LIU Zhiwei |
| 803 | `0da0989ec2` | hw/csky: Only trigger interrupt if irq is not NULL | Feature | 其他 | 2024-09-07 | LIU Zhiwei |
| 866 | `d1f6aa26b9` | target/riscv: Fix a typo | Fix | Misc | 2024-07-30 | LIU Zhiwei |
| 887 | `74d6161562` | Merge branch 3390c4884cbfb4e4928eca00b9e5ae0d5a4f3160 into xuantie-v9.0-dev Title: Merge request from 甲一 | Merge | Merge | 2024-07-23 | lzw194868 |
| 932 | `02cd3214ef` | RISCV: Merge new features | Merge | Merge | 2024-06-20 | LIU Zhiwei |
| 956 | `549681099a` | COVER | Feature | 其他 | 2023-12-28 | Atish Patra |
| 967 | `f6279b1a7a` | COVER | Feature | 其他 | 2023-12-28 | Atish Patra |
| 1008 | `b147712f61` | exec/memtxattr: add process identifier to the transaction attributes | Feature | 其他 | 2024-05-23 | Tomasz Jeznach |
| 1032 | `e2c6bbd6ec` | Fix the file copyright | Fix | Misc | 2024-06-17 | Huang Tao |
| 1034 | `202d8d71b4` | replay: Fix the function definition error for other Archs except riscv | Fix | 其他 | 2024-06-17 | Huang Tao |
| 1035 | `c938f42eaf` | replay/simpoint: Exit replay after consuming all simpoints | Feature | 其他 | 2024-06-15 | Huang Tao |

---

### SFU/SFA 特殊函数

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 31 | `284b48829c` | target/riscv: Add the new SFU/SFA instructions and refactor the code | Feature | SFU/SFA | 2025-11-18 | TANG Tiancheng |
| 32 | `1d49d19d64` | include/sfu.h: Update the sfu header | Feature | SFU/SFA | 2025-11-18 | TANG Tiancheng |
| 162 | `2a5f8eb233` | target/riscv: Fix sfu compile error for higher cpp compiler | Fix | SFU/SFA | 2025-12-03 | TANG Tiancheng |
| 233 | `1c7f560797` | target/riscv: Update sfu cmodel | Feature | SFU/SFA | 2025-11-01 | LIU Zhiwei |
| 282 | `2c31e9b3a2` | target/riscv: Fix mingw compile for sfu | Fix | SFU/SFA | 2025-09-25 | LIU Zhiwei |
| 287 | `195603326f` | target/riscv: Remove fp6 and sfu vector insns from c930 and zhijiang | Feature | SFU/SFA | 2025-09-17 | LIU Zhiwei |
| 335 | `a21986cb7d` | target/riscv/sfu: Fix the interface errors | Fix | SFU/SFA | 2025-08-14 | tiancheng.tang |
| 357 | `25e6710871` | target/riscv: update sfu | Feature | SFU/SFA | 2025-08-13 | tiancheng.tang |
| 439 | `dfa6e7e621` | target/riscv: Fix xtheadvsfu build error | Fix | SFU/SFA | 2025-06-16 | LIU Zhiwei |
| 440 | `7ded00cbb9` | target/riscv: Support xtheadsfu in zhijiang | Feature | SFU/SFA | 2025-06-15 | LIU Zhiwei |
| 624 | `ab47f081c3` | target/riscv: Add sfu source code | Feature | SFU/SFA | 2025-02-10 | LIU Zhiwei |
| 657 | `4d7d911ff1` | target/riscv: Update the missing sfu.h | Feature | SFU/SFA | 2024-12-28 | LIU Zhiwei |
| 659 | `6af51e76e5` | target/riscv: Update sfu cmodel for sigmoid and rcp | Feature | SFU/SFA | 2024-12-28 | LIU Zhiwei |
| 684 | `9d3bb57a10` | target/riscv: Use static library for sfu | Feature | SFU/SFA | 2024-12-23 | LIU Zhiwei |
| 685 | `4b4dbe65b4` | meson: Support sfu lib on windows | Feature | SFU/SFA | 2024-12-23 | LIU Zhiwei |
| 756 | `967cb7f188` | target/riscv: Support sfu linux dynamic library | Feature | SFU/SFA | 2024-11-15 | LIU Zhiwei |

---

### C-SKY 架构支持

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 34 | `14eb1d0322` | target/csky: Fix array out-of-bounds access in helper_vdsp2_vmulae | Fix | target/csky | 2026-02-05 | TANG Tiancheng |
| 50 | `1b8ec4fbad` | target/csky: use correct argument for semihosting exit call | Feature | target/csky | 2026-02-03 | Cooper Qu |
| 523 | `9c4dcc8ee5` | target/csky: Move parts csky-trace parse to cpu | Feature | target/csky | 2025-04-10 | LIU Zhiwei |
| 737 | `d37fe97971` | csky-trace: Expose more interfaces for all arches | Feature | target/csky | 2024-11-22 | LIU Zhiwei |
| 738 | `1565875bde` | csky-trace: Fix linux user mode compile | Fix | target/csky | 2024-11-22 | LIU Zhiwei |
| 1030 | `254893dc97` | target/csky: Fix csky system mode and linux-user mode errors | Fix | target/csky | 2024-06-19 | Huang Tao |
| 1075 | `8e74c6e2b7` | target/csky: Support csky arch linux user build | Feature | target/csky | 2024-06-14 | Huang Tao |
| 1076 | `a7a949c1b1` | target/csky: Update cskyv2 softmmu for Linux boot | Feature | target/csky | 2024-06-13 | Huang Tao |

---

### Trace / 调试

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 49 | `1306f1f933` | target/riscv: Fix Sstc CSR registration for GDB debugging | Fix | GDB | 2026-01-29 | TANG Tiancheng |
| 611 | `56ca4fcf6d` | target/riscv: Fix gdb cannot read CSRs | Fix | GDB | 2025-02-12 | LIU Zhiwei |
| 620 | `f4b1d9895a` | riscv/dsa: Add system mode option and fix gpr api | Fix | DSA | 2025-02-11 | HTT |
| 623 | `f29ba00fa5` | build: DSA supports require gmodule | Feature | DSA | 2025-02-10 | LIU Zhiwei |
| 638 | `62859b9101` | target/riscv: Using LAZY way to load dsa library | Feature | DSA | 2025-01-14 | HTT |
| 647 | `c46d941f56` | target/riscv: Let qemu abort when dsa is wrong | Feature | DSA | 2025-01-03 | HTT |
| 687 | `a1d24d6c2a` | tracestub: Using qemu api instead of posix lock | Feature | Trace | 2024-12-23 | LIU Zhiwei |
| 766 | `cd2d7f6c94` | target/riscv: Add softfloat interface for dsa | Feature | DSA | 2024-10-25 | Huang Tao |
| 767 | `a0022210d3` | target/riscv: Fix the dsa init bug in release version | Fix | DSA | 2024-10-26 | Huang Tao |
| 781 | `3b694d9486` | target/riscv: Add interface of gettng reg offset of env to dsa | Feature | DSA | 2024-10-16 | Huang Tao |
| 788 | `13066357be` | targer/riscv: update dsa api beacuse of cpf support | Feature | DSA | 2024-09-23 | Huang Tao |
| 807 | `720a588b73` | target/riscv: Add DSA support | Feature | DSA | 2024-08-22 | Huang Tao |
| 863 | `6c0478a258` | target/riscv: Fix gdb register read | Fix | GDB | 2024-08-01 | LIU Zhiwei |
| 1040 | `e1124d64fc` | util/log: Add tb_trace log | Feature | Trace | 2024-06-15 | Huang Tao |
| 1056 | `6670d437e7` | trace: Add more exit trace for special instructions | Feature | Trace | 2024-06-15 | Huang Tao |
| 1057 | `867e6dc73f` | trace: Send debug exit trace for risc-v | Feature | Trace | 2024-06-15 | Huang Tao |
| 1058 | `73974321e8` | trace: Send exception trace for risc-v | Feature | Trace | 2024-06-15 | Huang Tao |
| 1066 | `fdcc1806e4` | target/riscv: Add memory trace as 6.x | Feature | Trace | 2024-06-14 | Huang Tao |

---

### CskySim 仿真平台

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 51 | `8caa424262` | cskysim: Support CPU names with comma-separated parameters | Feature | cskysim | 2026-01-30 | LIU Zhiwei |
| 52 | `81f259d5d4` | cskysim: Correct loop increment for -cpu-prop option | Feature | cskysim | 2026-01-30 | LIU Zhiwei |
| 53 | `0887fed412` | Fix: cskysim: Remove extra closing brace causing compilation error | Fix | cskysim | 2026-01-30 | LIU Zhiwei |
| 56 | `6eeb842813` | cskysim: Prevent duplicate command-line options | Feature | cskysim | 2026-01-29 | LIU Zhiwei |
| 59 | `6552b0f39c` | cskysim: Make -machine and -cpu options conditional in postfix_args | Feature | cskysim | 2026-01-28 | LIU Zhiwei |
| 448 | `d15c56572d` | cskysim: Add smartm xml | Feature | cskysim | 2025-06-09 | LIU Zhiwei |
| 479 | `3ab07c98dd` | cskysim: Add e901 xml | Feature | cskysim | 2025-05-23 | LIU Zhiwei |
| 486 | `200908d070` | cskysim: Add e901/e901mini cpu | Feature | cskysim | 2025-05-14 | LIU Zhiwei |
| 515 | `2e15689408` | cskysim: Fix c908v xml default cpu | Fix | cskysim | 2025-04-15 | LIU Zhiwei |
| 572 | `9f17e3079d` | cskysim: Support -cp-xt cpus | Feature | cskysim | 2025-03-26 | LIU Zhiwei |
| 581 | `fddefd7039` | cskysim: Add cpu c908x-cp-xt | Feature | cskysim | 2025-03-19 | LIU Zhiwei |
| 608 | `37fb353011` | cskysim: Support -vlen option | Feature | cskysim | 2025-02-14 | LIU Zhiwei |
| 615 | `7d66174277` | cskysim: Support c908v2 | Feature | cskysim | 2025-02-12 | LIU Zhiwei |
| 622 | `fd8106e44c` | cskysim: Remove win32 support | Feature | cskysim | 2025-02-11 | LIU Zhiwei |
| 688 | `c0271c279d` | cskysim: Bump version to v5.2.0 | Feature | cskysim | 2024-12-23 | LIU Zhiwei |
| 697 | `6cbdf7c93a` | cskysim: Support c908x cpu | Feature | cskysim | 2024-12-19 | LIU Zhiwei |
| 723 | `d2674b85ce` | cskysim: Don't use 0(IPC_PRIVATE) as a share memory key | Feature | cskysim | 2024-11-27 | LIU Zhiwei |
| 741 | `022c945543` | cskysim: Don't use -cp cpu for xml | Feature | cskysim | 2024-11-21 | LIU Zhiwei |
| 768 | `9e00fb4454` | cskysim: Update version v5.1.2 | Feature | cskysim | 2024-10-23 | LIU Zhiwei |
| 798 | `df90ea6beb` | cskysim: Add zhijiang cpu | Feature | cskysim | 2024-09-07 | LIU Zhiwei |
| 799 | `36e2c78829` | cskysim: Add r908/c910v3/c920v3 | Feature | cskysim | 2024-09-07 | LIU Zhiwei |
| 800 | `02f79863dc` | cskysim/soccfg: Add r908/c910v3/c920v3 xml | Feature | cskysim | 2024-09-07 | LIU Zhiwei |
| 801 | `ac42b11e79` | cskysim: Update xiaohui xml | Feature | cskysim | 2024-09-07 | LIU Zhiwei |
| 1053 | `5e672ede01` | cskysim: Expose more device for cskysim | Feature | cskysim | 2024-06-15 | Huang Tao |
| 1068 | `5a746d8892` | cskysim: Update QEMU version to v9.0.0 | Feature | cskysim | 2024-06-14 | Huang Tao |
| 1072 | `6fc46c3196` | cskysim: Add cskysim support | Feature | cskysim | 2024-06-14 | Huang Tao |

---

### 板级 / 机器模型

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 55 | `f950cafb9a` | hw/riscv: Fix xiaohui_v3 checkpatch fail | Fix | Board | 2026-01-29 | LIU Zhiwei |
| 60 | `a85a8ea585` | hw/riscv: Fix format error in xiaohui_v3 | Fix | Board | 2026-01-28 | TANG Tiancheng |
| 64 | `7c39451923` | hw/xiaohuiv3: Add CPR in xiaohui v3 memory map | Feature | Board | 2026-01-27 | LIU Zhiwei |
| 66 | `f71a206a36` | hw/aplic: Don't check IDC structure size for fixed_msi mode | Feature | Board | 2026-01-26 | LIU Zhiwei |
| 67 | `f8a2815a4c` | hw/aplic: Always use msi for aplic when fixed_msi mode | Feature | Board | 2025-05-21 | LIU Zhiwei |
| 74 | `a98ecc7a23` | hw/riscv: Fix reserved IRQ identification logic | Fix | hw/riscv | 2026-01-22 | LIU Zhiwei |
| 96 | `482b2a19f9` | hw/riscv: Replace '_' to '-' in object_class_property of xiaohui | Feature | Board | 2026-01-01 | TANG Tiancheng |
| 106 | `4c2a9d9ab1` | hw/riscv: Merge xiaohui_v2 to xiaohui | Feature | Board | 2025-12-29 | LIU Zhiwei |
| 107 | `a69456920d` | hw/riscv: Set xt_monchipba from xiaohui platform | Feature | Board | 2025-12-26 | LIU Zhiwei |
| 109 | `2f322e9de8` | hw/riscv: Rename clint to aclint for xiaohui_v2 | Feature | Board | 2025-12-25 | LIU Zhiwei |
| 140 | `d48914399e` | hw/riscv: Fix aplic level interrupt | Fix | Board | 2025-12-15 | LIU Zhiwei |
| 143 | `ea76704dfe` | hw/riscv: Remove bql_unlock from dma transcation | Feature | hw/riscv | 2025-12-12 | LIU Zhiwei |
| 196 | `0df038ad26` | hw/riscv: Add stream link for DMA | Feature | hw/riscv | 2025-11-13 | LIU Zhiwei |
| 250 | `6f1385874e` | hw/riscv: Rename xiaohui-v2 to xiaohui_v2 | Feature | Board | 2025-10-23 | LIU Zhiwei |
| 251 | `a39af3a21e` | hw/riscv: Use aplic for xiaohui v2 | Feature | Board | 2025-10-22 | LIU Zhiwei |
| 253 | `fe05a20ebd` | hw/riscv: Always cpu on for linux boot on xiaohui | Feature | Board | 2025-10-22 | LIU Zhiwei |
| 255 | `69ad746a04` | hw/intc: Add a fixed_msi mode for aplic | Feature | Board | 2024-11-26 | LIU Zhiwei |
| 272 | `b908b4c5ae` | hw/riscv: Add an temp address space for crosspage check | Feature | hw/riscv | 2025-10-09 | LIU Zhiwei |
| 275 | `87c322214b` | hw/riscv: Enable huge page check | Feature | hw/riscv | 2025-09-29 | LIU Zhiwei |
| 277 | `fdb0add473` | hw/riscv: Don't probe TLB in bh function | Feature | hw/riscv | 2025-09-27 | LIU Zhiwei |
| 285 | `56e5edc837` | hw/riscv: Add dma_id > 7 error check | Feature | hw/riscv | 2025-09-17 | LIU Zhiwei |
| 295 | `4ab4965eea` | hw/riscv: Fix check patch error | Fix | hw/riscv | 2025-09-10 | LIU Zhiwei |
| 309 | `564fca82f1` | hw/riscv: Support dma_id and fix some bugs | Fix | hw/riscv | 2025-09-03 | LIU Zhiwei |
| 312 | `5517415c67` | hw/riscv: Uset qemu_bh_new_guarded instead of qemu_bh_new | Feature | hw/riscv | 2025-09-01 | LIU Zhiwei |
| 317 | `42aebf98cd` | hw/riscv: Read or write to tcm address space | Feature | hw/riscv | 2025-08-31 | LIU Zhiwei |
| 324 | `749020e48d` | hw/riscv: Fix tpe_dma_push_coord interface | Fix | hw/riscv | 2025-08-29 | LIU Zhiwei |
| 333 | `70febf6be5` | hw/riscv: Allow write pending bits for plic | Feature | hw/riscv | 2025-08-15 | LIU Zhiwei |
| 393 | `fb6ba987ea` | hw/riscv: Use 20MHZ for smartl and smartm | Feature | Board | 2025-07-23 | LIU Zhiwei |
| 403 | `0c767c18f9` | hw/riscv: Enable non-leaf pte and address range invalidation | Feature | hw/riscv | 2025-07-06 | LIU Zhiwei |
| 446 | `b9e37b0399` | hw/riscv: Alway provide timer for env | Feature | hw/riscv | 2025-06-09 | LIU Zhiwei |
| 450 | `9cfeb099cf` | hw/riscv: Add smartm support | Feature | hw/riscv | 2025-06-09 | LIU Zhiwei |
| 461 | `18edf9567d` | hw/riscv: Fix resource leak by coverity | Fix | hw/riscv | 2025-06-02 | LIU Zhiwei |
| 463 | `c5e3b7c07e` | hw/riscv: Fix resouce leak by coverity | Fix | hw/riscv | 2025-06-02 | LIU Zhiwei |
| 621 | `be3d0af493` | hw/riscv: Remove asp support | Feature | hw/riscv | 2025-02-11 | LIU Zhiwei |
| 644 | `dbd3e83c4d` | hw/riscv/virt-acpi-build.c: Update the HID of RISC-V UART | Feature | hw/riscv | 2024-04-19 | Sunil V L |
| 645 | `f6cbb7c547` | hw/riscv/virt-acpi-build.c: Add namespace devices for PLIC and APLIC | Feature | Board | 2024-03-01 | Sunil V L |
| 686 | `88bab06608` | hw/riscv: Fix size_t print format | Fix | hw/riscv | 2024-12-23 | LIU Zhiwei |
| 693 | `ee4d50701a` | hw/riscv: Fix dummyh compile | Fix | hw/riscv | 2024-12-23 | LIU Zhiwei |
| 694 | `2c4fe26d42` | hw/riscv: Fix missing header file | Fix | hw/riscv | 2024-12-21 | LIU Zhiwei |
| 769 | `c0a92fcc39` | hw/riscv: build example SoC when CBQRI_EXAMPLE_SOC enabled | Feature | hw/riscv | 2023-04-25 | Drew Fustini |
| 770 | `6f3ded5b78` | hw/riscv: instantiate CBQRI controllers for an example SoC | Feature | hw/riscv | 2023-04-25 | Nicolas Pitre |
| 771 | `85d1816142` | hw/riscv: add CBQRI controllers to virt machine | Feature | hw/riscv | 2023-04-25 | Nicolas Pitre |
| 773 | `038ebe35a8` | hw/riscv: Kconfig: add CBQRI options | Feature | hw/riscv | 2023-04-25 | Nicolas Pitre |
| 774 | `4bedc93280` | hw/riscv: implement CBQRI bandwidth controller | Feature | hw/riscv | 2023-04-25 | Nicolas Pitre |
| 775 | `49bfae8e01` | hw/riscv: implement CBQRI capacity controller | Feature | hw/riscv | 2023-04-25 | Nicolas Pitre |
| 776 | `2e242f249f` | hw/riscv: define capabilities of CBQRI controllers | Feature | hw/riscv | 2023-04-25 | Nicolas Pitre |
| 780 | `c50add3bae` | hw/riscv: Add server platform reference machine | Feature | hw/riscv | 2024-03-04 | Fei Wu |
| 783 | `85c743da8e` | hw/riscv/dummyh: Fix creating csky_timer in dummyh | Fix | hw/riscv | 2024-10-15 | LIU Zhiwei |
| 810 | `ecdd5fd972` | hw/riscv: Fix PLIC priority base for Xuantie | Fix | hw/riscv | 2024-09-03 | LIU Zhiwei |
| 826 | `3f40797123` | hw/xiaohui: Fix clint memory map | Fix | Board | 2024-08-27 | LIU Zhiwei |
| 832 | `36918c4bae` | hw/aclint: Fix an typo | Fix | Board | 2024-08-26 | LIU Zhiwei |
| 988 | `de49d25d06` | hw/riscv/boot.c: Support 64-bit address for initrd | Feature | hw/riscv | 2024-04-01 | Cheng Yang |
| 992 | `aeb6b1c62b` | hw/intc/riscv_aplic: APLICs should add child earlier than realize | Feature | Board | 2024-04-09 | yang.zhang |
| 1037 | `4389240bef` | Add simpoint for xiaohui platform | Feature | Board | 2024-06-15 | Huang Tao |
| 1039 | `32ea388388` | hw/riscv: Add Linux support for xiaohui platform | Feature | Board | 2024-06-15 | Huang Tao |
| 1043 | `a84e215750` | hw/riscv/xiaohui: Add CPR support | Feature | Board | 2024-06-15 | Huang Tao |
| 1045 | `32e3a624d7` | hw/riscv: Add xiaohui machine | Feature | Board | 2024-06-15 | Huang Tao |
| 1062 | `25631ad2a0` | hw/riscv: Fix dummyh compile | Fix | hw/riscv | 2024-06-14 | Huang Tao |
| 1078 | `4ff9cf491c` | hw/riscv: Add another serial for virt | Feature | hw/riscv | 2024-06-12 | Huang Tao |
| 1079 | `dac12b21b5` | hw/riscv: Add smarth and smartl for RISC-V | Feature | Board | 2024-06-12 | Huang Tao |

---

### 测试 (tests)

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 70 | `e98d2069ff` | Revert "tests/plugin: Add bbv plugin to support simpoint" | Revert | tests | 2026-01-26 | TANG Tiancheng |
| 805 | `e80a1158fb` | tests/riscv: Add dsa test case | Feature | tests | 2024-09-06 | Huang Tao |
| 996 | `749437f831` | qtest/riscv-iommu-test: add init queues test | Feature | tests | 2024-05-23 | Daniel Henrique Barboza |
| 1002 | `97b319ab63` | test/qtest: add riscv-iommu-pci tests | Feature | tests | 2024-05-23 | Daniel Henrique Barboza |
| 1027 | `682f4cb26e` | tests/csky: Add csky test cases | Feature | tests | 2024-06-19 | Huang Tao |
| 1038 | `914da6c729` | tests/plugin: Add bbv plugin to support simpoint | Feature | tests | 2024-06-15 | Huang Tao |
| 1063 | `ba10f327c2` | tests/qtest: Fix riscv qtest error | Fix | tests | 2024-06-14 | Huang Tao |
| 1103 | `7bd4fc9902` | tests/riscv: Add unit test cases | Feature | tests | 2024-05-30 | Huang Tao |

---

### 构建系统

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 78 | `38c5af4e39` | hw/intc: Reserve irq configure for irq reserved in clint mode | Feature | build-system | 2026-01-20 | LIU Zhiwei |
| 772 | `0adb7b026e` | hw/riscv: meson: add CBQRI controllers to the build | Feature | build-system | 2023-04-25 | Nicolas Pitre |
| 804 | `15dd8a9ace` | configure: Enable dynsoc in meson | Feature | build-system | 2024-09-07 | LIU Zhiwei |
| 872 | `c526408a4c` | meson: Initialize asp_win to remove dependency on Linux | Feature | build-system | 2024-07-30 | LIU Zhiwei |
| 873 | `242509cc6a` | meson: Probe asp for windows in another way | Feature | build-system | 2024-07-30 | LIU Zhiwei |
| 1071 | `fc99737057` | configure: Add --enable-dynsoc option | Feature | build-system | 2024-06-14 | Huang Tao |
| 1073 | `cdaa435119` | options: Add xuantie extend options | Feature | build-system | 2024-06-14 | Huang Tao |

---

### IOPMP

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 82 | `2d1753298c` | hw/riscv: Add rrid_num param to iopmp_create | Feature | IOPMP | 2026-01-12 | TANG Tiancheng |
| 137 | `671f81b37e` | target/risv: Enable iopmp for r908a | Feature | IOPMP | 2025-12-16 | LIU Zhiwei |
| 144 | `3285439d45` | hw/riscv: Use 32 as IOPMP IRQ | Feature | IOPMP | 2025-12-11 | LIU Zhiwei |
| 184 | `abb464b0b7` | hw/riscv: Fix gpex pci host bridge delete when add iopmp | Fix | IOPMP | 2025-11-25 | LIU Zhiwei |
| 224 | `da5f03546c` | hw/riscv: Add iopmp to xiaohui_v2 | Feature | IOPMP | 2025-11-01 | LIU Zhiwei |
| 252 | `0b897ac4f6` | hw/misc: Fix properties in IOPMP | Fix | IOPMP | 2025-10-22 | LIU Zhiwei |
| 258 | `db667a3f8f` | hw/riscv/virt: Add IOPMP support | Feature | IOPMP | 2025-10-18 | LIU Zhiwei |
| 259 | `88b27dcd3f` | hw/misc/riscv_iopmp_dispatcher: Device for redirect IOPMP transaction infomation | Feature | IOPMP | 2025-03-12 | Ethan Chen |
| 260 | `b3d4c4066a` | hw/misc/riscv_iopmp: Add RISC-V IOPMP device | Feature | IOPMP | 2025-03-12 | Ethan Chen |
| 261 | `fa8eb09839` | hw/misc/riscv_iopmp_txn_info: Add struct for transaction infomation | Feature | IOPMP | 2025-03-12 | Ethan Chen |
| 262 | `ebddc79b54` | target/riscv: Add support for IOPMP | Feature | IOPMP | 2025-03-12 | Ethan Chen |
| 266 | `891d47221b` | hw/misc: Remove old iopmp implemention | Feature | IOPMP | 2025-10-18 | LIU Zhiwei |
| 993 | `2963d30708` | hw/riscv/virt: Add IOPMP support | Feature | IOPMP | 2024-06-12 | Ethan Chen |
| 994 | `b675e160f7` | hw/misc/riscv_iopmp: Add RISC-V IOPMP device | Feature | IOPMP | 2024-06-12 | Ethan Chen |

---

### XuanTie CSR 支持

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 83 | `534ad3057d` | target/riscv: Enable iopmp_xtvmid_en in r908a | Feature | VMID | 2026-01-12 | TANG Tiancheng |
| 84 | `931340bf62` | target/riscv: Add iopmp_vmid_en property for IOPMP rrid control | Feature | VMID | 2026-01-12 | TANG Tiancheng |
| 94 | `cd6a904b7c` | target/riscv: Update suenq csr check | Feature | XuanTie CSR | 2026-01-05 | TANG Tiancheng |
| 97 | `824951d9fd` | target/riscv: Add SUENQ bit support in [m | Feature | XuanTie CSR | h]envcfg | 2025-12-30 20:52:45 +0800|TANG Tiancheng |
| 103 | `de8760e4a0` | target/riscv: Fix ISELECT_MASK to support xuantie custom csr | Fix | XuanTie CSR | 2025-12-30 | TANG Tiancheng |
| 116 | `663ed08cbb` | target/riscv: Update the ignore csr for XT | Feature | XuanTie CSR | 2025-12-19 | TANG Tiancheng |
| 128 | `581cf3db46` | target/riscv: Don't guard vmid by vmid_en | Feature | VMID | 2025-12-21 | LIU Zhiwei |
| 139 | `b300147a41` | target/riscv: Use vmid for iopmp for xuantie cpu | Feature | VMID | 2025-12-15 | LIU Zhiwei |
| 183 | `7a979e9816` | target/riscv: Fix sxstatus when debug | Fix | XuanTie CSR | 2025-11-26 | LIU Zhiwei |
| 195 | `9fde736b9f` | hw/riscv: Use vmid as dma rrid | Feature | VMID | 2025-11-14 | LIU Zhiwei |
| 204 | `df8d26944f` | target/riscv: virt_enabled and xt_vmid_en must not enabled at same time | Feature | VMID | 2025-11-11 | LIU Zhiwei |
| 210 | `b80fe9079a` | target/riscv: Fix vmid access error | Fix | VMID | 2025-11-10 | LIU Zhiwei |
| 211 | `752f655533` | target/riscv: Use Jama mpmpdelge CSR number | Feature | CSR | 2025-11-10 | LIU Zhiwei |
| 213 | `68fba9ef54` | target/riscv: Add Xuantie VMID CSR support | Feature | XuanTie CSR | 2025-11-10 | LIU Zhiwei |
| 214 | `43a64d8dca` | target/riscv: Adjust stimecmp write behavior with mtimedelta | Feature | XuanTie CSR | 2025-11-10 | LIU Zhiwei |
| 215 | `629f5aeec7` | target/riscv: Add Xuantie CSR mtimedelta | Feature | XuanTie CSR | 2025-11-10 | LIU Zhiwei |
| 216 | `9cfdd75f96` | target/riscv: Support xuantie mtimedelta offset in time csr | Feature | XuanTie CSR | 2025-11-10 | LIU Zhiwei |
| 279 | `2d864a1ae0` | target/riscv: Fix suenq CSR number | Fix | XuanTie CSR | 2025-09-25 | LIU Zhiwei |
| 280 | `15e9d904b0` | target/riscv: Fix smmpt csr number | Fix | CSR | 2025-09-25 | LIU Zhiwei |
| 383 | `b3034c4636` | target/riscv: Fix senq misalign exception | Fix | XuanTie CSR | 2025-07-26 | LIU Zhiwei |
| 476 | `ee359164d3` | target/riscv: Update mmpt CSR to v0.3.4 | Feature | CSR | 2025-05-28 | LIU Zhiwei |
| 492 | `891c95bf8d` | target/riscv: Fix regnum check for u/senq | Fix | XuanTie CSR | 2025-04-28 | LIU Zhiwei |
| 493 | `68b664c345` | target/riscv: Support suenq for uenq | Feature | XuanTie CSR | 2025-04-28 | LIU Zhiwei |
| 494 | `cbf2eda4a8` | target/riscv: Support suenq CSR | Feature | XuanTie CSR | 2025-04-28 | LIU Zhiwei |
| 512 | `66e56dac4d` | target/riscv: Support xmx*desc CSR | Feature | CSR | 2025-04-15 | LIU Zhiwei |
| 554 | `e9c13b120d` | target/riscv: Add independent CSR for fields in mcsr | Feature | CSR | 2025-04-01 | LIU Zhiwei |
| 654 | `3735e62d0a` | target/riscv: Fix titan CSR name | Fix | XuanTie CSR | 2024-12-30 | LIU Zhiwei |
| 722 | `226feec1e1` | target/riscv: Support utnmode CSR | Feature | XuanTie CSR | 2024-12-04 | LIU Zhiwei |
| 724 | `57b381303a` | target/riscv: Support scontext | Feature | XuanTie CSR | 2024-11-26 | LIU Zhiwei |
| 796 | `be05091fea` | target/riscv: Fix the cfi csr bugs | Fix | CSR | 2024-09-06 | Huang Tao |
| 881 | `f7e6e967ff` | target/riscv: Ingore xuantie CSRs for riscv_csrr | Feature | XuanTie CSR | 2024-07-26 | LIU Zhiwei |
| 944 | `7ce39ba7bc` | target/riscv: Add support for Control Transfer Records extension CSRs. | Feature | CSR | 2024-05-24 | Rajnesh Kanwal |
| 945 | `f5a52b54fc` | target/riscv: Add Control Transfer Records CSR definitions. | Feature | CSR | 2024-05-24 | Rajnesh Kanwal |
| 952 | `d4c93a79d1` | target/riscv: Support generic CSR indirect access | Feature | CSR | 2023-06-02 | Kaiwen Xue |
| 976 | `aa1ba0c5fd` | riscv: thead: Add th.sxstatus CSR emulation | Feature | XuanTie CSR | 2024-04-29 | Christoph Müllner |
| 979 | `600c61a258` | target/riscv: raise an exception when CSRRS/CSRRC writes a read-only CSR | Feature | CSR | 2024-04-03 | Yu-Ming Chang via |
| 1047 | `74a1739606` | target/riscv: Don't slack the new added CSRs | Feature | CSR | 2024-06-15 | Huang Tao |
| 1070 | `cbf7c4b350` | target/riscv: Add PM support for T-HEAD sxstatus | Feature | XuanTie CSR | 2024-06-14 | Huang Tao |
| 1094 | `98f0cf5072` | target/riscv: Support Xuantie CSRs | Feature | XuanTie CSR | 2024-06-05 | Huang Tao |
| 1098 | `a8c45386a2` | target/riscv: Add fxcr CSR | Feature | XuanTie CSR | 2024-06-03 | Huang Tao |
| 1105 | `cf590ae554` | target/riscv: Reuse th_csr.c to add user-mode csrs | Feature | XuanTie CSR | 2024-04-12 | Huang Tao |
| 1106 | `561b12709d` | riscv: thead: Add th.sxstatus CSR emulation | Feature | XuanTie CSR | 2024-03-29 | Christoph Müllner |

---

### 计数器委托 (Smcdeleg/Ssccfg/Smcntrpmf/Smcsrind)

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 91 | `38e037948d` | target/riscv: Remove mask in rmw_xiselect() | Feature | Smcsrind | 2026-01-06 | TANG Tiancheng |
| 207 | `d380a35462` | target/riscv: Fix custom xiselect mask | Fix | Smcsrind | 2025-11-11 | LIU Zhiwei |
| 278 | `68c9976671` | target/riscv: Fix ISELECT_MASK_SXCSRIND for C930 CSR | Fix | Smcsrind | 2025-09-25 | LIU Zhiwei |
| 358 | `7990b8eeee` | target/riscv: Fix ssccfg access for vsireg with mcounteren | Fix | Smcdeleg/Ssccfg | 2025-08-12 | LIU Zhiwei |
| 370 | `0d4cd2ab25` | target/riscv: Fix ssccfg check | Fix | Smcdeleg/Ssccfg | 2025-08-07 | LIU Zhiwei |
| 466 | `729a858e46` | target/riscv: Fix smcdeleg bugs | Fix | Smcdeleg/Ssccfg | 2025-05-31 | LIU Zhiwei |
| 948 | `164d9dc328` | target/riscv: Add counter delegation/configuration support | Feature | Smcdeleg/Ssccfg | 2023-06-09 | Kaiwen Xue |
| 949 | `7be0ff538a` | target/riscv: Add select value range check for counter delegation | Feature | Smcdeleg/Ssccfg | 2023-06-07 | Kaiwen Xue |
| 950 | `af5490ce25` | target/riscv: Add counter delegation definitions | Feature | Smcdeleg/Ssccfg | 2023-06-07 | Kaiwen Xue |
| 951 | `7501602e89` | target/riscv: Add smcdeleg/ssccfg properties | Feature | Smcdeleg/Ssccfg | 2023-06-07 | Kaiwen Xue |
| 954 | `27ec00e741` | target/riscv: Decouple AIA processing from xiselect and xireg | Feature | Smcsrind | 2023-06-02 | Kaiwen Xue |
| 955 | `2a4eee2a02` | target/riscv: Add properties for Indirect CSR Access extension | Feature | Smcsrind | 2023-06-02 | Kaiwen Xue |
| 957 | `921710569c` | target/riscv: More accurately model priv mode filtering. | Feature | Smcntrpmf | 2024-05-10 | Rajnesh Kanwal |
| 958 | `83a9eeb00d` | target/riscv: Start counters from both mhpmcounter and mcountinhibit | Feature | Smcntrpmf | 2024-05-14 | Rajnesh Kanwal |
| 960 | `3ee863f50e` | target/riscv: Save counter values during countinhibit update | Feature | Smcntrpmf | 2024-04-29 | Atish Patra |
| 965 | `7b4695fe16` | target/riscv: Fix the predicate functions for mhpmeventhX CSRs | Fix | Smcntrpmf | 2023-12-20 | Atish Patra |
| 1009 | `fa9043256d` | target/riscv: Add Smstateen check for CSRIND | Feature | Smcsrind | 2024-05-28 | LIU Zhiwei |

---

### XuanTie 矩阵扩展 - Matrix

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 100 | `65b9fea2d2` | target/riscv: Add matrix extension check for special function instructions | Feature | Matrix | 2025-12-31 | TANG Tiancheng |
| 101 | `7eb89f92d3` | target/riscv: Fix control in require_matrix | Fix | Matrix | 2025-12-30 | TANG Tiancheng |
| 154 | `01c4223122` | target/riscv: Fix dequant for out bound of sizem * sizek | Fix | Matrix | 2025-12-05 | LIU Zhiwei |
| 159 | `8e07cfca56` | target/riscv: Fix mldts6/mldtu6 | Fix | Matrix | 2025-12-04 | LIU Zhiwei |
| 176 | `35c5faccc5` | target/riscv: Fix mldtu6/mldts6 | Fix | Matrix | 2025-11-28 | LIU Zhiwei |
| 180 | `a4b4fa9220` | Revert "target/riscv: Nanbox source for matrix unary fp op" | Revert | Matrix | 2025-11-28 | LIU Zhiwei |
| 182 | `ec24b83978` | target/riscv: Fix mfred*.abs | Fix | Matrix | 2025-11-27 | LIU Zhiwei |
| 190 | `586da25788` | target/riscv: Nanbox source for matrix unary fp op | Feature | Matrix | 2025-11-14 | LIU Zhiwei |
| 199 | `237230af96` | target/riscv: Fix mn4clip* | Fix | Matrix | 2025-11-12 | LIU Zhiwei |
| 201 | `74d6531b03` | target/riscv: Ingore matrix size configuration for mn4clip* | Feature | Matrix | 2025-11-12 | LIU Zhiwei |
| 202 | `980f2d41c4` | target/riscv: Fix mpackhl according to new specification | Fix | Matrix | 2025-11-11 | LIU Zhiwei |
| 245 | `948c347d82` | target/riscv: Fix mfred dup for e32/e64 | Fix | Matrix | 2025-10-29 | LIU Zhiwei |
| 267 | `ee732668d8` | target/riscv: Fix matrix mfmax/mfmin/mfmul/mfsub_bf16_mv/mc_i | Fix | Matrix | 2025-10-13 | LIU Zhiwei |
| 274 | `b1ee324b43` | target/riscv: Fix mcslidedown helper function | Fix | Matrix | 2025-10-09 | LIU Zhiwei |
| 300 | `e1a7d7dfc2` | target/riscv: Remove a16w4/a16w8 | Feature | Matrix | 2025-09-04 | LIU Zhiwei |
| 302 | `b402ca6cbc` | target/riscv: Add bf16 dequant insns | Feature | Matrix | 2025-09-03 | LIU Zhiwei |
| 304 | `c358a2bc90` | target/riscv: Add xtheadmdma extension to isa string | Feature | Matrix | 2025-09-03 | LIU Zhiwei |
| 316 | `c81bc092d3` | target/riscv: Matrix load/store to TCM space address | Feature | Matrix | 2025-08-31 | LIU Zhiwei |
| 319 | `0ad1f38841` | target/riscv: Support mldts6/u6 for DMA | Feature | Matrix | 2025-08-30 | LIU Zhiwei |
| 321 | `a54d04c240` | target/riscv: Support matrix ldst access tcm directly | Feature | Matrix | 2025-08-29 | LIU Zhiwei |
| 336 | `3435db4d37` | target/riscv: Fix mfdup is not affected by xmsize | Fix | Matrix | 2025-08-14 | LIU Zhiwei |
| 337 | `f4b199f853` | target/riscv: Enable sfa for zhijiang | Feature | Matrix | 2025-08-14 | LIU Zhiwei |
| 349 | `90d07bb63b` | target/riscv: Add decode for matrix special function instructions | Feature | Matrix | 2025-08-14 | tiancheng.tang |
| 350 | `ae45d9504d` | target/riscv: Add decode for matrix reduction extension. | Feature | Matrix | 2025-08-13 | tiancheng.tang |
| 364 | `2f1c1886f3` | target/riscv: Support matrix scalar operations | Feature | Matrix | 2025-08-10 | LIU Zhiwei |
| 365 | `e023571faa` | target/riscv: Support matrix float and ceil operations | Feature | Matrix | 2025-08-09 | LIU Zhiwei |
| 368 | `54f2f0c5cd` | target/riscv: Support mfabs.*.mm | Feature | Matrix | 2025-08-09 | LIU Zhiwei |
| 372 | `6e1091361f` | target/riscv: Fix cols offset for dequant again | Fix | Matrix | 2025-07-30 | LIU Zhiwei |
| 373 | `0e89f5924b` | target/riscv: Fix cols offset for dequant | Fix | Matrix | 2025-07-30 | LIU Zhiwei |
| 375 | `738b96f59c` | target/riscv: Fix no double scale dequant | Fix | Matrix | 2025-07-30 | LIU Zhiwei |
| 376 | `322c251184` | target/riscv: Fix check on mxfp8-mxfp4 | Fix | Matrix | 2025-07-30 | LIU Zhiwei |
| 380 | `3ce25219f9` | target/riscv: Fix zp process for mdequant* | Fix | Matrix | 2025-07-29 | LIU Zhiwei |
| 381 | `af57e66fec` | target/riscv: Set default nan mode for matrix | Feature | Matrix | 2025-07-28 | LIU Zhiwei |
| 392 | `c78436383d` | target/riscv: Support mv type matrix integer pointwise instrcutions | Feature | Matrix | 2025-07-24 | LIU Zhiwei |
| 395 | `cbe2fbd0e2` | target/riscv: Support dequant operations for matrix | Feature | Matrix | 2025-07-21 | LIU Zhiwei |
| 412 | `1da49adea8` | tests/tcg: Fix matrix test case | Fix | Matrix | 2025-06-26 | LIU Zhiwei |
| 413 | `22af3a68d8` | target/riscv: Suppport th.mfmacc.s.mxe2m1.ue4m3 | Feature | Matrix | 2025-06-26 | LIU Zhiwei |
| 485 | `399570a8b8` | target/riscv: Fix mfmacc.h/bf16.e4/e5 check | Fix | Matrix | 2025-05-21 | LIU Zhiwei |
| 488 | `65127ae96b` | target/riscv: Fix mzero when mrowlen is big | Fix | Matrix | 2025-05-13 | LIU Zhiwei |
| 490 | `5315750afe` | target/riscv: Fix mscvtl check | Fix | Matrix | 2025-05-13 | LIU Zhiwei |
| 491 | `780848d057` | target/riscv: Add support for A8W4 | Feature | Matrix | 2025-05-12 | LIU Zhiwei |
| 496 | `3b4189c2e8` | target/riscv: Fix zhijiang xmisa | Fix | Matrix | 2025-04-27 | LIU Zhiwei |
| 497 | `51c0b49191` | target/riscv: Fix xmisa encode | Fix | Matrix | 2025-04-27 | LIU Zhiwei |
| 500 | `a2996103f7` | tests/tcg: Add a mx format test for xt matrix | Feature | Matrix | 2025-04-21 | LIU Zhiwei |
| 503 | `0751f1eec6` | target/riscv: Add mxf4/mxf8/mxf8f4 for zhijiang | Feature | Matrix | 2025-04-18 | LIU Zhiwei |
| 506 | `b159bde29a` | target/riscv: Support mxf8mxf4 | Feature | Matrix | 2025-04-17 | LIU Zhiwei |
| 507 | `cec32ecb69` | target/riscv: Fix mfmacc.s.e2m1 | Fix | Matrix | 2025-04-17 | LIU Zhiwei |
| 508 | `1d7493be46` | target/riscv: Support mxfp4 and mxfp8 | Feature | Matrix | 2025-04-16 | LIU Zhiwei |
| 509 | `ca9baeee38` | target/riscv: Matrix config for mx | Feature | Matrix | 2025-04-16 | LIU Zhiwei |
| 510 | `d2a4703ec8` | target/riscv: Fix mrowlen to 512 for zhijiang | Fix | Matrix | 2025-04-15 | LIU Zhiwei |
| 511 | `69629684aa` | target/riscv: Fix matrix a16w4 check | Fix | Matrix | 2025-04-15 | LIU Zhiwei |
| 527 | `9bc9852fc4` | target/riscv: Fix matrix store address calculation | Fix | Matrix | 2025-04-09 | LIU Zhiwei |
| 533 | `c760c6fd19` | target/riscv: Support mfmacc.s.e2m1 | Feature | Matrix | 2025-04-08 | LIU Zhiwei |
| 537 | `b5d1852dc8` | target/riscv: Fix xmisa encode | Fix | Matrix | 2025-04-08 | LIU Zhiwei |
| 538 | `c6a937a60d` | target/riscv: Support bf20f32 sub-extension | Feature | Matrix | 2025-04-08 | LIU Zhiwei |
| 539 | `c9a8861d69` | target/riscv: Support matrix prefetch load | Feature | Matrix | 2025-04-07 | LIU Zhiwei |
| 540 | `ef46db0643` | target/riscv: Support streaming matrix transposed store | Feature | Matrix | 2025-04-07 | LIU Zhiwei |
| 541 | `0326489ea8` | target/riscv: Support streaming matrix transposed load | Feature | Matrix | 2025-04-07 | LIU Zhiwei |
| 542 | `7cda10978a` | target/riscv: Fix memory trace for matrix load/store | Fix | Matrix | 2025-04-07 | LIU Zhiwei |
| 543 | `4c0ff39a38` | target/riscv: Fix probe for transposed load/store | Fix | Matrix | 2025-04-07 | LIU Zhiwei |
| 544 | `da4c5ccdff` | target/riscv: Support matrix transposed store | Feature | Matrix | 2025-04-07 | LIU Zhiwei |
| 545 | `a7d06d873d` | target/riscv: Support transposed matrix load | Feature | Matrix | 2025-04-07 | LIU Zhiwei |
| 546 | `aeb6a8ee2d` | target/riscv: Use LSB 32 bits for xmisa firstly | Feature | Matrix | 2025-04-07 | LIU Zhiwei |
| 547 | `c3e91535f3` | target/riscv: Fix mfmacc.h.hb check method | Fix | Matrix | 2025-04-07 | LIU Zhiwei |
| 548 | `28ba1ecfa2` | target/riscv: Remove i16i64 define from xmisa | Feature | Matrix | 2025-04-07 | LIU Zhiwei |
| 549 | `e16fb0a767` | target/riscv: Remove tbflags about matrix subextensions | Feature | Matrix | 2025-04-06 | LIU Zhiwei |
| 550 | `27cee54798` | target/riscv: Update xmisa according to v0.5 spec | Feature | Matrix | 2025-04-03 | LIU Zhiwei |
| 555 | `0758a1bf41` | target/riscv: Saturate result for interger matrix multiplication | Feature | Matrix | 2025-04-01 | LIU Zhiwei |
| 556 | `3af1356cbd` | target/riscv: Rename mmacc_s_bp to mmacc_w_bp | Feature | Matrix | 2025-04-01 | LIU Zhiwei |
| 557 | `fa7d84cd15` | target/riscv: Rename pmmaqa.b to mmacc.w.p | Feature | Matrix | 2025-04-01 | LIU Zhiwei |
| 558 | `076f32ff9d` | target/riscv: Add msaten field for xmcsr | Feature | Matrix | 2025-04-01 | LIU Zhiwei |
| 559 | `ef8758b362` | target/riscv: Rename mmaqa.b(h) to mmacc.w.b(d.h) | Feature | Matrix | 2025-04-01 | LIU Zhiwei |
| 561 | `0d9e6890ca` | target/riscv: Add matrix v0.5 CSRs | Feature | Matrix | 2025-03-31 | LIU Zhiwei |
| 809 | `4c00d31402` | target/riscv: Fix mzero uimm3 bug | Fix | Matrix | 2024-09-04 | Huang Tao |
| 816 | `12874500cb` | target/riscv: Fix fmmacc.h.hb | Fix | Matrix | 2024-08-30 | LIU Zhiwei |
| 817 | `3b65726177` | target/riscv: Fix matrix insn bugs | Fix | Matrix | 2024-08-30 | Huang Tao |
| 880 | `9acce8fc02` | target/riscv: Using uimm3 for mzero | Feature | Matrix | 2024-07-26 | LIU Zhiwei |
| 888 | `3390c4884c` | Merge branch 'xuantie-matrix-v0.4-dev' into xuantie-v9.0-dev | Merge | Matrix | 2024-07-23 | LIU Zhiwei |
| 889 | `eaf96a0c17` | target/riscv: Fix matrix half-byte to byte conversion offset calculation | Fix | Matrix | 2024-07-22 | Zhao.Mingxin |
| 890 | `8715c5ce82` | target/riscv: Fix matrix floating conversion offset calculation | Fix | Matrix | 2024-07-22 | Zhao.Mingxin |
| 891 | `2277fd2be3` | target/riscv: Fix matrix unsigned value mask calculation | Fix | Matrix | 2024-07-22 | Zhao.Mingxin |
| 892 | `39e2b86a38` | target/riscv: Fix matrix m{column,row} slide{up,down} iteration indexes | Fix | Matrix | 2024-07-22 | Zhao.Mingxin |
| 893 | `60d850fa3a` | target/riscv: Rename fwmmacc.s to fmmacc.d.s | Feature | Matrix | 2024-07-22 | Zhao.Mingxin |
| 894 | `a03967a893` | target/riscv: Remove legacy matrix-GPR type instructions | Feature | Matrix | 2024-07-22 | Zhao.Mingxin |
| 895 | `b5a5e3cbc7` | target/riscv: Change matrix config to not return size to GPR | Feature | Matrix | 2024-07-22 | Zhao.Mingxin |
| 896 | `96c8749ccb` | target/riscv: Update matrix decodetree to the latest v0.4 | Feature | Matrix | 2024-07-22 | Zhao.Mingxin |
| 897 | `463f52ba42` | target/riscv: Change matrix floating point instructions to use mfp_status | Feature | Matrix | 2024-07-22 | Zhao.Mingxin |
| 898 | `2b3a847f1b` | target/riscv: Add separate matrix mfrm and mfflags MCSR fields | Feature | Matrix | 2024-07-22 | Zhao.Mingxin |
| 904 | `b017f8558f` | target/riscv: Add matrix float double/single conversion instructions | Feature | Matrix | 2024-07-21 | Zhao.Mingxin |
| 905 | `575ea660f0` | target/riscv: Add matrix double float binary operations | Feature | Matrix | 2024-07-21 | Zhao.Mingxin |
| 906 | `1a373350c1` | target/riscv: Add double word matrix binary op instructions | Feature | Matrix | 2024-07-21 | Zhao.Mingxin |
| 907 | `d05ea7d63f` | target/riscv: Add new matrix instruction legalization checks | Feature | Matrix | 2024-07-21 | Zhao.Mingxin |
| 908 | `27b8fb3560` | target/riscv: Add matrix misc pack instructions | Feature | Matrix | 2024-07-20 | Zhao.Mingxin |
| 909 | `ae09d29293` | target/riscv: Add matrix half-byte integer mixed-precision multiplication | Feature | Matrix | 2024-07-20 | Zhao.Mingxin |
| 910 | `6d8398d3dd` | target/riscv: Add matrix normal multiplication accumulation operations | Feature | Matrix | 2024-07-20 | Zhao.Mingxin |
| 912 | `1f2bbca014` | target/riscv: Add matrix mixed-precision multiplication accumulation | Feature | Matrix | 2024-07-19 | Zhao.Mingxin |
| 915 | `d1d7d9e83c` | target/riscv: Add matrix half-byte integer conversion instructions | Feature | Matrix | 2024-07-18 | Zhao.Mingxin |
| 916 | `2e8be578c4` | target/riscv: Add matrix floating-point conversion instructions | Feature | Matrix | 2024-07-18 | Zhao.Mingxin |
| 918 | `bd6161669b` | target/riscv: Add matrix floating point binary operations | Feature | Matrix | 2024-07-18 | Zhao.Mingxin |
| 919 | `f8f2e1dd30` | target/riscv: Add matrix mn4clip instructions | Feature | Matrix | 2024-07-17 | Zhao.Mingxin |
| 920 | `05e2a17430` | target/riscv: Add matrix slide/cast-move instructions | Feature | Matrix | 2024-07-16 | Zhao.Mingxin |
| 921 | `4f31ceecf9` | target/riscv: Add matrix max/min/umax/umin/sll/srl instructions | Feature | Matrix | 2024-07-16 | Zhao.Mingxin |
| 931 | `ea225e1768` | target/riscv: Reset xmisa to enable all matrix subextension | Feature | Matrix | 2024-06-21 | LIU Zhiwei |
| 1042 | `aa868565d5` | target/riscv: Fix matrix and maee problems | Fix | Matrix | 2024-06-15 | Huang Tao |
| 1044 | `7121e164e4` | target/riscv: Swap I8I32 and I16I64 in xmisa | Feature | Matrix | 2024-06-15 | Huang Tao |
| 1050 | `2ef8b48e72` | target/riscv: Fix mstm which overides ms3 wrongly | Fix | Matrix | 2024-06-15 | Huang Tao |
| 1051 | `4593dc1ee7` | target/riscv: Fix mldm which overides md wrongly | Fix | Matrix | 2024-06-15 | Huang Tao |
| 1052 | `7951bf93ae` | target/riscv: Fix xmcsr read | Fix | Matrix | 2024-06-15 | Huang Tao |
| 1064 | `a10f0f1fc2` | target/riscv: Add gdb support for matrix | Feature | Matrix | 2024-06-14 | Huang Tao |
| 1101 | `561f36f596` | target/riscv: Add support for matrix v0.3 spec | Feature | Matrix | 2024-06-03 | Huang Tao |

---

### 硬件杂项 (hw/misc)

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 110 | `50f26982b9` | hw/misc: Implement num_indexes callback for RISC-V IOMMU | Feature | hw/misc | 2025-12-24 | TANG Tiancheng |

---

### XuanTie 矩阵扩展 - TPE

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 124 | `97a93d9c00` | target/riscv: Fix TPE check | Fix | TPE | 2025-12-20 | LIU Zhiwei |
| 129 | `56bc67d79d` | target/riscv: Add more tb flags for TPE | Feature | TPE | 2025-12-20 | LIU Zhiwei |
| 153 | `c0dc15405a` | hw/riscv/tpe: Fix array indexing and lock management | Fix | TPE | 2025-12-06 | LIU Zhiwei |
| 160 | `42989dcc4b` | hw/riscv: Support one TPE per core | Feature | TPE | 2025-12-04 | LIU Zhiwei |
| 200 | `e121f483d7` | target/riscv: Fix use max/minimum_number for TPE | Fix | TPE | 2025-11-12 | LIU Zhiwei |
| 244 | `4fa804cbba` | target/riscv: Don't round TPE sra/srl | Feature | TPE | 2025-10-29 | LIU Zhiwei |
| 270 | `ccc0516e55` | hw/riscv: Fix tpe checkpatch error | Fix | TPE | 2025-10-09 | LIU Zhiwei |
| 271 | `abafeaef04` | hw/riscv: Only raise irq when xmdmaerrinfo v bit is zero | Feature | TPE | 2025-10-09 | LIU Zhiwei |
| 276 | `ca5346d86e` | hw/riscv: Check crosspage for TPE | Feature | TPE | 2025-09-27 | LIU Zhiwei |
| 284 | `4e9b387565` | target/riscv: Fix TPE DMA interrupt missing | Fix | TPE | 2025-09-24 | LIU Zhiwei |
| 286 | `792273a492` | target/riscv: Fix xmdmaidle clean for DMA copy insns | Fix | TPE | 2025-09-17 | LIU Zhiwei |
| 290 | `19f6d21572` | target/riscv: Make TPE sync ordering stricter | Feature | TPE | 2025-09-15 | LIU Zhiwei |
| 292 | `77b963a732` | target/riscv: Support TPE sync operations | Feature | TPE | 2025-09-13 | LIU Zhiwei |
| 293 | `29719340aa` | target/riscv: Exchange ms1 and ms2 for TPE reduction | Feature | TPE | 2025-09-13 | LIU Zhiwei |
| 296 | `c7b66312d7` | target/riscv: Enable tcm_en access control | Feature | TPE | 2025-09-10 | LIU Zhiwei |
| 297 | `ba2512cae2` | target/riscv: Fix 6bit copy to tcm for TPE | Fix | TPE | 2025-09-09 | LIU Zhiwei |
| 301 | `7977aff158` | target/riscv: Add mfmacc.bf16 for TPE | Feature | TPE | 2025-09-04 | LIU Zhiwei |
| 306 | `e5ab075089` | target/riscv: Support tcmen check for DMA insns | Feature | TPE | 2025-09-03 | LIU Zhiwei |
| 307 | `2bee752c71` | target/riscv: Support XMDMAERRINFO CSR for TPE DMA | Feature | TPE | 2025-09-03 | LIU Zhiwei |
| 308 | `019abdd1db` | target/riscv: Add XMTCMCSR support for TPE DMA | Feature | TPE | 2025-09-03 | LIU Zhiwei |
| 310 | `8cc737464f` | target/riscv: Support TPE DMA interrupt | Feature | TPE | 2025-09-03 | LIU Zhiwei |
| 311 | `4b3eaef7d3` | target/riscv: Fix TPE DMA insns ms check | Fix | TPE | 2025-09-03 | LIU Zhiwei |
| 314 | `0d82046d86` | target/riscv: Fix user mode compile for TPE DMA | Fix | TPE | 2025-09-01 | LIU Zhiwei |
| 315 | `f0f375fae3` | hw/riscv: Add tpe device to virt | Feature | TPE | 2025-08-31 | LIU Zhiwei |
| 318 | `e45b6a2a13` | hw/riscv: Add tcm address space for TPE | Feature | TPE | 2025-08-31 | LIU Zhiwei |
| 320 | `48a1039917` | target/riscv: Support TPE xmdmaidle CSR | Feature | TPE | 2025-08-29 | LIU Zhiwei |
| 322 | `819ddb1af8` | target/riscv: Support TPE fence insns | Feature | TPE | 2025-08-29 | LIU Zhiwei |
| 323 | `c90534ed83` | target/riscv: Support TPE dma instructions | Feature | TPE | 2025-08-29 | LIU Zhiwei |
| 326 | `7452821bca` | hw/riscv: Support Xuantie TPE device | Feature | TPE | 2025-08-28 | LIU Zhiwei |
| 327 | `2e4766a855` | target/riscv: Use 49 interrupt number for TPE DMA | Feature | TPE | 2025-08-19 | LIU Zhiwei |
| 332 | `16189b462d` | target/riscv: Fix TPE reduction | Fix | TPE | 2025-08-15 | LIU Zhiwei |
| 334 | `4ca2c49983` | target/riscv: Fix md/ms overide for TPE reduction | Fix | TPE | 2025-08-15 | LIU Zhiwei |
| 338 | `33836f3ba5` | target/riscv: Support SFA functions for TPE | Feature | TPE | 2025-08-14 | LIU Zhiwei |
| 339 | `670e9d58e8` | target/riscv: Enable TPE reduction for zhijiang | Feature | TPE | 2025-08-14 | LIU Zhiwei |
| 340 | `8ef9e0baee` | target/riscv: Add mfdup operations for TPE | Feature | TPE | 2025-08-14 | LIU Zhiwei |
| 341 | `3ee6fa3ac9` | target/riscv: Add whole register reduction operations for TPE | Feature | TPE | 2025-08-14 | LIU Zhiwei |
| 344 | `ab975df5be` | target/riscv: Add reduction operations for TPE | Feature | TPE | 2025-08-14 | LIU Zhiwei |
| 345 | `9eda477f02` | target/riscv: Remove double reduction for TPE | Feature | TPE | 2025-08-14 | LIU Zhiwei |
| 346 | `05f8edf4d9` | target/riscv: Add reduction API for TPE | Feature | TPE | 2025-08-14 | LIU Zhiwei |
| 359 | `cce5475c23` | target/riscv: Support bf16 vs int8 conversion for TPE | Feature | TPE | 2025-08-11 | LIU Zhiwei |
| 360 | `858175d6dd` | target/riscv: Support bf16 vs fp8 conversion for TPE | Feature | TPE | 2025-08-11 | LIU Zhiwei |
| 361 | `410c96efa2` | target/riscv: Support e8m0 conversion for TPE | Feature | TPE | 2025-08-11 | LIU Zhiwei |
| 362 | `61f5904a34` | target/riscv: Support bf16 to fp4 convert for TPE | Feature | TPE | 2025-08-11 | LIU Zhiwei |

---

### FPU 浮点单元

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 130 | `b15b85c9bd` | fpu: Fix floor_normal and ceil_normal | Fix | fpu | 2025-12-19 | LIU Zhiwei |
| 158 | `98531a8938` | fpu: Fix ceil_normal | Fix | fpu | 2025-12-05 | LIU Zhiwei |
| 177 | `ddd4eee202` | fpu: Set to -0 for value -1 < x < 0 | Feature | fpu | 2025-11-28 | LIU Zhiwei |
| 178 | `440be82e7c` | fpu: Add NaN process for mfceil/mffloor | Feature | fpu | 2025-11-28 | LIU Zhiwei |
| 366 | `6066b8a1e7` | fpu: Support float ceil operations | Feature | fpu | 2025-08-09 | LIU Zhiwei |
| 367 | `a989f02370` | fpu: Support floor functions | Feature | fpu | 2025-08-09 | LIU Zhiwei |
| 398 | `128aa3d70d` | target/riscv: Use ocp fpu interface | Feature | fpu | 2025-07-11 | LIU Zhiwei |
| 399 | `da1e574001` | fpu: Use switch for ocp process | Feature | fpu | 2025-07-09 | LIU Zhiwei |
| 400 | `225236b1ac` | fpu: Use enum for ocp format | Feature | fpu | 2025-07-09 | LIU Zhiwei |
| 401 | `2f2f2cdd9d` | fpu: Don't use overflow_norm for ocp format | Feature | fpu | 2025-07-09 | LIU Zhiwei |
| 422 | `51e4464c17` | fpu: Fix em80/fp6/fp4 normal uncanon | Fix | fpu | 2025-06-24 | LIU Zhiwei |
| 423 | `fca8b34a7a` | fpu: Fix fp8 normal uncanon | Fix | fpu | 2025-06-24 | LIU Zhiwei |
| 436 | `c867d1ee39` | fpu: Support ocp fp6 | Feature | fpu | 2025-06-17 | LIU Zhiwei |
| 513 | `43705c33f4` | fpu: Support float8e0 to float32 | Feature | fpu | 2025-04-15 | LIU Zhiwei |
| 516 | `97a7ad3461` | fpu: Use float4e2_round_pack_canonical for conversion to float4e2 | Feature | fpu | 2025-04-13 | LIU Zhiwei |
| 517 | `3d014f89f4` | fpu: Add ocp_e2m1 field to float4e2 params | Feature | fpu | 2025-04-13 | LIU Zhiwei |
| 518 | `83efd80a06` | fpu: Fix nan_no1s_as_normal in canonicalize | Fix | fpu | 2025-04-11 | LIU Zhiwei |
| 529 | `390a93193f` | fpu: Use positive max for NaN to fp4 | Feature | fpu | 2025-04-08 | LIU Zhiwei |
| 531 | `209092bb5e` | fpu: Support float8/float4 convert | Feature | fpu | 2025-04-08 | LIU Zhiwei |
| 534 | `db88498c78` | fpu: Support float4e2_to_float32 | Feature | fpu | 2025-04-08 | LIU Zhiwei |
| 536 | `418b27cc98` | fpu: Softfpu support basic float4e2 | Feature | fpu | 2025-04-08 | LIU Zhiwei |
| 701 | `7c8d70f501` | fpu/softfloat: Fix fpu build | Fix | fpu | 2024-12-19 | LIU Zhiwei |

---

### 硬件 DMA

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 132 | `28dfc8cfaa` | hw/dma: Fix compile error for dw_dma_sw_axi | Fix | hw/dma | 2025-12-18 | TANG Tiancheng |
| 134 | `d18cf7fa68` | hw/riscv: Replace dw_dma_sw to dw_dma_sw_axi in xiaohui_v2 | Feature | hw/dma | 2025-12-18 | TANG Tiancheng |
| 135 | `35540096b7` | hw/dma: Split dw_dma_sw into AHB/AXI variants with shared common logic | Feature | hw/dma | 2025-12-16 | TANG Tiancheng |
| 194 | `f4ea8d8488` | hw/dma: Fix dw dma build | Fix | hw/dma | 2025-11-14 | LIU Zhiwei |
| 256 | `18361be9e1` | hw/riscv: Add xiaohui-v2 with dw_dma | Feature | hw/dma | 2025-10-17 | TANG Tiancheng |
| 257 | `b53b92b892` | hw/dma: Add DesignWare AHB DMA controller with software handshake only | Feature | hw/dma | 2025-10-17 | TANG Tiancheng |

---

### 反汇编器 (disas)

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 163 | `de35024414` | disas/riscv.c: Fix compile bug of function 'append' | Fix | disas | 2025-07-18 | Lyndra |
| 791 | `beb6fb229e` | target/riscv: Add insn_info to dsa disassemable function | Feature | disas | 2024-09-13 | Huang Tao |
| 793 | `e0fea19257` | target/riscv: Change the dsa disassemable function | Feature | disas | 2024-09-11 | Huang Tao |
| 806 | `a870c32330` | target/riscv: Add disas support for DSA | Feature | disas | 2024-09-03 | Huang Tao |

---

### 编译 / 构建

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 192 | `b4464ec0e1` | target/riscv: Add an empty newline to quiet checkpatch | Feature | Build | 2025-11-14 | LIU Zhiwei |
| 273 | `bfeaf498fd` | target/riscv: Fix 32bit compile | Fix | Build | 2025-10-09 | LIU Zhiwei |
| 283 | `b0222828cf` | target/riscv: Fix 32bit compile | Fix | Build | 2025-09-25 | LIU Zhiwei |
| 291 | `c29ad41cbc` | target/riscv: Fix compile error | Fix | Build | 2025-09-13 | LIU Zhiwei |
| 313 | `cf781c5ef0` | hw/riscv: Fix checkpatch error | Fix | Build | 2025-09-01 | LIU Zhiwei |
| 342 | `4df040c335` | target/riscv: Fix checkpatch warning for xt_reduction.c | Fix | Build | 2025-08-14 | LIU Zhiwei |
| 716 | `b9322b4214` | target/riscv: Fix user mode compile | Fix | Build | 2024-12-11 | LIU Zhiwei |
| 857 | `5a97e5f489` | target/riscv: Fix windows compile error | Fix | Build | 2024-08-12 | LIU Zhiwei |
| 878 | `30798f1095` | hw: Fix windows compile error | Fix | Build | 2024-07-26 | LIU Zhiwei |
| 1031 | `4b7c20b2d9` | tracestub: Fix the compile error for other archs | Fix | Build | 2024-06-18 | Huang Tao |
| 1097 | `397f20fe65` | target/riscv: Add system mode build | Feature | Build | 2024-06-05 | Huang Tao |

---

### RISC-V IOMMU

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 263 | `d28cd1ae5b` | system/physmem: Support IOMMU granularity smaller than TARGET_PAGE size | Feature | riscv-iommu | 2025-03-12 | Ethan Chen |
| 294 | `10ad8d0cf1` | hw/riscv/riscv-iommu: Fixup pdt_memory_read | Fix | riscv-iommu | 2025-09-12 | yfy_lazy |
| 299 | `ae91a44eb5` | hw/riscv/riscv-iommu: Fixup PDT Nested Walk | Fix | riscv-iommu | 2025-08-03 | Guo Ren (Alibaba DAMO Academy) |
| 394 | `b5d9e56735` | hw/riscv/riscv-iommu: Add iommu Svnapot support | Feature | riscv-iommu | 2025-07-23 | yfy_lazy |
| 402 | `eb98840bd5` | target/riscv: iommu: Add G-stage table In Process Context | Feature | riscv-iommu | 2025-07-03 | Guo Ren |
| 407 | `31528b7b9b` | hw/riscv: Support iommu address range invalidation | Feature | riscv-iommu | 2025-07-04 | LIU Zhiwei |
| 743 | `9f86557848` | riscv-iommu: use pasid & devid as key of RISCVIOMMUContext | Feature | riscv-iommu | 2024-09-30 | yunying.yyp |
| 744 | `959c744379` | target/riscv: Fix riscv_iommu_ats deadlock | Fix | riscv-iommu | 2024-09-01 | yunying.yyp |
| 862 | `d628f3e82d` | hw/riscv-iommu: Fix msi redirection address | Fix | riscv-iommu | 2024-08-05 | LIU Zhiwei |
| 997 | `12f5495bf3` | hw/riscv/riscv-iommu: Add another irq for mrif notifications | Feature | riscv-iommu | 2024-05-23 | Andrew Jones |
| 998 | `fcf6257ec2` | hw/riscv/riscv-iommu: add DBG support | Feature | riscv-iommu | 2024-05-23 | Tomasz Jeznach |
| 999 | `d23c16477d` | hw/riscv/riscv-iommu: add ATS support | Feature | riscv-iommu | 2024-05-23 | Tomasz Jeznach |
| 1000 | `caf93a7d48` | hw/riscv/riscv-iommu: add s-stage and g-stage support | Feature | riscv-iommu | 2024-05-23 | Tomasz Jeznach |
| 1001 | `eee3a6b825` | hw/riscv/riscv-iommu: add Address Translation Cache (IOATC) | Feature | riscv-iommu | 2024-05-23 | Tomasz Jeznach |
| 1003 | `9c1c4828a0` | hw/riscv/virt.c: support for RISC-V IOMMU PCIDevice hotplug | Feature | riscv-iommu | 2024-05-23 | Tomasz Jeznach |
| 1004 | `3132946d4a` | hw/riscv: add riscv-iommu-pci reference device | Feature | riscv-iommu | 2024-05-23 | Tomasz Jeznach |
| 1005 | `c2b39de659` | pci-ids.rst: add Red Hat pci-id for RISC-V IOMMU device | Feature | riscv-iommu | 2024-05-23 | Daniel Henrique Barboza |
| 1006 | `955ca345a8` | hw/riscv: add RISC-V IOMMU base emulation | Feature | riscv-iommu | 2024-05-23 | Tomasz Jeznach |
| 1007 | `81277ba6ba` | hw/riscv: add riscv-iommu-bits.h | Feature | riscv-iommu | 2024-05-23 | Tomasz Jeznach |

---

### 内存隔离 (Smmtt/Smsdid)

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 328 | `ee1eb6f3a4` | target/riscv: Fix smsdid encoding | Fix | Smsdid | 2025-08-15 | LIU Zhiwei |
| 674 | `07e3c9bff7` | target/riscv: MTT L1 reserved bits must be zero | Feature | Smmtt | 2024-12-24 | LIU Zhiwei |
| 675 | `988557ef8f` |  target/riscv: MTT L2 reserved bits must be zero | Feature | Smmtt | 2024-12-24 | LIU Zhiwei |
| 676 | `7dc27f5f88` | target/riscv: MTT L3 reserved bits must be zero | Feature | Smmtt | 2024-12-24 | LIU Zhiwei |
| 677 | `eff31a90c7` | target/riscv: MTT L2 entry nozero reserved bits causes exception | Feature | Smmtt | 2024-12-24 | LIU Zhiwei |
| 678 | `5f7e3945d4` | target/riscv: MTT L1 perm use 2 bits for perm | Feature | Smmtt | 2024-12-24 | LIU Zhiwei |
| 680 | `2a1218c1b8` | target/riscv: Add valid bit for smmtt L3 | Feature | Smmtt | 2024-12-24 | LIU Zhiwei |
| 695 | `0c374d09e4` | target/riscv: Fix mtt overflow | Fix | Smmtt | 2024-12-21 | LIU Zhiwei |
| 785 | `4d30d00f8d` | target/riscv: Update smmtt to v1.0.83 | Feature | Smmtt | 2024-09-29 | Huang Tao |
| 834 | `f05a72d3a9` | target/riscv: Fix the MTT lookup bug | Fix | Smmtt | 2024-08-22 | Huang Tao |
| 923 | `11308c5008` | target/riscv: Support Smmtt extension | Feature | Smmtt | 2024-07-05 | Huang Tao |
| 924 | `adc7da555e` | target/riscv: Support Smsdid extension | Feature | Smsdid | 2024-07-03 | Huang Tao |

---

### BFloat16 支持

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 363 | `62af1df110` | fpu: Support bfloat16_to_float4e2 | Feature | BF16 | 2025-08-11 | LIU Zhiwei |
| 429 | `35e8c6c2a1` | fpu: Support float8e0 convert to bfloat16 | Feature | BF16 | 2025-06-23 | LIU Zhiwei |
| 604 | `61c0965dbc` | target/riscv: Add bfloat16 support for c908v2 | Feature | BF16 | 2025-02-18 | LIU Zhiwei |
| 672 | `9fa4fe5efb` | target/riscv: C908x support bfloat16 | Feature | BF16 | 2024-12-25 | LIU Zhiwei |
| 930 | `73fdf75a0c` | target/riscv: Enable zfbfmin for c907fd* cpus | Feature | BF16 | 2024-06-21 | Huang Tao |
| 1048 | `dec2d42ad6` | target/riscv: Nanbox bfloat16 for tcg variable | Feature | BF16 | 2024-06-15 | Huang Tao |
| 1049 | `fc68bcb1ff` | target/riscv: Fix frsqrt_bh for zero and +inf | Fix | BF16 | 2024-06-15 | Huang Tao |
| 1099 | `40da0a9277` | target/riscv: Add bfloat16 support for fpu | Feature | BF16 | 2024-06-03 | Huang Tao |

---

### XuanTie 向量扩展 - XTheadVfofp

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 371 | `bba90b80ad` | fpu: Fix e8m0 NaN canonicalize process | Fix | xtheadvfofp | 2025-08-01 | LIU Zhiwei |
| 396 | `5f8b1c10c3` | fpu: Fix e5m2 for out of range in no sat mode | Fix | xtheadvfofp | 2025-07-11 | LIU Zhiwei |
| 408 | `dc8545f475` | fpu: Fix e4m3 uncanon normal | Fix | xtheadvfofp | 2025-07-03 | LIU Zhiwei |
| 409 | `44e4baf2a5` | target/riscv: Use custom trans for xtheadvfofp8min | Feature | xtheadvfofp | 2025-07-02 | LIU Zhiwei |
| 410 | `9321a53936` | target/riscv: Update xtheadvfofp8min encode | Feature | xtheadvfofp | 2025-07-01 | LIU Zhiwei |
| 416 | `2e57229774` | tests/tcg: Add tcg case for xtheadvfofp8min | Feature | xtheadvfofp | 2025-06-25 | LIU Zhiwei |
| 418 | `6c6f2b3f5e` | fpu: Fix e8m0 uncanon | Fix | xtheadvfofp | 2025-06-25 | LIU Zhiwei |
| 419 | `f1090bb7db` | fpu: Fix e5m2 and e4m3 uncanon | Fix | xtheadvfofp | 2025-06-25 | LIU Zhiwei |
| 421 | `2120f12f19` | fpu: Process e5m2 uncanon for NaN | Feature | xtheadvfofp | 2025-06-25 | LIU Zhiwei |
| 424 | `ecc8c6c6af` | fpu: Fix e4m3 normal uncanon | Fix | xtheadvfofp | 2025-06-24 | LIU Zhiwei |
| 425 | `f2750ac247` | target/riscv: Enable xtheadvfofp8min for zhijiang | Feature | xtheadvfofp | 2025-06-24 | LIU Zhiwei |
| 426 | `6e1cc4c2b8` | target/riscv: Support xtheadvfofp8min | Feature | xtheadvfofp | 2025-06-24 | LIU Zhiwei |
| 427 | `3fd8e3ca37` | target/riscv: Enable xtheadvfoe8m0min for zhijiang | Feature | xtheadvfofp | 2025-06-23 | LIU Zhiwei |
| 428 | `eeaedcbc97` | target/riscv: Support xtheadvfoe8m0min | Feature | xtheadvfofp | 2025-06-23 | LIU Zhiwei |
| 430 | `d8beb4746a` | fpu: Support convert to e8m0 | Feature | xtheadvfofp | 2025-06-20 | LIU Zhiwei |
| 432 | `6752cce2e5` | target/riscv: Enable xtheadvfofp4min for zhijiang | Feature | xtheadvfofp | 2025-06-19 | LIU Zhiwei |
| 433 | `7f47eaa6c2` | target/riscv: Support xtheadvfofp4min | Feature | xtheadvfofp | 2025-06-18 | LIU Zhiwei |
| 434 | `1158d83d48` | target/riscv: Enable xtheadvfofp6min for zhijiang | Feature | xtheadvfofp | 2025-06-18 | LIU Zhiwei |
| 435 | `a06d778e0c` | target/riscv: Support xtheadvfofp6min | Feature | xtheadvfofp | 2025-06-18 | LIU Zhiwei |
| 502 | `f949b46e26` | fpu: Fix an abort for e8m0 | Fix | xtheadvfofp | 2025-04-18 | LIU Zhiwei |
| 519 | `85800044f5` | fpu: Support ocp_e4m3 in FloatFmt | Feature | xtheadvfofp | 2025-04-11 | LIU Zhiwei |
| 520 | `b4f37544ce` | fpu: Remove special process for float8e4 in uncanon_normal | Feature | xtheadvfofp | 2025-04-10 | LIU Zhiwei |
| 521 | `55b7515055` | fpu: Add nan_no1s_as_normal for float8e4 | Feature | xtheadvfofp | 2025-04-10 | LIU Zhiwei |
| 560 | `ff0db4abee` | fpu: Update conversion interfaces to e4m3 | Feature | xtheadvfofp | 2025-03-31 | LIU Zhiwei |
| 592 | `de5120ff02` | target/riscv: Refactor convert from to float16 to float8e4 | Feature | xtheadvfofp | 2025-03-04 | LIU Zhiwei |
| 593 | `cdad4e7825` | target/riscv: Fix fncvt.e4.h when greater than e4m3 max | Fix | xtheadvfofp | 2025-02-28 | LIU Zhiwei |
| 613 | `3b86e58aec` | target/riscv: Fix e5m2 snan check | Fix | xtheadvfofp | 2025-02-12 | LIU Zhiwei |
| 702 | `ecffdf058c` | fpu: Canonicalize the E4M3 as it doesn't have Inf | Feature | xtheadvfofp | 2024-12-19 | LIU Zhiwei |
| 703 | `e087e78df8` | fpu: Fix uncanon_normal for e4m3 | Fix | xtheadvfofp | 2024-12-17 | LIU Zhiwei |
| 876 | `128dd286f5` | target/riscv: Fix fcvt fp8 with fp16 check | Fix | xtheadvfofp | 2024-07-26 | LIU Zhiwei |
| 877 | `b2f672ab54` | target/riscv: Fix fcvt fp8 with fp32 check | Fix | xtheadvfofp | 2024-07-26 | LIU Zhiwei |
| 911 | `33857953dd` | fpu/softfloat: Add float8e4/float8e5 to/from bfloat16 | Feature | xtheadvfofp | 2024-07-20 | Zhao.Mingxin |
| 917 | `9a397d5319` | fpu/softfloat: Add float8e5/float8e4 conversion for float16 | Feature | xtheadvfofp | 2024-07-18 | Zhao.Mingxin |
| 922 | `1ee1600229` | fpu/softfloat: Add float8e4 and float8e5 interfaces | Feature | xtheadvfofp | 2024-07-16 | LIU Zhiwei |

---

### XuanTie 向量扩展 - XTheadVcoder

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 377 | `e4878f23f3` | target/riscv: Add vabsmax.vv for vcoder extension | Feature | xtheadvcoder | 2025-07-30 | LIU Zhiwei |
| 642 | `1fd6827b89` | target/riscv: Support Xuantie coder | Feature | xtheadvcoder | 2025-01-10 | LIU Zhiwei |
| 977 | `33b27ea279` | target/riscv: Implement dynamic establishment of custom decoder | Feature | xtheadvcoder | 2024-05-06 | Huang Tao |
| 1107 | `0b3d3542ab` | target/riscv: Implement dynamic establishment of custom decoder | Feature | xtheadvcoder | 2024-05-06 | Huang Tao |

---

### XuanTie 向量扩展 - XTheadVcrypto

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 411 | `1fe9a795bd` | target/riscv: Support th.vgmulxor.vv | Feature | xtheadvcrypto | 2025-06-26 | LIU Zhiwei |
| 636 | `8114ec060d` | target/riscv: Support Xuantie vcrcfold | Feature | xtheadvcrypto | 2025-01-15 | LIU Zhiwei |
| 637 | `51f572c58e` | target/riscv: Support xuantie vgmul | Feature | xtheadvcrypto | 2025-01-14 | LIU Zhiwei |
| 757 | `7e1fdd7359` | target/riscv: Fix vsha* check | Fix | xtheadvcrypto | 2024-11-11 | LIU Zhiwei |

---

### XuanTie 向量扩展 - XTheadVfcvt

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 417 | `00c6a9011b` | target/riscv: Fix vfncvt.f.f.q | Fix | xtheadvfcvt | 2025-06-25 | LIU Zhiwei |
| 717 | `2f26c789ae` | target/riscv: Add support for vfwcvt | Feature | xtheadvfcvt | 2024-12-07 | LIU Zhiwei |
| 720 | `e488708536` | target/riscv: Add support for vfncvt | Feature | xtheadvfcvt | 2024-12-05 | LIU Zhiwei |
| 721 | `0e7babc9e0` | target/riscv: Support decode of xtheadvfcvt | Feature | xtheadvfcvt | 2024-12-04 | LIU Zhiwei |
| 973 | `da16c2bccd` | target/riscv: rvv: Check single width operator for vfncvt.rod.f.f.w | Feature | xtheadvfcvt | 2024-03-22 | Max Chou |
| 975 | `931eb81cc7` | target/riscv: rvv: Fix Zvfhmin checking for vfwcvt.f.f.v and vfncvt.f.f.w instructions | Fix | xtheadvfcvt | 2024-03-22 | Max Chou |

---

### 文档

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 452 | `1114dd53ec` | docs: Support e901 | Feature | docs | 2025-06-06 | LIU Zhiwei |
| 570 | `97b07cfb0e` | docs: Update cpu names | Feature | docs | 2025-03-27 | LIU Zhiwei |
| 689 | `98cf5daeca` | docs: Support c908x | Feature | docs | 2024-12-23 | LIU Zhiwei |
| 753 | `58d71a1fd4` | docs: Update user manual | Feature | docs | 2024-11-19 | LIU Zhiwei |
| 1033 | `60c65c1c33` | docs/csky: Fix csky docs compile problem | Fix | docs | 2024-06-17 | Huang Tao |
| 1061 | `ed73827e31` | docs: Add copyright subject | Feature | docs | 2024-06-14 | Huang Tao |

---

### CskyTrace 追踪

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 522 | `8539856161` | csky-trace: Remove cpustate arg form csky-trace parse | Feature | csky-trace | 2025-04-10 | LIU Zhiwei |
| 524 | `be1fc76e05` | csky-trace: Move parts of csky_trace parse to cpu | Feature | csky-trace | 2025-04-10 | LIU Zhiwei |
| 525 | `d9bce8d41e` | csky-trace: Move csky-trace parse parts in cpu to csky-trace | Feature | csky-trace | 2025-04-10 | LIU Zhiwei |
| 739 | `b3758d4b3d` | csky-trace: Don't use tb->pc for PCREL | Feature | csky-trace | 2024-11-22 | LIU Zhiwei |
| 742 | `7832330d83` | csky-trace: Guard csky trace exit with gen_tb_trace | Feature | csky-trace | 2024-11-21 | LIU Zhiwei |
| 752 | `badbca043b` | csky-trace: Exit current tb for MMIO | Feature | csky-trace | 2024-11-21 | LIU Zhiwei |
| 758 | `f9a9b25e27` | csky-trace: Add x_mt_trace for multi-thread | Feature | csky-trace | 2024-11-08 | LIU Zhiwei |
| 926 | `9e95ba0da5` | csky-trace: Enable data filter trace for RISC-V | Feature | csky-trace | 2024-07-03 | LIU Zhiwei |
| 1060 | `55e92b72d1` | gdbstub: Set is_gdbserver_start for csky-trace | Feature | csky-trace | 2024-06-14 | Huang Tao |
| 1065 | `fb2419acc6` | csky-trace: Send trace tail when exit | Feature | csky-trace | 2024-06-14 | Huang Tao |
| 1069 | `25d0f866b9` | csky-trace: Fix unit test fails for cpf | Fix | csky-trace | 2024-06-14 | Huang Tao |
| 1074 | `018b9e2131` | csky-trace: Support simple csky-trace for RISC-V | Feature | csky-trace | 2024-06-14 | Huang Tao |

---

### XuanTie 向量扩展 - XTheadVfreduction

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 590 | `0a947c3e01` | target/riscv: Fix zero result process in th.vfredsum* | Fix | xtheadvfreduction | 2025-03-06 | LIU Zhiwei |
| 594 | `fb39d9663c` | target/riscv: Fix th.vbfredsum* using xt_canon_bf16 | Fix | xtheadvfreduction | 2025-02-28 | LIU Zhiwei |
| 614 | `fe0edfa81e` | target/riscv: Add vlmax check for xtheadvfreduction. | Feature | xtheadvfreduction | 2025-02-12 | LIU Zhiwei |
| 652 | `f3f0a1a717` | target/riscv: Fix th.vfredsum.dup.32 check | Fix | xtheadvfreduction | 2024-12-30 | LIU Zhiwei |
| 653 | `0db6e8f872` | target/riscv: Fix check function for th.vfredsum.dup.64 | Fix | xtheadvfreduction | 2024-12-30 | LIU Zhiwei |
| 655 | `5cc8d9ef71` | target/riscv: Fix denormal fp16 and bf16 for fredsum | Fix | xtheadvfreduction | 2024-12-30 | LIU Zhiwei |
| 660 | `a4d885f42a` | target/riscv: Fix vary.dup translation | Fix | xtheadvfreduction | 2024-12-28 | LIU Zhiwei |
| 661 | `911cb83758` | target/riscv: Fix v*fremin* sat result | Fix | xtheadvfreduction | 2024-12-28 | LIU Zhiwei |
| 662 | `e12e46c643` | target/riscv: Fix v*fredmax* sat result | Fix | xtheadvfreduction | 2024-12-28 | LIU Zhiwei |
| 663 | `d0ab4d02be` | target/riscv: fix vfredsum.*.64.* | Fix | xtheadvfreduction | 2024-12-28 | LIU Zhiwei |
| 664 | `21113f3aff` | target/riscv: Fix vbfredsum.64 | Fix | xtheadvfreduction | 2024-12-28 | LIU Zhiwei |
| 665 | `8aa2d4ed27` | target/riscv: Fix vbfredsum.32.*.h | Fix | xtheadvfreduction | 2024-12-28 | LIU Zhiwei |
| 667 | `f68c57e8a5` | target/riscv: Fix vfresum_32_h | Fix | xtheadvfreduction | 2024-12-27 | LIU Zhiwei |
| 669 | `cdaffb9013` | target/riscv: Fix vfredsum_dup_32_w | Fix | xtheadvfreduction | 2024-12-27 | LIU Zhiwei |
| 670 | `fab410a706` | target/riscv: Fix min/max dest position for xtheadvfreduction | Fix | xtheadvfreduction | 2024-12-26 | LIU Zhiwei |
| 704 | `98d1da40f2` | target/riscv: Add vary instructions | Feature | xtheadvfreduction | 2024-12-12 | LIU Zhiwei |
| 705 | `8a657d3db3` | target/riscv: Add vfredmin.* instructions | Feature | xtheadvfreduction | 2024-12-12 | LIU Zhiwei |
| 708 | `4b8440f5b3` | target/riscv: Add vbfredmax.dup.32 | Feature | xtheadvfreduction | 2024-12-12 | LIU Zhiwei |
| 710 | `6d893010be` | target/riscv: Support vfredsum.c.* | Feature | xtheadvfreduction | 2024-12-11 | LIU Zhiwei |
| 711 | `992bd989fd` | target/riscv: Prepare for vfredsum.c* | Feature | xtheadvfreduction | 2024-12-11 | LIU Zhiwei |
| 712 | `de1b2c15fc` | target/riscv: Support th.vbfredsum.dup.64 for xtheadvfreduction | Feature | xtheadvfreduction | 2024-12-11 | LIU Zhiwei |
| 713 | `70b489e1d0` | target/riscv: Support th.vfredsum.dup.64 for xtheadvfreduction | Feature | xtheadvfreduction | 2024-12-11 | LIU Zhiwei |
| 714 | `b3d03116b5` | target/riscv: Support th.vbfredsum.dup.32 for xtheadvfreduction | Feature | xtheadvfreduction | 2024-12-11 | LIU Zhiwei |

---

### 控制流完整性 (Zicfilp/Zicfiss)

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 595 | `a520fa9850` | target/riscv: Remove lpad code in ctr | Feature | Zicfilp | 2025-02-25 | LIU Zhiwei |
| 596 | `9026b9c86f` | target/riscv: Fix lpad enable detection | Fix | Zicfilp | 2025-02-25 | LIU Zhiwei |
| 597 | `ef0b536d26` | target/riscv: Fix cfi mmu index setting | Fix | Zicfiss | 2025-02-25 | LIU Zhiwei |
| 797 | `50d9b5423b` | target/riscv: Fix the lpad bug | Fix | Zicfilp | 2024-09-06 | Huang Tao |
| 865 | `e99e31e40f` | target/riscv: Make locfip delegatable | Feature | Zicfilp | 2024-07-31 | LIU Zhiwei |
| 899 | `23eca4b651` | target/riscv: Add trap and ret support for Zicfilp | Feature | Zicfilp | 2024-07-17 | Huang Tao |
| 900 | `f4f7f36d32` | target/riscv: Add LPAD instruction for Zicfilp | Feature | Zicfilp | 2024-07-17 | Huang Tao |
| 901 | `7c67cbc075` | target/riscv: Add csr support for Zicfilp extension | Feature | Zicfilp | 2024-07-17 | Huang Tao |
| 902 | `3a1329235f` | target/riscv: Add properties for Zicfilp extension | Feature | Zicfilp | 2024-07-17 | Huang Tao |
| 903 | `b5f2cbd636` | target/riscv: Add instructions and mmu implementation for Zicfiss | Feature | Zicfiss | 2024-07-17 | Huang Tao |
| 913 | `80950553f7` | target/riscv: Add csr support for Zicfiss extension | Feature | Zicfiss | 2024-07-15 | Huang Tao |
| 914 | `f00379c20d` | target/riscv: Add properties for Zicfiss extension | Feature | Zicfiss | 2024-07-15 | Huang Tao |

---

### Privilege 1.13 / 基础设施

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 599 | `944babe7ce` | target/riscv: Fix senvcfg read | Fix | Privilege | 2025-02-22 | LIU Zhiwei |
| 794 | `a27fed5ac1` | target/riscv: Expose m/senvcfg and priv to user-mode to enable cfi | Feature | Privilege | 2024-09-09 | Huang Tao |
| 867 | `9ab11dcf9b` | target/riscv: Reserve pbmt value 0b11 for future use | Feature | Svpbmt | 2024-07-30 | LIU Zhiwei |
| 868 | `8f15e7ee99` | target/riscv: Fix stateen0 behavior on MXL32 | Fix | Smstateen | 2024-07-30 | LIU Zhiwei |
| 870 | `5a61d70cad` | target/riscv: Only makes jvt writable when zcmt enabled | Feature | Zcmt | 2024-07-30 | LIU Zhiwei |
| 871 | `0ec38923e7` | target/riscv: Only makes envcfg writable when RVS enabled | Feature | Privilege | 2024-07-30 | LIU Zhiwei |
| 946 | `9d607b1eb5` | target/riscv: Remove obsolete sfence.vm instruction | Feature | ss1p13 | 2024-05-24 | Rajnesh Kanwal |
| 953 | `304101acba` | target/riscv: Enable S*stateen bits for AIA | Feature | Smstateen | 2023-09-25 | Atish Patra |
| 966 | `db496bcda2` | target/riscv: Combine set_mode and set_virt functions. | Feature | ss1p13 | 2024-05-14 | Rajnesh Kanwal |
| 969 | `5855051d8e` | target/riscv: Remove experimental prefix from "B" extension | Feature | B ext | 2024-05-14 | Rob Bradford |
| 1021 | `a52aab43c7` | target/riscv: Support the version for ss1p13 | Feature | ss1p13 | 2024-06-06 | Fea.Wang |
| 1023 | `2b765f1482` | target/riscv: Add MEDELEGH, HEDELEGH csrs for RV32 | Feature | ss1p13 | 2024-06-06 | Fea.Wang |
| 1024 | `d8f78ad12c` | target/riscv: Add 'P1P13' bit in SMSTATEEN0 | Feature | ss1p13 | 2024-06-06 | Fea.Wang |
| 1025 | `5ecaf65645` | target/riscv: Define macros and variables for ss1p13 | Feature | ss1p13 | 2024-06-06 | Fea.Wang |
| 1026 | `33ea46850f` | target/riscv: Reuse the conversion function of priv_spec | Feature | ss1p13 | 2024-06-06 | Jim Shu |

---

### 原子操作扩展

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 600 | `a06d07b00c` | target/riscv: Fix zacas implementation | Fix | Zacas | 2025-02-21 | LIU Zhiwei |
| 814 | `8927f89523` | target/riscv: Support zama16b for zacas and zabha | Feature | Zabha | 2024-08-31 | LIU Zhiwei |
| 995 | `275e0668ae` | target/riscv: Support for the RISCV Zalasr extension | Feature | Zalasr | 2024-06-11 | Brendan Sweeney |
| 1010 | `94e453c593` | disas/riscv: Support zabha disassemble | Feature | Zabha | 2024-05-23 | LIU Zhiwei |
| 1011 | `a02976261c` | target/riscv: Enable zabha for max cpu | Feature | Zabha | 2024-05-23 | LIU Zhiwei |
| 1012 | `488713c0b5` | target/riscv: Add amocas.[b | Feature | Zabha | h] | 2024-05-23 13:51:26 +0800|LIU Zhiwei |
| 1013 | `4f5bd823aa` | target/riscv: Move gen_cmpxchg before adding amocas.[b | Feature | Zabha | h] | 2024-05-23 13:43:14 +0800|LIU Zhiwei |
| 1014 | `0c7617c69f` | target/riscv: Add AMO instructions for Zabha | Feature | Zabha | 2024-05-23 | LIU Zhiwei |
| 1015 | `f58a97240b` | target/riscv: Move gen_amo before implement Zabha | Feature | Zabha | 2024-05-22 | LIU Zhiwei |
| 1016 | `b36d99b41c` | target/riscv: Support Zama16b extension | Feature | Zama16b | 2024-05-22 | LIU Zhiwei |

---

### RVV 标准向量修复

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 609 | `abad7ca9f5` | target/riscv: Don't call gvec if maxsz > 2048 | Feature | RVV | 2025-02-14 | LIU Zhiwei |
| 815 | `f71751e88f` | target/riscv: Fix zvfh depends on zfh for cpu | Fix | RVV | 2024-08-30 | LIU Zhiwei |
| 972 | `aff7775d6f` | target/riscv: rvv: Remove redudant SEW checking for vector fp narrow/widen instructions | Feature | RVV | 2024-03-22 | Max Chou |
| 980 | `62e5b109ba` | target/riscv: Fix the element agnostic function problem | Fix | RVV | 2024-03-25 | Huang Tao |
| 982 | `04905949b3` | target/riscv: Add support for Zve64x extension | Feature | RVV | 2024-03-28 | Jason Chien |
| 983 | `6a72f64a53` | target/riscv: Add support for Zve32x extension | Feature | RVV | 2024-03-28 | Jason Chien |
| 1029 | `9fb4ea3807` | target/riscv: Turn ext_zve32f on when ext_zvfh is true | Feature | RVV | 2024-06-19 | Huang Tao |
| 1108 | `70556ed4aa` | target/riscv: Fix the element agnostic function problem | Fix | RVV | 2024-03-21 | Huang Tao |

---

### XuanTie 向量扩展 - XTheadVarith

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 634 | `4e8f98e2e3` | target/riscv: Support abdu/abau for Xuantie arith turbo | Feature | xtheadvarith | 2025-01-16 | LIU Zhiwei |
| 635 | `e1383f42d8` | target/riscv: Support Xuantie clean.persist | Feature | xtheadvarith | 2025-01-16 | LIU Zhiwei |
| 641 | `83e8e8881c` | target/riscv: Support Xuantie arith | Feature | xtheadvarith | 2025-01-10 | LIU Zhiwei |
| 751 | `817447fc8a` | target/riscv: Disable Xuantie turbo for _cp processors | Feature | xtheadvarith | 2024-11-21 | LIU Zhiwei |

---

### XuanTie 向量扩展 - XTheadVdot

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 719 | `ee4b410590` | target/riscv: Enable xtheadvdot for zhijiang | Feature | xtheadvdot | 2024-12-05 | LIU Zhiwei |
| 750 | `aa135a39d1` | target/riscv: Add xtheadvdot check | Feature | xtheadvdot | 2024-11-21 | LIU Zhiwei |
| 1095 | `bcb3ff4f09` | target/riscv: Add support for Xuantie Vdot extension | Feature | xtheadvdot | 2024-06-05 | Huang Tao |

---

### XuanTie 向量扩展 - XTheadVsfa

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 755 | `8a9c2aee82` | target/riscv: Support xtheadvsfa | Feature | xtheadvsfa | 2024-11-15 | LIU Zhiwei |

---

### 控制转移记录 (Smctr/Ssctr)

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 869 | `53e46e30d5` | target/riscv: Only makes ctr writable when smctr enabled | Feature | Smctr/Ssctr | 2024-07-30 | LIU Zhiwei |
| 875 | `72fffc7d6b` | target/riscv: Using BIT_ULL define mctrctl fields | Feature | Smctr/Ssctr | 2024-07-30 | LIU Zhiwei |
| 941 | `818c782cce` | target/riscv: Add support to access ctrsource, ctrtarget, ctrdata regs. | Feature | Smctr/Ssctr | 2024-05-24 | Rajnesh Kanwal |
| 942 | `85ba69453a` | target/riscv: Add CTR sctrclr instruction. | Feature | Smctr/Ssctr | 2024-05-24 | Rajnesh Kanwal |
| 943 | `f6cd18c434` | target/riscv: Add support to record CTR entries. | Feature | Smctr/Ssctr | 2024-05-24 | Rajnesh Kanwal |
| 947 | `306569a41e` | TEMP: target/riscv: Uprev opensbi to include CTR extension. | Feature | Smctr/Ssctr | 2024-05-15 | Rajnesh Kanwal |
| 1059 | `0b10707626` | pctrace: Make pctrace target agnostic | Feature | Smctr/Ssctr | 2024-06-14 | Huang Tao |

---

### 指针掩码 (Ssnpm/Smnpm/Smmpm)

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 879 | `f480f2b5cc` | target/riscv: Fix write mseccfg | Fix | Ssnpm/Smnpm/Smmpm | 2024-07-26 | LIU Zhiwei |
| 882 | `de39549e22` | target/riscv: Fix mseccfg length | Fix | Ssnpm/Smnpm/Smmpm | 2024-07-26 | LIU Zhiwei |
| 883 | `e03a53fc62` | target/riscv: Using 64-bit mask for PMM in mseccfg | Feature | Ssnpm/Smnpm/Smmpm | 2024-07-26 | LIU Zhiwei |
| 884 | `be490fe2b0` | target/riscv: Add support for mseccfgh | Feature | Ssnpm/Smnpm/Smmpm | 2024-07-26 | LIU Zhiwei |
| 885 | `31bfe3aa10` | target/riscv: Fix mseccfg priv version to 1.12 | Fix | Ssnpm/Smnpm/Smmpm | 2024-07-26 | LIU Zhiwei |
| 935 | `c3d2dc31f9` | target/riscv: Enable updates for pointer masking variables and thus enable pointer masking extension | Feature | Ssnpm/Smnpm/Smmpm | 2024-05-11 | Alexey Baturo |
| 936 | `48f7175a1d` | target/riscv: Update address modify functions to take into account pointer masking | Feature | Ssnpm/Smnpm/Smmpm | 2024-05-11 | Alexey Baturo |
| 937 | `4b94ef9f75` | target/riscv: Add pointer masking tb flags | Feature | Ssnpm/Smnpm/Smmpm | 2024-05-11 | Alexey Baturo |
| 938 | `423d9b59a9` | target/riscv: Add helper functions to calculate current number of masked bits for pointer masking | Feature | Ssnpm/Smnpm/Smmpm | 2024-05-11 | Alexey Baturo |
| 939 | `bbd2d4cb73` | target/riscv: Add new CSR fields for S{sn, mn, m}pm extensions as part of Zjpm v0.8 | Feature | Ssnpm/Smnpm/Smmpm | 2024-05-11 | Alexey Baturo |
| 940 | `aed7cec05d` | target/riscv: Remove obsolete pointer masking extension code. | Feature | Ssnpm/Smnpm/Smmpm | 2024-05-11 | Alexey Baturo |

---

### May-Be-Operations (Zimop/Zcmop)

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 925 | `c48465578c` | target/riscv: Fix zimop major opcode to 0x73 | Fix | Zimop | 2024-07-09 | LIU Zhiwei |
| 1017 | `a1aa695227` | disas/riscv: Support zcmop disassemble | Feature | Zcmop | 2024-05-21 | LIU Zhiwei |
| 1018 | `21a8457901` | target/riscv: Add zcmop extension | Feature | Zcmop | 2024-05-21 | LIU Zhiwei |
| 1019 | `57d4afaf34` | disas/riscv: Support zimop disassemble | Feature | Zimop | 2024-05-21 | LIU Zhiwei |
| 1020 | `c9201f059b` | target/riscv: Add zimop extension | Feature | Zimop | 2024-05-21 | LIU Zhiwei |

---

### 双陷阱 (Smdbltrp/Ssdbltrp)

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 933 | `c0e234100b` | target/riscv: add Smdbltrp extension support | Feature | Smdbltrp | 2024-04-18 | Clément Léger |
| 934 | `8883d4923e` | target/riscv: add Ssdbltrp extension support | Feature | Ssdbltrp | 2024-04-18 | Clément Léger |

---

### GDB 调试

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 981 | `465a891e93` | target/riscv: Relax vector register check in RISCV gdbstub | Feature | gdbstub | 2024-03-28 | Jason Chien |

---

### KVM

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 986 | `6352bf2612` | target/riscv/kvm: tolerate KVM disable ext errors | Feature | KVM | 2024-04-22 | Daniel Henrique Barboza |
| 989 | `0108e9c3e0` | target/riscv/kvm: implement SBI debug console (DBCN) calls | Feature | KVM | 2024-04-25 | Daniel Henrique Barboza |
| 991 | `edb3a28da7` | target/riscv/kvm: Fix exposure of Zkr | Fix | KVM | 2024-04-22 | Andrew Jones |

---

### Packed SIMD (P ext)

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 1028 | `6ef75f6dab` | target/riscv: Relax zpsfoperand PRIV check for rv32 cpus | Feature | P ext | 2024-06-19 | Huang Tao |
| 1102 | `668d0c58da` | target/riscv: Add support for packed extension 0.9.4 | Feature | P ext | 2024-06-03 | Huang Tao |

---

### Linux 用户态

| # | Commit | 描述 | 类型 | 子扩展 | 日期 | 作者 |
|---|--------|------|------|--------|------|------|
| 1109 | `e99b959533` | linux-user: Fix macro | Fix | linux-user | 2024-05-30 | Huang Tao |

---

## 第二部分：全量 Commit 明细表

> 按时间倒序排列（最新在前），每条包含全局编号、Commit Hash、Commit Log、提交日期、作者姓名、所属子扩展、是否为修复。

| # | Commit | Commit Log | 提交日期 | 作者 | 子扩展 | 是否修复 |
|---|--------|------------|----------|------|--------|----------|
| 1 | `ea7a554cbc` | target/riscv: Add riscv_clic_find_suitable_interrupt version routing | 2026-03-12 | LIU Zhiwei | CLIC | ❌ 否 |
| 2 | `6d052dba7b` |  target/riscv: Add r908fdvk-cp and r908fdvk-cp-xt CPU models | 2026-03-11 | LIU Zhiwei | R908A | ❌ 否 |
| 3 | `cb15849d0e` | target/riscv: Add c925 cpu | 2026-03-11 | LIU Zhiwei | C925 | ❌ 否 |
| 4 | `9c79142f3d` | hw/intc/xt_clic_v0p10: Fix m/scliccfg parameter type to uint32_t | 2026-03-11 | TANG Tiancheng | CLIC | ✅ 是 |
| 5 | `c22eb1176c` | target/riscv: Remove mtvec dependency from stvec read | 2026-03-10 | TANG Tiancheng | target/riscv | ❌ 否 |
| 6 | `151804e65e` | target/riscv: Fix sintstatus CSR read to preserve bit position | 2026-03-03 | TANG Tiancheng | CLIC | ✅ 是 |
| 7 | `7e886d8f3e` | target/riscv: Fix CLIC mode global consistency per spec | 2026-03-05 | TANG Tiancheng | CLIC | ✅ 是 |
| 8 | `6be7a6cf3d` | target/riscv: Fix riscv_cpu_has_work() and remove stale CLIC flag clear | 2026-03-07 | TANG Tiancheng | CLIC | ✅ 是 |
| 9 | `882dc803ca` | hw/intc: Reassert CLIC interrupt arbitration on pending bit cleared | 2026-03-07 | TANG Tiancheng | CLIC | ❌ 否 |
| 10 | `fc7462c07f` | Revert "hw/intc: Fix CLIC interrupt signal stickiness issue" | 2026-03-07 | TANG Tiancheng | CLIC | ✅ 是 |
| 11 | `4ad592a995` | Revert "target/riscv: Add exccode clean for clic" | 2026-03-03 | TANG Tiancheng | CLIC | ❌ 否 |
| 12 | `8a6e717ff7` | tests/riscv: Add cvwn test suite to semihost framework | 2026-03-02 | TANG Tiancheng | xtheadcvwn | ❌ 否 |
| 13 | `878bd63b1e` | tests/riscv: Add semihost test framework and mpmpswitch test suite | 2026-03-02 | TANG Tiancheng | SPMP | ❌ 否 |
| 14 | `7ecd0128c9` | target/riscv: Enable Xtheadmpmpswitch extension for r908a CPU | 2026-03-02 | TANG Tiancheng | SPMP | ❌ 否 |
| 15 | `a787c78d34` | target/riscv: add mpmpswitch support for per-entry PMP activation control | 2026-03-02 | TANG Tiancheng | SPMP | ❌ 否 |
| 16 | `ccefdfbb72` | target/riscv: Fixup typo error for spmpswitch | 2026-02-28 | TANG Tiancheng | SPMP | ✅ 是 |
| 17 | `4607fed5bf` | target/riscv: Enable XTcvwn extension for c908x-cp-xt CPU | 2026-02-27 | TANG Tiancheng | xtheadcvwn | ❌ 否 |
| 18 | `98045cbccb` | disas/riscv: Add disassembler support for XTcvwn extension | 2026-02-27 | TANG Tiancheng | xtheadcvwn | ❌ 否 |
| 19 | `2026f823b7` | target/riscv: Add XTheadcvwn coprocessor vector widen/narrow extension | 2026-02-27 | TANG Tiancheng | xtheadcvwn | ❌ 否 |
| 20 | `ca9eb3a221` | target/riscv: Add XuanTie mxstatus.MM unaligned access support | 2026-02-26 | TANG Tiancheng | mxstatus | ❌ 否 |
| 21 | `48156446c9` | target/riscv: Fix cache lock CSR privilege checks and compilation scope | 2026-02-06 | TANG Tiancheng | xtheadvector | ✅ 是 |
| 22 | `a90c05b416` | target/riscv: add missing xthead extensions to isa_edata_arr | 2026-02-28 | LIU Zhiwei | isa_edata_arr | ❌ 否 |
| 23 | `9b3d090b90` | contrib/plugins/hotblocks: fix deadlock in vcpu_tb_trans | 2026-02-27 | LIU Zhiwei | plugins | ✅ 是 |
| 24 | `ae04ec758e` | plugins: fix race condition with scoreboards | 2026-02-27 | LIU Zhiwei | plugins | ✅ 是 |
| 25 | `b56eb905bd` | target/riscv: Fix release address clobbering on multi-core boot | 2026-02-25 | LIU Zhiwei | Boot | ✅ 是 |
| 26 | `d0ed6c406e` | target/riscv: Fix wrong CSR number in read_sintstatus() | 2026-02-12 | LIU Zhiwei | CLIC | ✅ 是 |
| 27 | `06b2810989` | hw/core/loader: Use qemu_read_full() for ROM file loading | 2026-02-11 | LIU Zhiwei | 其他 | ❌ 否 |
| 28 | `5e1765e8e4` | util/osdep: Add qemu_read_full() helper function | 2026-02-11 | LIU Zhiwei | 其他 | ❌ 否 |
| 29 | `2c634814ed` | target/riscv: Remove xtheadvsfb | 2026-02-10 | LIU Zhiwei | isa_edata_arr | ❌ 否 |
| 30 | `e90c0a9e2f` | target/riscv: Fix snxti | 2026-02-10 | LIU Zhiwei | target/riscv | ✅ 是 |
| 31 | `284b48829c` | target/riscv: Add the new SFU/SFA instructions and refactor the code | 2025-11-18 | TANG Tiancheng | SFU/SFA | ❌ 否 |
| 32 | `1d49d19d64` | include/sfu.h: Update the sfu header | 2025-11-18 | TANG Tiancheng | SFU/SFA | ❌ 否 |
| 33 | `4f46051345` | target/riscv: Fix irq loss when xstatus.xie enabled | 2026-02-06 | LIU Zhiwei | target/riscv | ✅ 是 |
| 34 | `14eb1d0322` | target/csky: Fix array out-of-bounds access in helper_vdsp2_vmulae | 2026-02-05 | TANG Tiancheng | target/csky | ✅ 是 |
| 35 | `ce36dc8f7e` | target/riscv: Fix mmu_idx parameter passing for SUM bit check in SPMP | 2026-02-05 | TANG Tiancheng | SPMP | ✅ 是 |
| 36 | `49c54e2c1f` | target/riscv: Initialize PMP address ranges on CPU reset | 2026-02-04 | TANG Tiancheng | PMP | ❌ 否 |
| 37 | `d10995abe6` | target/riscv: Fix xtvec bit preservation for CLIC v0.8/v0.10 | 2026-02-04 | TANG Tiancheng | CLIC | ✅ 是 |
| 38 | `607c1e2f0b` | target/riscv: Fix reserved mode (0b10) error handling in clint mode | 2026-02-04 | TANG Tiancheng | target/riscv | ✅ 是 |
| 39 | `e32fc60700` | target/riscv: Enable ext_smclic_v0p8 for e902/e906/e907 CPUs | 2026-02-03 | TANG Tiancheng | CLIC | ❌ 否 |
| 40 | `82a179ace2` | target/riscv: Fix SPMP CSR write operations with zero mask | 2026-02-02 | TANG Tiancheng | SPMP | ✅ 是 |
| 41 | `455aa954e8` | target/riscv: Enable ext_smclic for r908 and r908afdvk_xt CPUs | 2026-02-03 | TANG Tiancheng | CLIC | ❌ 否 |
| 42 | `3d705edc53` | target/riscv: Distinguish XINTSTATUS CSR addresses for different CLIC versions | 2026-02-02 | TANG Tiancheng | CLIC | ❌ 否 |
| 43 | `b5a500b860` | target/riscv: Add CLIC device existence check for CLIC CSRs | 2026-02-02 | TANG Tiancheng | CLIC | ❌ 否 |
| 44 | `2f2c719e4e` | target/riscv: Check ext_smclic in clic() predicate instead of env->xt_clic_v0p8 | 2026-02-02 | TANG Tiancheng | CLIC | ❌ 否 |
| 45 | `6489d00035` | target/riscv: Rename ext_smclic/ext_ssclic to ext_smclic{_v0p8,_v0p10},ext_ssclic_v0p10 | 2026-02-03 | TANG Tiancheng | CLIC | ❌ 否 |
| 46 | `4e23b68651` | target/riscv: Remove clint_clic for clic v0.8/v.10 | 2026-01-30 | TANG Tiancheng | CLIC | ❌ 否 |
| 47 | `1fe4ac4665` | target/riscv: Rename env.clic to env.xt_clic_v0p8 for xt_clic | 2026-01-28 | TANG Tiancheng | CLIC | ❌ 否 |
| 48 | `7fbd14c8d0` | target/riscv: Fix rmw_mnxti() return value when not in CLIC mode | 2026-02-02 | TANG Tiancheng | CLIC | ✅ 是 |
| 49 | `1306f1f933` | target/riscv: Fix Sstc CSR registration for GDB debugging | 2026-01-29 | TANG Tiancheng | GDB | ✅ 是 |
| 50 | `1b8ec4fbad` | target/csky: use correct argument for semihosting exit call | 2026-02-03 | Cooper Qu | target/csky | ❌ 否 |
| 51 | `8caa424262` | cskysim: Support CPU names with comma-separated parameters | 2026-01-30 | LIU Zhiwei | cskysim | ❌ 否 |
| 52 | `81f259d5d4` | cskysim: Correct loop increment for -cpu-prop option | 2026-01-30 | LIU Zhiwei | cskysim | ❌ 否 |
| 53 | `0887fed412` | Fix: cskysim: Remove extra closing brace causing compilation error | 2026-01-30 | LIU Zhiwei | cskysim | ✅ 是 |
| 54 | `58ae2e8110` | scripts: Add Git pre-commit hook installer and documentation | 2026-01-29 | LIU Zhiwei | 其他 | ❌ 否 |
| 55 | `f950cafb9a` | hw/riscv: Fix xiaohui_v3 checkpatch fail | 2026-01-29 | LIU Zhiwei | Board | ✅ 是 |
| 56 | `6eeb842813` | cskysim: Prevent duplicate command-line options | 2026-01-29 | LIU Zhiwei | cskysim | ❌ 否 |
| 57 | `5a09fbd004` | target/riscv: Add marchid for Xuantie cpu | 2026-01-29 | LIU Zhiwei | CPU | ❌ 否 |
| 58 | `e283e82d25` | target/riscv: Extend cpu name length for dynamic cpu | 2026-01-28 | LIU Zhiwei | target/riscv | ❌ 否 |
| 59 | `6552b0f39c` | cskysim: Make -machine and -cpu options conditional in postfix_args | 2026-01-28 | LIU Zhiwei | cskysim | ❌ 否 |
| 60 | `a85a8ea585` | hw/riscv: Fix format error in xiaohui_v3 | 2026-01-28 | TANG Tiancheng | Board | ✅ 是 |
| 61 | `de723be7c6` | hw/char: Fix format error in csky_uart.c | 2026-01-28 | TANG Tiancheng | 其他 | ✅ 是 |
| 62 | `2c74aedf20` | hw/intc: Fix CLIC interrupt signal stickiness issue | 2026-01-27 | TANG Tiancheng | CLIC | ✅ 是 |
| 63 | `85ed4595fb` | target/riscv: Add exccode clean for clic | 2026-01-27 | TANG Tiancheng | CLIC | ❌ 否 |
| 64 | `7c39451923` | hw/xiaohuiv3: Add CPR in xiaohui v3 memory map | 2026-01-27 | LIU Zhiwei | Board | ❌ 否 |
| 65 | `718178b749` | hw/char: Refactor csky_uart.c following DW_apb_uart specification | 2026-01-26 | LIU Zhiwei | 其他 | ❌ 否 |
| 66 | `f71a206a36` | hw/aplic: Don't check IDC structure size for fixed_msi mode | 2026-01-26 | LIU Zhiwei | Board | ❌ 否 |
| 67 | `f8a2815a4c` | hw/aplic: Always use msi for aplic when fixed_msi mode | 2025-05-21 | LIU Zhiwei | Board | ❌ 否 |
| 68 | `9747ec257a` | hw/riscv: Add xiaohui_v3 support for c930 | 2026-01-26 | LIU Zhiwei | C930 | ❌ 否 |
| 69 | `e1103eac31` | Revert "plugins/bbv: Use exception instead of sret as monitor exit" | 2026-01-26 | TANG Tiancheng | plugins | ❌ 否 |
| 70 | `e98d2069ff` | Revert "tests/plugin: Add bbv plugin to support simpoint" | 2026-01-26 | TANG Tiancheng | tests | ❌ 否 |
| 71 | `27a61f263f` | target/riscv: Add smaia and ssaia for r908a | 2026-01-22 | LIU Zhiwei | R908A | ❌ 否 |
| 72 | `f1c010f381` | hw/timer: Improve code readability and fix clic irq connection | 2026-01-22 | LIU Zhiwei | CLIC | ✅ 是 |
| 73 | `620d892b96` | hw/intc: Always trigger a new irq selection when clic status changes | 2026-01-22 | LIU Zhiwei | CLIC | ❌ 否 |
| 74 | `a98ecc7a23` | hw/riscv: Fix reserved IRQ identification logic | 2026-01-22 | LIU Zhiwei | hw/riscv | ✅ 是 |
| 75 | `09496a11e6` | hw/intc: Update active list when irq mode change | 2026-01-22 | LIU Zhiwei | hw/intc | ❌ 否 |
| 76 | `e4af70af97` | target/riscv: Spmp rule is addressed from 0 | 2026-01-21 | LIU Zhiwei | SPMP | ❌ 否 |
| 77 | `056c10fa18` | target/riscv: Remove redundent semicolon | 2026-01-21 | LIU Zhiwei | Misc | ❌ 否 |
| 78 | `38c5af4e39` | hw/intc: Reserve irq configure for irq reserved in clint mode | 2026-01-20 | LIU Zhiwei | build-system | ❌ 否 |
| 79 | `102ad81a38` | target/riscv: Enable zc and b for e906f | 2026-01-13 | LIU Zhiwei | E90x | ❌ 否 |
| 80 | `f6fcca19d9` | target/riscv: Enable cache lock CSRs with privilege control | 2026-01-13 | TANG Tiancheng | xtheadvector | ❌ 否 |
| 81 | `ba24bc7b66` | target/riscv: Add ext_xtheadcacherpl extenion for r908a/r908 | 2026-01-13 | TANG Tiancheng | CacheLock/xtheadcacherpl | ❌ 否 |
| 82 | `2d1753298c` | hw/riscv: Add rrid_num param to iopmp_create | 2026-01-12 | TANG Tiancheng | IOPMP | ❌ 否 |
| 83 | `534ad3057d` | target/riscv: Enable iopmp_xtvmid_en in r908a | 2026-01-12 | TANG Tiancheng | VMID | ❌ 否 |
| 84 | `931340bf62` | target/riscv: Add iopmp_vmid_en property for IOPMP rrid control | 2026-01-12 | TANG Tiancheng | VMID | ❌ 否 |
| 85 | `2dffe22edf` | hw/intc: Add tail-chaining check for clic v0.9 | 2026-01-06 | TANG Tiancheng | CLIC | ❌ 否 |
| 86 | `f4c9c89637` | hw/intc: Add tail-chaining check for clic v0.10 | 2026-01-06 | TANG Tiancheng | CLIC | ❌ 否 |
| 87 | `df4d5e9d82` | hw/intc: Use sintthresh instead of mintthresh in xt_clic_v0p10_next_interrupt | 2026-01-14 | TANG Tiancheng | CLIC | ❌ 否 |
| 88 | `154958716c` | hw/intc: Short-circuit nvbits check in SHV interrupt in clic 0.9/0.10 | 2026-01-14 | TANG Tiancheng | CLIC | ❌ 否 |
| 89 | `d91a9fb5bd` | target/riscv: Fix scause update in rmw_mnxti | 2026-01-06 | TANG Tiancheng | target/riscv | ✅ 是 |
| 90 | `a29e8129a3` | target/riscv: Use decode_exccode to get exccode/mode/il in clic instead of encode these to exception_index | 2026-01-06 | TANG Tiancheng | CLIC | ❌ 否 |
| 91 | `38e037948d` | target/riscv: Remove mask in rmw_xiselect() | 2026-01-06 | TANG Tiancheng | Smcsrind | ❌ 否 ⬅️ Revert |
| 92 | `73d28ab11b` | hw/timer: Each csky timer triggers irq to all harts | 2026-01-06 | LIU Zhiwei | 其他 | ❌ 否 |
| 93 | `63934cfef0` | physmem: Use (*plen - 1) as mask for orig_addr when translate | 2026-01-06 | LIU Zhiwei | 其他 | ❌ 否 |
| 94 | `cd6a904b7c` | target/riscv: Update suenq csr check | 2026-01-05 | TANG Tiancheng | XuanTie CSR | ❌ 否 |
| 95 | `5225397a94` | target/riscv: Fix function 'smode' parameter passing | 2026-01-04 | TANG Tiancheng | target/riscv | ✅ 是 |
| 96 | `482b2a19f9` | hw/riscv: Replace '_' to '-' in object_class_property of xiaohui | 2026-01-01 | TANG Tiancheng | Board | ❌ 否 |
| 97 | `824951d9fd` | target/riscv: Add SUENQ bit support in [m | h]envcfg | 2025-12-30 20:52:45 +0800|TANG Tiancheng | XuanTie CSR | ❌ 否 |
| 98 | `c7db9403e0` | target/riscv: Remove MXSTATUS_AIOE control | 2025-12-30 | TANG Tiancheng | xtheadaioe | ❌ 否 |
| 99 | `11dff1829d` | target/riscv: Replace the cpu r908a type determination with object_dynamic_cast | 2025-12-30 | TANG Tiancheng | R908A | ❌ 否 |
| 100 | `65b9fea2d2` | target/riscv: Add matrix extension check for special function instructions | 2025-12-31 | TANG Tiancheng | Matrix | ❌ 否 |
| 101 | `7eb89f92d3` | target/riscv: Fix control in require_matrix | 2025-12-30 | TANG Tiancheng | Matrix | ✅ 是 |
| 102 | `e395f674fb` | target/riscv: Fix ext_xtheadvector with vector extensions in c930v | 2025-12-30 | TANG Tiancheng | xtheadvector | ✅ 是 |
| 103 | `de8760e4a0` | target/riscv: Fix ISELECT_MASK to support xuantie custom csr | 2025-12-30 | TANG Tiancheng | XuanTie CSR | ✅ 是 |
| 104 | `6ce1170884` | target/riscv: Fix th_vfsig_w calculation for infinity | 2025-12-29 | TANG Tiancheng | xtheadvector | ✅ 是 |
| 105 | `c153b5acdc` | target/riscv: Enable svrsw60t59b for c930 and zhijiang | 2025-12-29 | LIU Zhiwei | svrsw60t59b | ❌ 否 ⬅️ Revert |
| 106 | `4c2a9d9ab1` | hw/riscv: Merge xiaohui_v2 to xiaohui | 2025-12-29 | LIU Zhiwei | Board | ❌ 否 |
| 107 | `a69456920d` | hw/riscv: Set xt_monchipba from xiaohui platform | 2025-12-26 | LIU Zhiwei | Board | ❌ 否 |
| 108 | `1dfda88721` | target/riscv: Model monchipba and mapbaddr for r908a | 2025-12-26 | LIU Zhiwei | R908A | ❌ 否 |
| 109 | `2f322e9de8` | hw/riscv: Rename clint to aclint for xiaohui_v2 | 2025-12-25 | LIU Zhiwei | Board | ❌ 否 |
| 110 | `50f26982b9` | hw/misc: Implement num_indexes callback for RISC-V IOMMU | 2025-12-24 | TANG Tiancheng | hw/misc | ❌ 否 |
| 111 | `3fc4d8be44` | target/riscv: Add new ignore csrs of CacheLock for R908A | 2025-12-23 | TANG Tiancheng | CacheLock/xtheadcacherpl | ❌ 否 |
| 112 | `62221488d9` | target/riscv: Add dynamic control for xthead extensions for c930 | 2025-12-23 | TANG Tiancheng | C930 | ❌ 否 |
| 113 | `e2e5520d62` | target/riscv: Fix typo in csr.c | 2025-12-23 | TANG Tiancheng | Misc | ✅ 是 |
| 114 | `4aaafd2dd7` | target/riscv: Fix CPURISCVTBFlags.flags2 type mismatch | 2025-12-23 | TANG Tiancheng | target/riscv | ✅ 是 |
| 115 | `2c90182d93` | target: riscv: Add Svrsw60t59b extension support | 2025-12-22 | TANG Tiancheng | svrsw60t59b | ❌ 否 ⬅️ Revert |
| 116 | `663ed08cbb` | target/riscv: Update the ignore csr for XT | 2025-12-19 | TANG Tiancheng | XuanTie CSR | ❌ 否 |
| 117 | `e5a2bfce3b` | target/riscv: Add zvkgs and zvbc32e to r908a | 2025-12-19 | TANG Tiancheng | zvkgs/zvbc32e/zbc | ❌ 否 |
| 118 | `ccf07051ab` | target/riscv: Add tcm_split to riscv_cpu_properties | 2025-12-19 | TANG Tiancheng | CPU | ❌ 否 |
| 119 | `6bc887f8b4` | target/riscv: Use c910 instead of c910v in xml | 2025-12-24 | LIU Zhiwei | C910 | ❌ 否 |
| 120 | `90bea94b19` | target/riscv: Fix jcount error | 2025-12-24 | LIU Zhiwei | Misc | ✅ 是 |
| 121 | `8598e8b7f9` | target/riscv: Fix xtvec write | 2025-12-24 | LIU Zhiwei | target/riscv | ✅ 是 |
| 122 | `fd08f33fe2` | target/riscv: Fix vlm.v/vsm.v for vlen 4096 | 2025-12-24 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 123 | `cb20360f9d` | target/riscv: Fix vmacc54h | 2025-12-23 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 124 | `97a93d9c00` | target/riscv: Fix TPE check | 2025-12-20 | LIU Zhiwei | TPE | ✅ 是 |
| 125 | `e88e728218` | hw/intc: Remove sintstatus and fix level check for CLIC | 2025-12-20 | TANG Tiancheng | CLIC | ✅ 是 |
| 126 | `6406650ee1` | target/riscv: Encode hartid in the irq index for riscv_clic_set_irq | 2025-12-19 | TANG Tiancheng | CLIC | ❌ 否 |
| 127 | `3e34999176` | target/riscv: Fix bql lock in riscv_clic_set_irq | 2025-12-19 | TANG Tiancheng | CLIC | ✅ 是 |
| 128 | `581cf3db46` | target/riscv: Don't guard vmid by vmid_en | 2025-12-21 | LIU Zhiwei | VMID | ❌ 否 |
| 129 | `56bc67d79d` | target/riscv: Add more tb flags for TPE | 2025-12-20 | LIU Zhiwei | TPE | ❌ 否 |
| 130 | `b15b85c9bd` | fpu: Fix floor_normal and ceil_normal | 2025-12-19 | LIU Zhiwei | fpu | ✅ 是 |
| 131 | `7ef64608f6` | target/riscv: Interrupt compatibility with CLINT mode in XT-CLIC | 2025-12-19 | TANG Tiancheng | CLIC | ❌ 否 |
| 132 | `28dfc8cfaa` | hw/dma: Fix compile error for dw_dma_sw_axi | 2025-12-18 | TANG Tiancheng | hw/dma | ✅ 是 |
| 133 | `37b99b7da4` | target/riscv: fix exccode clearing in rmw_mnxti | 2025-12-18 | TANG Tiancheng | target/riscv | ✅ 是 |
| 134 | `d18cf7fa68` | hw/riscv: Replace dw_dma_sw to dw_dma_sw_axi in xiaohui_v2 | 2025-12-18 | TANG Tiancheng | hw/dma | ❌ 否 |
| 135 | `35540096b7` | hw/dma: Split dw_dma_sw into AHB/AXI variants with shared common logic | 2025-12-16 | TANG Tiancheng | hw/dma | ❌ 否 |
| 136 | `34b5d8de6b` | Revert "hw/dummyh: Fix resource leak by coverity" | 2025-12-16 | LIU Zhiwei | 其他 | ✅ 是 |
| 137 | `671f81b37e` | target/risv: Enable iopmp for r908a | 2025-12-16 | LIU Zhiwei | IOPMP | ❌ 否 |
| 138 | `fa3faf3a28` | hw/riscv: Fix riscv_aclint connect to v0.10 clic | 2025-12-16 | LIU Zhiwei | CLIC | ✅ 是 |
| 139 | `b300147a41` | target/riscv: Use vmid for iopmp for xuantie cpu | 2025-12-15 | LIU Zhiwei | VMID | ❌ 否 |
| 140 | `d48914399e` | hw/riscv: Fix aplic level interrupt | 2025-12-15 | LIU Zhiwei | Board | ✅ 是 |
| 141 | `62edd7f8ce` | hw/riscv: Add default cpu to r908afdvk-xt | 2025-12-15 | LIU Zhiwei | R908A | ❌ 否 |
| 142 | `28819e83ef` | target/riscv: Add miss c908vk-cp_v2 cpu | 2025-12-12 | LIU Zhiwei | C908 | ❌ 否 |
| 143 | `ea76704dfe` | hw/riscv: Remove bql_unlock from dma transcation | 2025-12-12 | LIU Zhiwei | hw/riscv | ❌ 否 |
| 144 | `3285439d45` | hw/riscv: Use 32 as IOPMP IRQ | 2025-12-11 | LIU Zhiwei | IOPMP | ❌ 否 |
| 145 | `ca2e6fd0c4` | target/riscv: Add c908vk_v2/c908vk-xt_v2 support | 2025-12-10 | TANG Tiancheng | C908 | ❌ 否 |
| 146 | `0e31d5a0a3` | accel/tcg: Store section pointer in CPUTLBEntryFull | 2025-01-21 | Ethan Chen | 其他 | ❌ 否 |
| 147 | `eb83930721` | target/riscv: Fix mmext_fp_cvt for high part only insn | 2025-12-10 | LIU Zhiwei | target/riscv | ✅ 是 |
| 148 | `2bc6f69d15` | target/riscv: Fix spmpswitch access by mireg/2 | 2025-12-09 | TANG Tiancheng | SPMP | ✅ 是 |
| 149 | `a2f2d000c8` | target/riscv: Add xt_pmaaddr32[0-31] and rename xt_pmaaddr to xt_pmaaddr64 | 2025-12-09 | TANG Tiancheng | target/riscv | ❌ 否 |
| 150 | `4b7a7c91e8` | target/riscv: Add the default size of mdtcmcr/mitcmcr for r908a | 2025-12-08 | TANG Tiancheng | R908A | ❌ 否 |
| 151 | `27a7aacf77` | target/riscv: Replace pmpaddr_csr_read with spmpaddr_csr_read in rmw_spmpaddr to fix the spmpaddr[0] read | 2025-12-08 | TANG Tiancheng | SPMP | ✅ 是 |
| 152 | `b8ba223532` | target/riscv: Fix return errno for spmp csr | 2025-12-09 | HTT | SPMP | ✅ 是 |
| 153 | `c0dc15405a` | hw/riscv/tpe: Fix array indexing and lock management | 2025-12-06 | LIU Zhiwei | TPE | ✅ 是 |
| 154 | `01c4223122` | target/riscv: Fix dequant for out bound of sizem * sizek | 2025-12-05 | LIU Zhiwei | Matrix | ✅ 是 |
| 155 | `4ab8b78172` | target/riscv: Add v0.10 clic sintthresh, sintstatus access | 2025-12-05 | TANG Tiancheng | CLIC | ❌ 否 |
| 156 | `51aced2477` | hw/intc: Fix the clicintattr write mask of CLIC v0.10 | 2025-12-04 | TANG Tiancheng | CLIC | ✅ 是 |
| 157 | `0e1692f1be` | target/riscv: Fix mmext_fp_cvt for ms1==md case | 2025-12-05 | LIU Zhiwei | target/riscv | ✅ 是 |
| 158 | `98531a8938` | fpu: Fix ceil_normal | 2025-12-05 | LIU Zhiwei | fpu | ✅ 是 |
| 159 | `8e07cfca56` | target/riscv: Fix mldts6/mldtu6 | 2025-12-04 | LIU Zhiwei | Matrix | ✅ 是 |
| 160 | `42989dcc4b` | hw/riscv: Support one TPE per core | 2025-12-04 | LIU Zhiwei | TPE | ❌ 否 |
| 161 | `c1e8b7c91c` | target/riscv: Fix xnxti read/wirte in clic | 2025-12-03 | TANG Tiancheng | CLIC | ✅ 是 |
| 162 | `2a5f8eb233` | target/riscv: Fix sfu compile error for higher cpp compiler | 2025-12-03 | TANG Tiancheng | SFU/SFA | ✅ 是 |
| 163 | `de35024414` | disas/riscv.c: Fix compile bug of function 'append' | 2025-07-18 | Lyndra | disas | ✅ 是 |
| 164 | `6b8d0fd4f2` | target/riscv: Enable ssclic for r908a | 2025-12-03 | LIU Zhiwei | CLIC | ❌ 否 |
| 165 | `a9c8935cae` | target/riscv: Add v0.10 clic stvt support | 2025-12-03 | LIU Zhiwei | CLIC | ❌ 否 |
| 166 | `84ea986492` | target/riscv: Add sub extensions for CLIC v0.10 | 2025-12-03 | LIU Zhiwei | CLIC | ❌ 否 |
| 167 | `a86ba4d60f` | target/riscv: Fix the mintthresh read and wirte in xt_clic_v0p10 | 2025-12-03 | TANG Tiancheng | CLIC | ✅ 是 |
| 168 | `3c4d9101a2` | target/riscv: Add pmu_mask init for rv32_r908afdvk_xt_cpu_init | 2025-12-02 | TANG Tiancheng | R908A | ❌ 否 |
| 169 | `90987463f9` | hw/intc: Add peripherals routing interrupts to CLIC via APLIC | 2025-12-03 | TANG Tiancheng | CLIC | ❌ 否 |
| 170 | `bfb74ea42d` | Revert "hw/intc: Add peripherals routing interrupts to CLIC via APLIC" | 2025-12-03 | TANG Tiancheng | CLIC | ❌ 否 |
| 171 | `dfa97f09fe` | hw/intc: Add peripherals routing interrupts to CLIC via APLIC | 2025-12-01 | TANG Tiancheng | CLIC | ❌ 否 |
| 172 | `812b7e1382` | hw/intc: Add error logging for WPRI field of clicintattr in clic_v0p10 | 2025-12-01 | TANG Tiancheng | CLIC | ❌ 否 |
| 173 | `3c5fecfb10` | hw/intc: Fix the clicintattr[i].mode read for clic_v0p10 | 2025-12-01 | TANG Tiancheng | CLIC | ✅ 是 |
| 174 | `6ff4905157` | Revert "hw/intc: Fix the clicintattr[i].mode read for clic_v0p10" | 2025-12-01 | TANG Tiancheng | CLIC | ✅ 是 |
| 175 | `785a0a5264` | hw/intc: Fix the clicintattr[i].mode read for clic_v0p10 | 2025-12-01 | TANG Tiancheng | CLIC | ✅ 是 |
| 176 | `35c5faccc5` | target/riscv: Fix mldtu6/mldts6 | 2025-11-28 | LIU Zhiwei | Matrix | ✅ 是 |
| 177 | `ddd4eee202` | fpu: Set to -0 for value -1 < x < 0 | 2025-11-28 | LIU Zhiwei | fpu | ❌ 否 |
| 178 | `440be82e7c` | fpu: Add NaN process for mfceil/mffloor | 2025-11-28 | LIU Zhiwei | fpu | ❌ 否 |
| 179 | `89a97ba400` | Revert "target/riscv: Nanbox for mffloor" | 2025-11-28 | LIU Zhiwei | target/riscv | ❌ 否 |
| 180 | `a4b4fa9220` | Revert "target/riscv: Nanbox source for matrix unary fp op" | 2025-11-28 | LIU Zhiwei | Matrix | ❌ 否 |
| 181 | `25c98d98a0` | target/riscv: Lock xireg_clic as it may trigger interrupt | 2025-11-27 | LIU Zhiwei | CLIC | ❌ 否 |
| 182 | `ec24b83978` | target/riscv: Fix mfred*.abs | 2025-11-27 | LIU Zhiwei | Matrix | ✅ 是 |
| 183 | `7a979e9816` | target/riscv: Fix sxstatus when debug | 2025-11-26 | LIU Zhiwei | XuanTie CSR | ✅ 是 |
| 184 | `abb464b0b7` | hw/riscv: Fix gpex pci host bridge delete when add iopmp | 2025-11-25 | LIU Zhiwei | IOPMP | ✅ 是 |
| 185 | `0ccb9501e9` | targe/riscv: Fix clic v0.10 interrupt process | 2025-11-20 | LIU Zhiwei | CLIC | ✅ 是 |
| 186 | `1439255a51` | target/riscv: Fix riscv_xt_clic interfaces | 2025-11-20 | LIU Zhiwei | CLIC | ✅ 是 |
| 187 | `99e2e517fe` | hw/riscv: Fix clic v0.10 ip write and il/ipri calculation | 2025-11-20 | LIU Zhiwei | CLIC | ✅ 是 |
| 188 | `a74e817aef` | hw/riscv: Use xt_clic_v0p10 for aclint | 2025-11-20 | LIU Zhiwei | CLIC | ❌ 否 |
| 189 | `f61d494478` | target/riscv: Fix mpmpdeleg and spmpswitch csr number | 2025-11-17 | LIU Zhiwei | SPMP | ✅ 是 |
| 190 | `586da25788` | target/riscv: Nanbox source for matrix unary fp op | 2025-11-14 | LIU Zhiwei | Matrix | ❌ 否 |
| 191 | `d87bf0093e` | target/riscv: Nanbox for mffloor | 2025-11-14 | LIU Zhiwei | target/riscv | ❌ 否 |
| 192 | `b4464ec0e1` | target/riscv: Add an empty newline to quiet checkpatch | 2025-11-14 | LIU Zhiwei | Build | ❌ 否 |
| 193 | `d0af11310f` | target/riscv: Use spmp_start instead of RISCV_MAX_PMPS for pmp only rules | 2025-11-14 | LIU Zhiwei | SPMP | ❌ 否 |
| 194 | `f4ea8d8488` | hw/dma: Fix dw dma build | 2025-11-14 | LIU Zhiwei | hw/dma | ✅ 是 |
| 195 | `9fde736b9f` | hw/riscv: Use vmid as dma rrid | 2025-11-14 | LIU Zhiwei | VMID | ❌ 否 |
| 196 | `0df038ad26` | hw/riscv: Add stream link for DMA | 2025-11-13 | LIU Zhiwei | hw/riscv | ❌ 否 |
| 197 | `4880608fef` | target/riscv: Fix csrw for spmp | 2025-11-13 | LIU Zhiwei | SPMP | ✅ 是 |
| 198 | `cb4296763f` | target/riscv: Fix csrw for clic 0.10 | 2025-11-13 | LIU Zhiwei | CLIC | ✅ 是 |
| 199 | `237230af96` | target/riscv: Fix mn4clip* | 2025-11-12 | LIU Zhiwei | Matrix | ✅ 是 |
| 200 | `e121f483d7` | target/riscv: Fix use max/minimum_number for TPE | 2025-11-12 | LIU Zhiwei | TPE | ✅ 是 |
| 201 | `74d6531b03` | target/riscv: Ingore matrix size configuration for mn4clip* | 2025-11-12 | LIU Zhiwei | Matrix | ❌ 否 |
| 202 | `980f2d41c4` | target/riscv: Fix mpackhl according to new specification | 2025-11-11 | LIU Zhiwei | Matrix | ✅ 是 |
| 203 | `689fcd8904` | target/riscv: Only use rs1 log2(rlen-elen) bit | 2025-11-11 | LIU Zhiwei | target/riscv | ❌ 否 |
| 204 | `df8d26944f` | target/riscv: virt_enabled and xt_vmid_en must not enabled at same time | 2025-11-11 | LIU Zhiwei | VMID | ❌ 否 |
| 205 | `0de4cfc7fd` | target/riscv: Enable clint_clic for r908a | 2025-11-11 | LIU Zhiwei | CLIC | ❌ 否 |
| 206 | `c9617e498a` | target/riscv: Fix clic indirect csr access | 2025-11-11 | LIU Zhiwei | CLIC | ✅ 是 |
| 207 | `d380a35462` | target/riscv: Fix custom xiselect mask | 2025-11-11 | LIU Zhiwei | Smcsrind | ✅ 是 ⬅️ Revert |
| 208 | `e625b50a74` | target/riscv: Add zbc for r908a | 2025-11-10 | LIU Zhiwei | zvkgs/zvbc32e/zbc | ❌ 否 |
| 209 | `838190b93f` | target/riscv: Set mpmpdeleg value reset value | 2025-11-10 | LIU Zhiwei | SPMP | ❌ 否 |
| 210 | `b80fe9079a` | target/riscv: Fix vmid access error | 2025-11-10 | LIU Zhiwei | VMID | ✅ 是 |
| 211 | `752f655533` | target/riscv: Use Jama mpmpdelge CSR number | 2025-11-10 | LIU Zhiwei | CSR | ❌ 否 |
| 212 | `d8b73e7d6a` | target/riscv: Fix R908A CSR encoding | 2025-11-10 | LIU Zhiwei | R908A | ✅ 是 |
| 213 | `68fba9ef54` | target/riscv: Add Xuantie VMID CSR support | 2025-11-10 | LIU Zhiwei | XuanTie CSR | ❌ 否 |
| 214 | `43a64d8dca` | target/riscv: Adjust stimecmp write behavior with mtimedelta | 2025-11-10 | LIU Zhiwei | XuanTie CSR | ❌ 否 |
| 215 | `629f5aeec7` | target/riscv: Add Xuantie CSR mtimedelta | 2025-11-10 | LIU Zhiwei | XuanTie CSR | ❌ 否 |
| 216 | `9cfdd75f96` | target/riscv: Support xuantie mtimedelta offset in time csr | 2025-11-10 | LIU Zhiwei | XuanTie CSR | ❌ 否 |
| 217 | `cf52873d57` | target/riscv: Update r908a priv version to 1.13 | 2025-11-07 | LIU Zhiwei | R908A | ❌ 否 |
| 218 | `7ea7efdb1c` | target/riscv: Enable Smpmpdeleg for r908a | 2025-11-07 | LIU Zhiwei | SPMP | ❌ 否 |
| 219 | `03220bc337` | target/riscv: Add spmp CSR config | 2025-11-07 | LIU Zhiwei | SPMP | ❌ 否 |
| 220 | `8620b89c06` | target/riscv: Fix v0.10 clic xireg access | 2025-11-06 | LIU Zhiwei | CLIC | ✅ 是 |
| 221 | `1f24b4ccb8` | target/riscv: Add Smpmpdeleg support | 2025-11-06 | LIU Zhiwei | SPMP | ❌ 否 |
| 222 | `a9a0d8b555` | target/riscv: Update pmp entry to 64 | 2025-11-05 | LIU Zhiwei | PMP | ❌ 否 |
| 223 | `be20d3b2d6` | target/riscv: Support spmp check | 2025-11-05 | LIU Zhiwei | SPMP | ❌ 否 |
| 224 | `da5f03546c` | hw/riscv: Add iopmp to xiaohui_v2 | 2025-11-01 | LIU Zhiwei | IOPMP | ❌ 否 |
| 225 | `3a0d4cb262` | target/riscv: Add new extensions for r908afdv-xt | 2025-09-04 | LIU Zhiwei | R908A | ❌ 否 |
| 226 | `c8eb2cba16` | target/riscv: Support r908afdvk-xt | 2025-08-12 | LIU Zhiwei | R908A | ❌ 否 |
| 227 | `63af90b24d` | target/riscv: Enable zfa for r908afdv-xt | 2025-08-08 | LIU Zhiwei | R908A | ❌ 否 |
| 228 | `6f23f689e2` | target/riscv: Add Zicsr for r908afdv-xt | 2025-08-08 | LIU Zhiwei | R908A | ❌ 否 |
| 229 | `59369fc3fa` | target/riscv: Use r908afdv-xt cpu as HRD requires | 2025-08-07 | LIU Zhiwei | R908A | ❌ 否 |
| 230 | `d95364f091` | target/riscv: Add some custom CSR for r908a | 2025-08-04 | LIU Zhiwei | R908A | ❌ 否 |
| 231 | `c43083ea67` | target/riscv: Add experimental r908a cpu | 2025-08-04 | LIU Zhiwei | R908A | ❌ 否 |
| 232 | `3bdb1a5e56` | target/riscv: Add Zilsd and Zclsd extension support | 2025-08-04 | LIU Zhiwei | Zilsd/Zclsd | ❌ 否 |
| 233 | `1c7f560797` | target/riscv: Update sfu cmodel | 2025-11-01 | LIU Zhiwei | SFU/SFA | ❌ 否 |
| 234 | `7a5a379676` | target/riscv: Use common clic interface for idle | 2025-10-31 | LIU Zhiwei | CLIC | ❌ 否 |
| 235 | `045f441695` | target/riscv: Use common clic interface for interrupt entry and return | 2025-10-31 | LIU Zhiwei | CLIC | ❌ 否 |
| 236 | `186616253c` | target/riscv: Add riscv_xt_clic.c for common clic interfaces | 2025-10-31 | LIU Zhiwei | CLIC | ❌ 否 |
| 237 | `4eb2c67a68` | configs/devices: Remove RISCV_CLIC as no machine use it | 2025-10-31 | LIU Zhiwei | CLIC | ❌ 否 |
| 238 | `af43ee6c40` | hw/intc: Always use interrupt level with no 1s padding | 2025-10-31 | LIU Zhiwei | hw/intc | ❌ 否 |
| 239 | `d69c3ba43c` | target/riscv: Add new smode CSR for v0.10 CLIC | 2025-10-30 | LIU Zhiwei | CLIC | ❌ 否 |
| 240 | `5443beb843` | add isatrace plugin, add interface qemu_plugin_read_memory_vaddr | 2025-10-30 | jianchang.xjc | plugins | ❌ 否 |
| 241 | `8488cf3571` | hw/riscv: Add v0.10 clic to xiaohuiv2 | 2025-10-30 | LIU Zhiwei | CLIC | ❌ 否 |
| 242 | `fbf26efbd8` | target/riscv: Add CSR support for v0.10 CLIC | 2025-10-30 | LIU Zhiwei | CLIC | ❌ 否 |
| 243 | `5fb54d4064` | hw/riscv: Fix xt_clic_v0p10_is_clic_mode | 2025-10-30 | LIU Zhiwei | CLIC | ✅ 是 |
| 244 | `4fa804cbba` | target/riscv: Don't round TPE sra/srl | 2025-10-29 | LIU Zhiwei | TPE | ❌ 否 |
| 245 | `948c347d82` | target/riscv: Fix mfred dup for e32/e64 | 2025-10-29 | LIU Zhiwei | Matrix | ✅ 是 |
| 246 | `f5f8f2f7da` | target/riscv: Support indirect CSR access for clic 0.10 | 2025-10-27 | LIU Zhiwei | CLIC | ❌ 否 |
| 247 | `17e8417966` | hw/intc: Support CLIC 0.10 specification | 2025-10-27 | LIU Zhiwei | CLIC | ❌ 否 |
| 248 | `88fe59415b` | hw/riscv: Fix clic int level according to hardware | 2025-10-23 | LIU Zhiwei | CLIC | ✅ 是 |
| 249 | `f79333f1eb` | hw/riscv: Fix clic mintthresh mth | 2025-10-23 | LIU Zhiwei | CLIC | ✅ 是 |
| 250 | `6f1385874e` | hw/riscv: Rename xiaohui-v2 to xiaohui_v2 | 2025-10-23 | LIU Zhiwei | Board | ❌ 否 |
| 251 | `a39af3a21e` | hw/riscv: Use aplic for xiaohui v2 | 2025-10-22 | LIU Zhiwei | Board | ❌ 否 |
| 252 | `0b897ac4f6` | hw/misc: Fix properties in IOPMP | 2025-10-22 | LIU Zhiwei | IOPMP | ✅ 是 |
| 253 | `fe05a20ebd` | hw/riscv: Always cpu on for linux boot on xiaohui | 2025-10-22 | LIU Zhiwei | Board | ❌ 否 |
| 254 | `4a22f0677b` | include: Fix a miss file | 2024-11-26 | LIU Zhiwei | 其他 | ✅ 是 |
| 255 | `69ad746a04` | hw/intc: Add a fixed_msi mode for aplic | 2024-11-26 | LIU Zhiwei | Board | ❌ 否 |
| 256 | `18361be9e1` | hw/riscv: Add xiaohui-v2 with dw_dma | 2025-10-17 | TANG Tiancheng | hw/dma | ❌ 否 |
| 257 | `b53b92b892` | hw/dma: Add DesignWare AHB DMA controller with software handshake only | 2025-10-17 | TANG Tiancheng | hw/dma | ❌ 否 |
| 258 | `db667a3f8f` | hw/riscv/virt: Add IOPMP support | 2025-10-18 | LIU Zhiwei | IOPMP | ❌ 否 |
| 259 | `88b27dcd3f` | hw/misc/riscv_iopmp_dispatcher: Device for redirect IOPMP transaction infomation | 2025-03-12 | Ethan Chen | IOPMP | ❌ 否 |
| 260 | `b3d4c4066a` | hw/misc/riscv_iopmp: Add RISC-V IOPMP device | 2025-03-12 | Ethan Chen | IOPMP | ❌ 否 |
| 261 | `fa8eb09839` | hw/misc/riscv_iopmp_txn_info: Add struct for transaction infomation | 2025-03-12 | Ethan Chen | IOPMP | ❌ 否 |
| 262 | `ebddc79b54` | target/riscv: Add support for IOPMP | 2025-03-12 | Ethan Chen | IOPMP | ❌ 否 |
| 263 | `d28cd1ae5b` | system/physmem: Support IOMMU granularity smaller than TARGET_PAGE size | 2025-03-12 | Ethan Chen | riscv-iommu | ❌ 否 |
| 264 | `0bb2ac8732` | memory: Introduce memory region fetch operation | 2025-03-12 | Ethan Chen | 其他 | ❌ 否 |
| 265 | `baf45e28d6` | hw/core: Add config stream | 2025-03-12 | Ethan Chen | 其他 | ❌ 否 |
| 266 | `891d47221b` | hw/misc: Remove old iopmp implemention | 2025-10-18 | LIU Zhiwei | IOPMP | ❌ 否 |
| 267 | `ee732668d8` | target/riscv: Fix matrix mfmax/mfmin/mfmul/mfsub_bf16_mv/mc_i | 2025-10-13 | LIU Zhiwei | Matrix | ✅ 是 |
| 268 | `3e9c9b2d93` | target/riscv: Fix th_vs predicate | 2025-10-11 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 269 | `22348017a8` | target/riscv: Fix vfmv.s.f tail process | 2025-10-10 | HTT | xtheadvector | ✅ 是 |
| 270 | `ccc0516e55` | hw/riscv: Fix tpe checkpatch error | 2025-10-09 | LIU Zhiwei | TPE | ✅ 是 |
| 271 | `abafeaef04` | hw/riscv: Only raise irq when xmdmaerrinfo v bit is zero | 2025-10-09 | LIU Zhiwei | TPE | ❌ 否 |
| 272 | `b908b4c5ae` | hw/riscv: Add an temp address space for crosspage check | 2025-10-09 | LIU Zhiwei | hw/riscv | ❌ 否 |
| 273 | `bfeaf498fd` | target/riscv: Fix 32bit compile | 2025-10-09 | LIU Zhiwei | Build | ✅ 是 |
| 274 | `b1ee324b43` | target/riscv: Fix mcslidedown helper function | 2025-10-09 | LIU Zhiwei | Matrix | ✅ 是 |
| 275 | `87c322214b` | hw/riscv: Enable huge page check | 2025-09-29 | LIU Zhiwei | hw/riscv | ❌ 否 |
| 276 | `ca5346d86e` | hw/riscv: Check crosspage for TPE | 2025-09-27 | LIU Zhiwei | TPE | ❌ 否 |
| 277 | `fdb0add473` | hw/riscv: Don't probe TLB in bh function | 2025-09-27 | LIU Zhiwei | hw/riscv | ❌ 否 |
| 278 | `68c9976671` | target/riscv: Fix ISELECT_MASK_SXCSRIND for C930 CSR | 2025-09-25 | LIU Zhiwei | Smcsrind | ✅ 是 ⬅️ Revert |
| 279 | `2d864a1ae0` | target/riscv: Fix suenq CSR number | 2025-09-25 | LIU Zhiwei | XuanTie CSR | ✅ 是 |
| 280 | `15e9d904b0` | target/riscv: Fix smmpt csr number | 2025-09-25 | LIU Zhiwei | CSR | ✅ 是 |
| 281 | `5a2a2f2ca1` | target/riscv: Fix c930 cpu names | 2025-09-25 | LIU Zhiwei | C930 | ✅ 是 |
| 282 | `2c31e9b3a2` | target/riscv: Fix mingw compile for sfu | 2025-09-25 | LIU Zhiwei | SFU/SFA | ✅ 是 |
| 283 | `b0222828cf` | target/riscv: Fix 32bit compile | 2025-09-25 | LIU Zhiwei | Build | ✅ 是 |
| 284 | `4e9b387565` | target/riscv: Fix TPE DMA interrupt missing | 2025-09-24 | LIU Zhiwei | TPE | ✅ 是 |
| 285 | `56e5edc837` | hw/riscv: Add dma_id > 7 error check | 2025-09-17 | LIU Zhiwei | hw/riscv | ❌ 否 |
| 286 | `792273a492` | target/riscv: Fix xmdmaidle clean for DMA copy insns | 2025-09-17 | LIU Zhiwei | TPE | ✅ 是 |
| 287 | `195603326f` | target/riscv: Remove fp6 and sfu vector insns from c930 and zhijiang | 2025-09-17 | LIU Zhiwei | SFU/SFA | ❌ 否 |
| 288 | `9a6768fa80` | target/riscv: Ignore C930 CSRs we don't support | 2025-09-15 | LIU Zhiwei | C930 | ❌ 否 |
| 289 | `b29df72fb5` | target/riscv: Support C930 cpu | 2025-09-15 | LIU Zhiwei | C930 | ❌ 否 |
| 290 | `19f6d21572` | target/riscv: Make TPE sync ordering stricter | 2025-09-15 | LIU Zhiwei | TPE | ❌ 否 |
| 291 | `c29ad41cbc` | target/riscv: Fix compile error | 2025-09-13 | LIU Zhiwei | Build | ✅ 是 |
| 292 | `77b963a732` | target/riscv: Support TPE sync operations | 2025-09-13 | LIU Zhiwei | TPE | ❌ 否 |
| 293 | `29719340aa` | target/riscv: Exchange ms1 and ms2 for TPE reduction | 2025-09-13 | LIU Zhiwei | TPE | ❌ 否 |
| 294 | `10ad8d0cf1` | hw/riscv/riscv-iommu: Fixup pdt_memory_read | 2025-09-12 | yfy_lazy | riscv-iommu | ✅ 是 |
| 295 | `4ab4965eea` | hw/riscv: Fix check patch error | 2025-09-10 | LIU Zhiwei | hw/riscv | ✅ 是 |
| 296 | `c7b66312d7` | target/riscv: Enable tcm_en access control | 2025-09-10 | LIU Zhiwei | TPE | ❌ 否 |
| 297 | `ba2512cae2` | target/riscv: Fix 6bit copy to tcm for TPE | 2025-09-09 | LIU Zhiwei | TPE | ✅ 是 |
| 298 | `fc522e3f99` | target/riscv: Give different names to each TCM MR and AS | 2025-09-05 | LIU Zhiwei | target/riscv | ❌ 否 |
| 299 | `ae91a44eb5` | hw/riscv/riscv-iommu: Fixup PDT Nested Walk | 2025-08-03 | Guo Ren (Alibaba DAMO Academy) | riscv-iommu | ✅ 是 |
| 300 | `e1a7d7dfc2` | target/riscv: Remove a16w4/a16w8 | 2025-09-04 | LIU Zhiwei | Matrix | ❌ 否 |
| 301 | `7977aff158` | target/riscv: Add mfmacc.bf16 for TPE | 2025-09-04 | LIU Zhiwei | TPE | ❌ 否 |
| 302 | `b402ca6cbc` | target/riscv: Add bf16 dequant insns | 2025-09-03 | LIU Zhiwei | Matrix | ❌ 否 |
| 303 | `6caf526302` | Merge branch c358a2bc9094fc2af1a4ce63f88dbaadb6eecc36 into xuantie-v9.0-dev Title: Merge request from 甲一 | 2025-09-03 | lzw194868 | Merge | ❌ 否 |
| 304 | `c358a2bc90` | target/riscv: Add xtheadmdma extension to isa string | 2025-09-03 | LIU Zhiwei | Matrix | ❌ 否 |
| 305 | `488e377951` | target/riscv: Fix crc32 insn | 2025-09-03 | LIU Zhiwei | xtheadcrc | ✅ 是 |
| 306 | `e5ab075089` | target/riscv: Support tcmen check for DMA insns | 2025-09-03 | LIU Zhiwei | TPE | ❌ 否 |
| 307 | `2bee752c71` | target/riscv: Support XMDMAERRINFO CSR for TPE DMA | 2025-09-03 | LIU Zhiwei | TPE | ❌ 否 |
| 308 | `019abdd1db` | target/riscv: Add XMTCMCSR support for TPE DMA | 2025-09-03 | LIU Zhiwei | TPE | ❌ 否 |
| 309 | `564fca82f1` | hw/riscv: Support dma_id and fix some bugs | 2025-09-03 | LIU Zhiwei | hw/riscv | ✅ 是 |
| 310 | `8cc737464f` | target/riscv: Support TPE DMA interrupt | 2025-09-03 | LIU Zhiwei | TPE | ❌ 否 |
| 311 | `4b3eaef7d3` | target/riscv: Fix TPE DMA insns ms check | 2025-09-03 | LIU Zhiwei | TPE | ✅ 是 |
| 312 | `5517415c67` | hw/riscv: Uset qemu_bh_new_guarded instead of qemu_bh_new | 2025-09-01 | LIU Zhiwei | hw/riscv | ❌ 否 |
| 313 | `cf781c5ef0` | hw/riscv: Fix checkpatch error | 2025-09-01 | LIU Zhiwei | Build | ✅ 是 |
| 314 | `0d82046d86` | target/riscv: Fix user mode compile for TPE DMA | 2025-09-01 | LIU Zhiwei | TPE | ✅ 是 |
| 315 | `f0f375fae3` | hw/riscv: Add tpe device to virt | 2025-08-31 | LIU Zhiwei | TPE | ❌ 否 |
| 316 | `c81bc092d3` | target/riscv: Matrix load/store to TCM space address | 2025-08-31 | LIU Zhiwei | Matrix | ❌ 否 |
| 317 | `42aebf98cd` | hw/riscv: Read or write to tcm address space | 2025-08-31 | LIU Zhiwei | hw/riscv | ❌ 否 |
| 318 | `e45b6a2a13` | hw/riscv: Add tcm address space for TPE | 2025-08-31 | LIU Zhiwei | TPE | ❌ 否 |
| 319 | `0ad1f38841` | target/riscv: Support mldts6/u6 for DMA | 2025-08-30 | LIU Zhiwei | Matrix | ❌ 否 |
| 320 | `48a1039917` | target/riscv: Support TPE xmdmaidle CSR | 2025-08-29 | LIU Zhiwei | TPE | ❌ 否 |
| 321 | `a54d04c240` | target/riscv: Support matrix ldst access tcm directly | 2025-08-29 | LIU Zhiwei | Matrix | ❌ 否 |
| 322 | `819ddb1af8` | target/riscv: Support TPE fence insns | 2025-08-29 | LIU Zhiwei | TPE | ❌ 否 |
| 323 | `c90534ed83` | target/riscv: Support TPE dma instructions | 2025-08-29 | LIU Zhiwei | TPE | ❌ 否 |
| 324 | `749020e48d` | hw/riscv: Fix tpe_dma_push_coord interface | 2025-08-29 | LIU Zhiwei | hw/riscv | ✅ 是 |
| 325 | `8f62011d49` | hw/riscv: Set max cpus of xiaohui to 16 | 2025-08-28 | LIU Zhiwei | CPU | ❌ 否 |
| 326 | `7452821bca` | hw/riscv: Support Xuantie TPE device | 2025-08-28 | LIU Zhiwei | TPE | ❌ 否 |
| 327 | `2e4766a855` | target/riscv: Use 49 interrupt number for TPE DMA | 2025-08-19 | LIU Zhiwei | TPE | ❌ 否 |
| 328 | `ee1eb6f3a4` | target/riscv: Fix smsdid encoding | 2025-08-15 | LIU Zhiwei | Smsdid | ✅ 是 |
| 329 | `0d192a4e5e` | target/riscv: Fix aioe encode | 2025-08-15 | LIU Zhiwei | xtheadaioe | ✅ 是 |
| 330 | `f8b60bd750` | target/riscv: Free temp dest buffer | 2025-08-15 | LIU Zhiwei | target/riscv | ❌ 否 |
| 331 | `7e17daa6f8` | target/riscv: Fix whole reduction operations | 2025-08-15 | LIU Zhiwei | target/riscv | ✅ 是 |
| 332 | `16189b462d` | target/riscv: Fix TPE reduction | 2025-08-15 | LIU Zhiwei | TPE | ✅ 是 |
| 333 | `70febf6be5` | hw/riscv: Allow write pending bits for plic | 2025-08-15 | LIU Zhiwei | hw/riscv | ❌ 否 |
| 334 | `4ca2c49983` | target/riscv: Fix md/ms overide for TPE reduction | 2025-08-15 | LIU Zhiwei | TPE | ✅ 是 |
| 335 | `a21986cb7d` | target/riscv/sfu: Fix the interface errors | 2025-08-14 | tiancheng.tang | SFU/SFA | ✅ 是 |
| 336 | `3435db4d37` | target/riscv: Fix mfdup is not affected by xmsize | 2025-08-14 | LIU Zhiwei | Matrix | ✅ 是 |
| 337 | `f4b199f853` | target/riscv: Enable sfa for zhijiang | 2025-08-14 | LIU Zhiwei | Matrix | ❌ 否 |
| 338 | `33836f3ba5` | target/riscv: Support SFA functions for TPE | 2025-08-14 | LIU Zhiwei | TPE | ❌ 否 |
| 339 | `670e9d58e8` | target/riscv: Enable TPE reduction for zhijiang | 2025-08-14 | LIU Zhiwei | TPE | ❌ 否 |
| 340 | `8ef9e0baee` | target/riscv: Add mfdup operations for TPE | 2025-08-14 | LIU Zhiwei | TPE | ❌ 否 |
| 341 | `3ee6fa3ac9` | target/riscv: Add whole register reduction operations for TPE | 2025-08-14 | LIU Zhiwei | TPE | ❌ 否 |
| 342 | `4df040c335` | target/riscv: Fix checkpatch warning for xt_reduction.c | 2025-08-14 | LIU Zhiwei | Build | ✅ 是 |
| 343 | `811a6bfe93` | target/riscv: Always init src1 | 2025-08-14 | LIU Zhiwei | target/riscv | ❌ 否 |
| 344 | `ab975df5be` | target/riscv: Add reduction operations for TPE | 2025-08-14 | LIU Zhiwei | TPE | ❌ 否 |
| 345 | `9eda477f02` | target/riscv: Remove double reduction for TPE | 2025-08-14 | LIU Zhiwei | TPE | ❌ 否 |
| 346 | `05f8edf4d9` | target/riscv: Add reduction API for TPE | 2025-08-14 | LIU Zhiwei | TPE | ❌ 否 |
| 347 | `65b26ee0c3` | target/riscv: Add abs parameter for xt reduction | 2025-08-14 | LIU Zhiwei | target/riscv | ❌ 否 |
| 348 | `afaac935ab` | target/riscv: Rename base to init_val for xt reduction | 2025-08-14 | LIU Zhiwei | target/riscv | ❌ 否 |
| 349 | `90d07bb63b` | target/riscv: Add decode for matrix special function instructions | 2025-08-14 | tiancheng.tang | Matrix | ❌ 否 |
| 350 | `ae45d9504d` | target/riscv: Add decode for matrix reduction extension. | 2025-08-13 | tiancheng.tang | Matrix | ❌ 否 |
| 351 | `2e357be80c` | target/riscv: Remove row index from xt reduction funcs | 2025-08-13 | LIU Zhiwei | target/riscv | ❌ 否 |
| 352 | `cde028a49b` | target/riscv: Add base element for xt reduction | 2025-08-13 | LIU Zhiwei | target/riscv | ❌ 否 |
| 353 | `df34453942` | target/riscv: Add row len parameter for xt reduction | 2025-08-13 | LIU Zhiwei | target/riscv | ❌ 否 |
| 354 | `2340721348` | target/riscv: Make hpm19-31 readonly zero for c908x | 2025-08-13 | LIU Zhiwei | C908 | ❌ 否 |
| 355 | `5ff719bf0b` | target/riscv: Add frm and sat parameters for xt reduction funcs | 2025-08-13 | LIU Zhiwei | target/riscv | ❌ 否 |
| 356 | `651136232b` | target/riscv: Move xuantie reduction to xt_reduction.c | 2025-08-13 | LIU Zhiwei | target/riscv | ❌ 否 |
| 357 | `25e6710871` | target/riscv: update sfu | 2025-08-13 | tiancheng.tang | SFU/SFA | ❌ 否 |
| 358 | `7990b8eeee` | target/riscv: Fix ssccfg access for vsireg with mcounteren | 2025-08-12 | LIU Zhiwei | Smcdeleg/Ssccfg | ✅ 是 ⬅️ Revert |
| 359 | `cce5475c23` | target/riscv: Support bf16 vs int8 conversion for TPE | 2025-08-11 | LIU Zhiwei | TPE | ❌ 否 |
| 360 | `858175d6dd` | target/riscv: Support bf16 vs fp8 conversion for TPE | 2025-08-11 | LIU Zhiwei | TPE | ❌ 否 |
| 361 | `410c96efa2` | target/riscv: Support e8m0 conversion for TPE | 2025-08-11 | LIU Zhiwei | TPE | ❌ 否 |
| 362 | `61f5904a34` | target/riscv: Support bf16 to fp4 convert for TPE | 2025-08-11 | LIU Zhiwei | TPE | ❌ 否 |
| 363 | `62af1df110` | fpu: Support bfloat16_to_float4e2 | 2025-08-11 | LIU Zhiwei | BF16 | ❌ 否 |
| 364 | `2f1c1886f3` | target/riscv: Support matrix scalar operations | 2025-08-10 | LIU Zhiwei | Matrix | ❌ 否 |
| 365 | `e023571faa` | target/riscv: Support matrix float and ceil operations | 2025-08-09 | LIU Zhiwei | Matrix | ❌ 否 |
| 366 | `6066b8a1e7` | fpu: Support float ceil operations | 2025-08-09 | LIU Zhiwei | fpu | ❌ 否 |
| 367 | `a989f02370` | fpu: Support floor functions | 2025-08-09 | LIU Zhiwei | fpu | ❌ 否 |
| 368 | `54f2f0c5cd` | target/riscv: Support mfabs.*.mm | 2025-08-09 | LIU Zhiwei | Matrix | ❌ 否 |
| 369 | `fd33ea0e22` | target/riscv: Use stvec_hs for debug in vs mode | 2025-08-08 | LIU Zhiwei | target/riscv | ❌ 否 |
| 370 | `0d4cd2ab25` | target/riscv: Fix ssccfg check | 2025-08-07 | LIU Zhiwei | Smcdeleg/Ssccfg | ✅ 是 ⬅️ Revert |
| 371 | `bba90b80ad` | fpu: Fix e8m0 NaN canonicalize process | 2025-08-01 | LIU Zhiwei | xtheadvfofp | ✅ 是 |
| 372 | `6e1091361f` | target/riscv: Fix cols offset for dequant again | 2025-07-30 | LIU Zhiwei | Matrix | ✅ 是 |
| 373 | `0e89f5924b` | target/riscv: Fix cols offset for dequant | 2025-07-30 | LIU Zhiwei | Matrix | ✅ 是 |
| 374 | `eb00fb4589` | target/riscv: Fix crc compile error | 2025-07-30 | LIU Zhiwei | xtheadcrc | ✅ 是 |
| 375 | `738b96f59c` | target/riscv: Fix no double scale dequant | 2025-07-30 | LIU Zhiwei | Matrix | ✅ 是 |
| 376 | `322c251184` | target/riscv: Fix check on mxfp8-mxfp4 | 2025-07-30 | LIU Zhiwei | Matrix | ✅ 是 |
| 377 | `e4878f23f3` | target/riscv: Add vabsmax.vv for vcoder extension | 2025-07-30 | LIU Zhiwei | xtheadvcoder | ❌ 否 |
| 378 | `2676507245` | target/riscv: Enable xtheadcrc for zhijiang | 2025-07-30 | LIU Zhiwei | xtheadcrc | ❌ 否 |
| 379 | `2f67a9c119` | target/riscv: Support xtheadcrc | 2025-07-30 | LIU Zhiwei | xtheadcrc | ❌ 否 |
| 380 | `3ce25219f9` | target/riscv: Fix zp process for mdequant* | 2025-07-29 | LIU Zhiwei | Matrix | ✅ 是 |
| 381 | `af57e66fec` | target/riscv: Set default nan mode for matrix | 2025-07-28 | LIU Zhiwei | Matrix | ❌ 否 |
| 382 | `0f099741af` | target/riscv: Always init oprd_b to quiet compiler warning | 2025-07-28 | LIU Zhiwei | target/riscv | ❌ 否 |
| 383 | `b3034c4636` | target/riscv: Fix senq misalign exception | 2025-07-26 | LIU Zhiwei | XuanTie CSR | ✅ 是 |
| 384 | `bd1bf05121` | target/riscv: Fix mfcvt*.e5.s | 2025-07-26 | LIU Zhiwei | target/riscv | ✅ 是 |
| 385 | `d6a88e224e` | target/riscv: Fix mfcvth.d.s encoding | 2025-07-26 | LIU Zhiwei | target/riscv | ✅ 是 |
| 386 | `80bb43d600` | target/riscv: Support bf16 for pointwise | 2025-07-26 | LIU Zhiwei | target/riscv | ❌ 否 |
| 387 | `c1277a6955` | target/riscv: target/riscv: Support mc.i and mc for fused pointwise | 2025-07-25 | LIU Zhiwei | target/riscv | ❌ 否 |
| 388 | `d6ab4c43d3` | target/riscv: target/riscv: Support mc.i and mc for fp pointwise | 2025-07-25 | LIU Zhiwei | target/riscv | ❌ 否 |
| 389 | `370b0760b1` | target/riscv: Support mv for float pointwise | 2025-07-25 | LIU Zhiwei | target/riscv | ❌ 否 |
| 390 | `6f981cbe06` | target/riscv: Support mc.i and mc for int pointwise | 2025-07-25 | LIU Zhiwei | target/riscv | ❌ 否 |
| 391 | `7ed1ee08e5` | target/riscv: Support mfma | 2025-07-24 | LIU Zhiwei | target/riscv | ❌ 否 |
| 392 | `c78436383d` | target/riscv: Support mv type matrix integer pointwise instrcutions | 2025-07-24 | LIU Zhiwei | Matrix | ❌ 否 |
| 393 | `fb6ba987ea` | hw/riscv: Use 20MHZ for smartl and smartm | 2025-07-23 | LIU Zhiwei | Board | ❌ 否 |
| 394 | `b5d9e56735` | hw/riscv/riscv-iommu: Add iommu Svnapot support | 2025-07-23 | yfy_lazy | riscv-iommu | ❌ 否 |
| 395 | `cbe2fbd0e2` | target/riscv: Support dequant operations for matrix | 2025-07-21 | LIU Zhiwei | Matrix | ❌ 否 |
| 396 | `5f8b1c10c3` | fpu: Fix e5m2 for out of range in no sat mode | 2025-07-11 | LIU Zhiwei | xtheadvfofp | ✅ 是 |
| 397 | `d881466e26` | target/riscv: Pass utn to fpstatus as saturate flag | 2025-07-11 | LIU Zhiwei | target/riscv | ❌ 否 |
| 398 | `128aa3d70d` | target/riscv: Use ocp fpu interface | 2025-07-11 | LIU Zhiwei | fpu | ❌ 否 |
| 399 | `da1e574001` | fpu: Use switch for ocp process | 2025-07-09 | LIU Zhiwei | fpu | ❌ 否 |
| 400 | `225236b1ac` | fpu: Use enum for ocp format | 2025-07-09 | LIU Zhiwei | fpu | ❌ 否 |
| 401 | `2f2f2cdd9d` | fpu: Don't use overflow_norm for ocp format | 2025-07-09 | LIU Zhiwei | fpu | ❌ 否 |
| 402 | `eb98840bd5` | target/riscv: iommu: Add G-stage table In Process Context | 2025-07-03 | Guo Ren | riscv-iommu | ❌ 否 |
| 403 | `0c767c18f9` | hw/riscv: Enable non-leaf pte and address range invalidation | 2025-07-06 | LIU Zhiwei | hw/riscv | ❌ 否 |
| 404 | `56d4ebec80` | target/riscv: Fix compile warning | 2025-07-06 | LIU Zhiwei | target/riscv | ✅ 是 |
| 405 | `5ff560879d` | target/riscv: Fix mfence.spa encode | 2025-07-04 | LIU Zhiwei | target/riscv | ✅ 是 |
| 406 | `52ea27a7a3` | target/riscv: Enable  M mode when debug | 2025-07-04 | LIU Zhiwei | target/riscv | ❌ 否 |
| 407 | `31528b7b9b` | hw/riscv: Support iommu address range invalidation | 2025-07-04 | LIU Zhiwei | riscv-iommu | ❌ 否 |
| 408 | `dc8545f475` | fpu: Fix e4m3 uncanon normal | 2025-07-03 | LIU Zhiwei | xtheadvfofp | ✅ 是 |
| 409 | `44e4baf2a5` | target/riscv: Use custom trans for xtheadvfofp8min | 2025-07-02 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 410 | `9321a53936` | target/riscv: Update xtheadvfofp8min encode | 2025-07-01 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 411 | `1fe9a795bd` | target/riscv: Support th.vgmulxor.vv | 2025-06-26 | LIU Zhiwei | xtheadvcrypto | ❌ 否 |
| 412 | `1da49adea8` | tests/tcg: Fix matrix test case | 2025-06-26 | LIU Zhiwei | Matrix | ✅ 是 |
| 413 | `22af3a68d8` | target/riscv: Suppport th.mfmacc.s.mxe2m1.ue4m3 | 2025-06-26 | LIU Zhiwei | Matrix | ❌ 否 |
| 414 | `5db19b9c4e` | target/riscv: Fix mx colidx setting | 2025-06-25 | LIU Zhiwei | target/riscv | ✅ 是 |
| 415 | `39d9f11099` | target/riscv: Fix set_elem_p | 2025-06-25 | LIU Zhiwei | target/riscv | ✅ 是 |
| 416 | `2e57229774` | tests/tcg: Add tcg case for xtheadvfofp8min | 2025-06-25 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 417 | `00c6a9011b` | target/riscv: Fix vfncvt.f.f.q | 2025-06-25 | LIU Zhiwei | xtheadvfcvt | ✅ 是 |
| 418 | `6c6f2b3f5e` | fpu: Fix e8m0 uncanon | 2025-06-25 | LIU Zhiwei | xtheadvfofp | ✅ 是 |
| 419 | `f1090bb7db` | fpu: Fix e5m2 and e4m3 uncanon | 2025-06-25 | LIU Zhiwei | xtheadvfofp | ✅ 是 |
| 420 | `f626a55826` | target/riscv: Set vlen 256 for zhijiang | 2025-06-25 | LIU Zhiwei | zhijiang | ❌ 否 |
| 421 | `2120f12f19` | fpu: Process e5m2 uncanon for NaN | 2025-06-25 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 422 | `51e4464c17` | fpu: Fix em80/fp6/fp4 normal uncanon | 2025-06-24 | LIU Zhiwei | fpu | ✅ 是 |
| 423 | `fca8b34a7a` | fpu: Fix fp8 normal uncanon | 2025-06-24 | LIU Zhiwei | fpu | ✅ 是 |
| 424 | `ecc8c6c6af` | fpu: Fix e4m3 normal uncanon | 2025-06-24 | LIU Zhiwei | xtheadvfofp | ✅ 是 |
| 425 | `f2750ac247` | target/riscv: Enable xtheadvfofp8min for zhijiang | 2025-06-24 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 426 | `6e1cc4c2b8` | target/riscv: Support xtheadvfofp8min | 2025-06-24 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 427 | `3fd8e3ca37` | target/riscv: Enable xtheadvfoe8m0min for zhijiang | 2025-06-23 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 428 | `eeaedcbc97` | target/riscv: Support xtheadvfoe8m0min | 2025-06-23 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 429 | `35e8c6c2a1` | fpu: Support float8e0 convert to bfloat16 | 2025-06-23 | LIU Zhiwei | BF16 | ❌ 否 |
| 430 | `d8beb4746a` | fpu: Support convert to e8m0 | 2025-06-20 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 431 | `89dbe7bb79` | target/riscv: Fix mfcvth/l.h.e4 encoding | 2025-06-19 | LIU Zhiwei | target/riscv | ✅ 是 |
| 432 | `6752cce2e5` | target/riscv: Enable xtheadvfofp4min for zhijiang | 2025-06-19 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 433 | `7f47eaa6c2` | target/riscv: Support xtheadvfofp4min | 2025-06-18 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 434 | `1158d83d48` | target/riscv: Enable xtheadvfofp6min for zhijiang | 2025-06-18 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 435 | `a06d778e0c` | target/riscv: Support xtheadvfofp6min | 2025-06-18 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 436 | `c867d1ee39` | fpu: Support ocp fp6 | 2025-06-17 | LIU Zhiwei | fpu | ❌ 否 |
| 437 | `ce362794c7` | hw/riscv: Set zhijiang as valid cpu for rvsp-ref | 2025-06-16 | LIU Zhiwei | zhijiang | ❌ 否 |
| 438 | `3c085c1268` | target/riscv: Support altfmt field in vtype | 2025-06-16 | LIU Zhiwei | target/riscv | ❌ 否 |
| 439 | `dfa6e7e621` | target/riscv: Fix xtheadvsfu build error | 2025-06-16 | LIU Zhiwei | SFU/SFA | ✅ 是 |
| 440 | `7ded00cbb9` | target/riscv: Support xtheadsfu in zhijiang | 2025-06-15 | LIU Zhiwei | SFU/SFA | ❌ 否 |
| 441 | `f263bd56fc` | target/riscv: Add jvt property for e901 | 2025-06-13 | LIU Zhiwei | E90x | ❌ 否 |
| 442 | `219c9df0e3` | target/riscv: Default mtvec set to 0x43 | 2025-06-11 | LIU Zhiwei | target/riscv | ❌ 否 |
| 443 | `30d868424e` | target/riscv: Enable svvptc for zhijiang | 2025-06-10 | LIU Zhiwei | zhijiang | ❌ 否 ⬅️ Revert |
| 444 | `36568e597b` | target: riscv: Add Svvptc extension support | 2024-08-28 | Alexandre Ghiti | 其他 | ❌ 否 ⬅️ Revert |
| 445 | `42c7c9045b` | target/riscv: Disable zihpm for e901 | 2025-06-09 | LIU Zhiwei | E90x | ❌ 否 |
| 446 | `b9e37b0399` | hw/riscv: Alway provide timer for env | 2025-06-09 | LIU Zhiwei | hw/riscv | ❌ 否 |
| 447 | `bf23050131` | target/riscv: Fix mtvec/mtvt for other cpus | 2025-06-09 | LIU Zhiwei | target/riscv | ✅ 是 |
| 448 | `d15c56572d` | cskysim: Add smartm xml | 2025-06-09 | LIU Zhiwei | cskysim | ❌ 否 |
| 449 | `22dc94c62b` | target/riscv: Set default mtvt and mtvec | 2025-06-09 | LIU Zhiwei | target/riscv | ❌ 否 |
| 450 | `9cfeb099cf` | hw/riscv: Add smartm support | 2025-06-09 | LIU Zhiwei | hw/riscv | ❌ 否 |
| 451 | `17e4e12f15` | target/riscv: Fix mtvt/mtvec prop not exist error | 2025-06-06 | LIU Zhiwei | target/riscv | ✅ 是 |
| 452 | `1114dd53ec` | docs: Support e901 | 2025-06-06 | LIU Zhiwei | docs | ❌ 否 |
| 453 | `d0d7815aa8` | target/riscv: Fix mtvec/mtvt setting for e901 rename | 2025-06-06 | LIU Zhiwei | E90x | ✅ 是 |
| 454 | `966ad37d82` | target/riscv: Rename e901 | 2025-06-05 | LIU Zhiwei | E90x | ❌ 否 |
| 455 | `8033e03582` | target/riscv: Fix smmpt64 | 2025-06-03 | LIU Zhiwei | target/riscv | ✅ 是 |
| 456 | `9aff9efbfd` | target/riscv: Fix a typo | 2025-06-03 | LIU Zhiwei | Misc | ✅ 是 |
| 457 | `96fc089834` | target/riscv: Fix a coverity false warning | 2025-06-02 | LIU Zhiwei | target/riscv | ✅ 是 |
| 458 | `739825d9d8` | target/riscv: Fix froundnx.h | 2025-06-02 | LIU Zhiwei | target/riscv | ✅ 是 |
| 459 | `550e4cb809` | target/riscv: Fix aia logically dead code by coverity | 2025-06-02 | LIU Zhiwei | target/riscv | ✅ 是 |
| 460 | `0ffc7a066d` | target/riscv: Fix logically dead code by coverity | 2025-06-02 | LIU Zhiwei | target/riscv | ✅ 是 |
| 461 | `18edf9567d` | hw/riscv: Fix resource leak by coverity | 2025-06-02 | LIU Zhiwei | hw/riscv | ✅ 是 |
| 462 | `8994983c33` | hw/dummyh: Fix resource leak by coverity | 2025-06-02 | LIU Zhiwei | 其他 | ✅ 是 |
| 463 | `c5e3b7c07e` | hw/riscv: Fix resouce leak by coverity | 2025-06-02 | LIU Zhiwei | hw/riscv | ✅ 是 |
| 464 | `4612f242e0` | target/riscv: Support coprocessor for zhijiang | 2025-06-02 | LIU Zhiwei | zhijiang | ❌ 否 |
| 465 | `2f38398a67` | target/riscv: Fix vqdotsu/us | 2025-06-02 | LIU Zhiwei | target/riscv | ✅ 是 |
| 466 | `729a858e46` | target/riscv: Fix smcdeleg bugs | 2025-05-31 | LIU Zhiwei | Smcdeleg/Ssccfg | ✅ 是 ⬅️ Revert |
| 467 | `d7605534e4` | target/riscv: Use mew*b check for xtheadfp4 and xtheadint4 | 2025-05-31 | LIU Zhiwei | target/riscv | ❌ 否 |
| 468 | `28e6bc913c` | target/riscv: Use v0.5 instruction names for pw | 2025-05-31 | LIU Zhiwei | target/riscv | ❌ 否 |
| 469 | `44dd059722` | target/riscv: Use mew*b subextensions for check | 2025-05-31 | LIU Zhiwei | target/riscv | ❌ 否 |
| 470 | `e660242d72` | target/riscv: Add point wise width subextension | 2025-05-31 | LIU Zhiwei | target/riscv | ❌ 否 |
| 471 | `99ec7db58e` | target/riscv: Fix napot check when N bit is zero | 2025-05-30 | LIU Zhiwei | target/riscv | ✅ 是 |
| 472 | `4efbf4d719` | target/riscv: Fix style check error for mpt | 2025-05-30 | LIU Zhiwei | target/riscv | ✅ 是 |
| 473 | `8007595019` | target/riscv: Fix pasid update error | 2025-05-29 | yfy_lazy | target/riscv | ✅ 是 |
| 474 | `a4bad99fff` | target/riscv: Fix an warning | 2025-05-29 | LIU Zhiwei | target/riscv | ✅ 是 |
| 475 | `5b81863303` | target/riscv: Support smmptnlnapot | 2025-05-28 | LIU Zhiwei | target/riscv | ❌ 否 |
| 476 | `ee359164d3` | target/riscv: Update mmpt CSR to v0.3.4 | 2025-05-28 | LIU Zhiwei | CSR | ❌ 否 |
| 477 | `2dfd53b499` | target/riscv: Update mpt lookup to v0.3.4 | 2025-05-28 | LIU Zhiwei | target/riscv | ❌ 否 |
| 478 | `ae5b23ff81` | target/riscv: Save opcode for wrs.nto as it may be illegal | 2025-05-26 | LIU Zhiwei | target/riscv | ❌ 否 |
| 479 | `3ab07c98dd` | cskysim: Add e901 xml | 2025-05-23 | LIU Zhiwei | cskysim | ❌ 否 |
| 480 | `4203c3e648` | target/riscv: Support mtvt and mtvec for e901mini | 2025-05-23 | LIU Zhiwei | E90x | ❌ 否 |
| 481 | `25e509592b` | target/riscv: Zce depends on at least 1.12 priv spec | 2025-05-22 | LIU Zhiwei | target/riscv | ❌ 否 |
| 482 | `f3de86df73` | target/riscv: Fix ctx->f4f32 flag | 2025-05-22 | LIU Zhiwei | target/riscv | ✅ 是 |
| 483 | `c5f40c82ae` | target/riscv: Fix mfcvth/l.s.h encoding | 2025-05-21 | LIU Zhiwei | target/riscv | ✅ 是 |
| 484 | `77a5f3bbd9` | target/riscv: Fix float-point conversion instructions check | 2025-05-21 | LIU Zhiwei | target/riscv | ✅ 是 |
| 485 | `399570a8b8` | target/riscv: Fix mfmacc.h/bf16.e4/e5 check | 2025-05-21 | LIU Zhiwei | Matrix | ✅ 是 |
| 486 | `200908d070` | cskysim: Add e901/e901mini cpu | 2025-05-14 | LIU Zhiwei | cskysim | ❌ 否 |
| 487 | `57bdf3bce7` | target/riscv: Support e901/e901mini | 2025-05-14 | LIU Zhiwei | E90x | ❌ 否 |
| 488 | `65127ae96b` | target/riscv: Fix mzero when mrowlen is big | 2025-05-13 | LIU Zhiwei | Matrix | ✅ 是 |
| 489 | `99a07d6662` | target/riscv: Support int4 to float8 | 2025-05-13 | LIU Zhiwei | target/riscv | ❌ 否 |
| 490 | `5315750afe` | target/riscv: Fix mscvtl check | 2025-05-13 | LIU Zhiwei | Matrix | ✅ 是 |
| 491 | `780848d057` | target/riscv: Add support for A8W4 | 2025-05-12 | LIU Zhiwei | Matrix | ❌ 否 |
| 492 | `891c95bf8d` | target/riscv: Fix regnum check for u/senq | 2025-04-28 | LIU Zhiwei | XuanTie CSR | ✅ 是 |
| 493 | `68b664c345` | target/riscv: Support suenq for uenq | 2025-04-28 | LIU Zhiwei | XuanTie CSR | ❌ 否 |
| 494 | `cbf2eda4a8` | target/riscv: Support suenq CSR | 2025-04-28 | LIU Zhiwei | XuanTie CSR | ❌ 否 |
| 495 | `2367d9f56b` | targe/riscv: Add inital support xtheadaioe | 2025-04-28 | LIU Zhiwei | xtheadaioe | ❌ 否 |
| 496 | `3b4189c2e8` | target/riscv: Fix zhijiang xmisa | 2025-04-27 | LIU Zhiwei | Matrix | ✅ 是 |
| 497 | `51c0b49191` | target/riscv: Fix xmisa encode | 2025-04-27 | LIU Zhiwei | Matrix | ✅ 是 |
| 498 | `5373c3720e` | target/riscv: Support clean persist check bit | 2025-04-27 | LIU Zhiwei | target/riscv | ❌ 否 |
| 499 | `c446579790` | target/riscv: Add support zvqdotq | 2025-04-25 | LIU Zhiwei | zvqdotq | ❌ 否 |
| 500 | `a2996103f7` | tests/tcg: Add a mx format test for xt matrix | 2025-04-21 | LIU Zhiwei | Matrix | ❌ 否 |
| 501 | `17c7411820` | target/riscv: Always use mxa to calculate total elements | 2025-04-21 | LIU Zhiwei | target/riscv | ❌ 否 |
| 502 | `f949b46e26` | fpu: Fix an abort for e8m0 | 2025-04-18 | LIU Zhiwei | xtheadvfofp | ✅ 是 |
| 503 | `0751f1eec6` | target/riscv: Add mxf4/mxf8/mxf8f4 for zhijiang | 2025-04-18 | LIU Zhiwei | Matrix | ❌ 否 |
| 504 | `ca9e274b40` | target/riscv: Fix l_k2_blocksize | 2025-04-18 | LIU Zhiwei | target/riscv | ✅ 是 |
| 505 | `83bcb30f5e` | target/riscv: Support mxa/b with different blksize | 2025-04-18 | LIU Zhiwei | target/riscv | ❌ 否 |
| 506 | `b159bde29a` | target/riscv: Support mxf8mxf4 | 2025-04-17 | LIU Zhiwei | Matrix | ❌ 否 |
| 507 | `cec32ecb69` | target/riscv: Fix mfmacc.s.e2m1 | 2025-04-17 | LIU Zhiwei | Matrix | ✅ 是 |
| 508 | `1d7493be46` | target/riscv: Support mxfp4 and mxfp8 | 2025-04-16 | LIU Zhiwei | Matrix | ❌ 否 |
| 509 | `ca9baeee38` | target/riscv: Matrix config for mx | 2025-04-16 | LIU Zhiwei | Matrix | ❌ 否 |
| 510 | `d2a4703ec8` | target/riscv: Fix mrowlen to 512 for zhijiang | 2025-04-15 | LIU Zhiwei | Matrix | ✅ 是 |
| 511 | `69629684aa` | target/riscv: Fix matrix a16w4 check | 2025-04-15 | LIU Zhiwei | Matrix | ✅ 是 |
| 512 | `66e56dac4d` | target/riscv: Support xmx*desc CSR | 2025-04-15 | LIU Zhiwei | CSR | ❌ 否 |
| 513 | `43705c33f4` | fpu: Support float8e0 to float32 | 2025-04-15 | LIU Zhiwei | fpu | ❌ 否 |
| 514 | `f4d9c54441` | target/riscv: Remove c908x rv32 support | 2025-04-15 | LIU Zhiwei | C908 | ❌ 否 |
| 515 | `2e15689408` | cskysim: Fix c908v xml default cpu | 2025-04-15 | LIU Zhiwei | cskysim | ✅ 是 |
| 516 | `97a7ad3461` | fpu: Use float4e2_round_pack_canonical for conversion to float4e2 | 2025-04-13 | LIU Zhiwei | fpu | ❌ 否 |
| 517 | `3d014f89f4` | fpu: Add ocp_e2m1 field to float4e2 params | 2025-04-13 | LIU Zhiwei | fpu | ❌ 否 |
| 518 | `83efd80a06` | fpu: Fix nan_no1s_as_normal in canonicalize | 2025-04-11 | LIU Zhiwei | fpu | ✅ 是 |
| 519 | `85800044f5` | fpu: Support ocp_e4m3 in FloatFmt | 2025-04-11 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 520 | `b4f37544ce` | fpu: Remove special process for float8e4 in uncanon_normal | 2025-04-10 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 521 | `55b7515055` | fpu: Add nan_no1s_as_normal for float8e4 | 2025-04-10 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 522 | `8539856161` | csky-trace: Remove cpustate arg form csky-trace parse | 2025-04-10 | LIU Zhiwei | csky-trace | ❌ 否 |
| 523 | `9c4dcc8ee5` | target/csky: Move parts csky-trace parse to cpu | 2025-04-10 | LIU Zhiwei | target/csky | ❌ 否 |
| 524 | `be1fc76e05` | csky-trace: Move parts of csky_trace parse to cpu | 2025-04-10 | LIU Zhiwei | csky-trace | ❌ 否 |
| 525 | `d9bce8d41e` | csky-trace: Move csky-trace parse parts in cpu to csky-trace | 2025-04-10 | LIU Zhiwei | csky-trace | ❌ 否 |
| 526 | `b24366728f` | target/riscv: Parse csky-extend/cpu-prop opts for each cpu | 2025-04-10 | LIU Zhiwei | target/riscv | ❌ 否 |
| 527 | `9bc9852fc4` | target/riscv: Fix matrix store address calculation | 2025-04-09 | LIU Zhiwei | Matrix | ✅ 是 |
| 528 | `30b488b8a6` | target/riscv: Fix ssdtso for zhijiang | 2025-04-08 | LIU Zhiwei | ssdtso | ✅ 是 |
| 529 | `390a93193f` | fpu: Use positive max for NaN to fp4 | 2025-04-08 | LIU Zhiwei | fpu | ❌ 否 |
| 530 | `ed0ec1956b` | target/riscv: Support fp8/fp4 conversion | 2025-04-08 | LIU Zhiwei | target/riscv | ❌ 否 |
| 531 | `209092bb5e` | fpu: Support float8/float4 convert | 2025-04-08 | LIU Zhiwei | fpu | ❌ 否 |
| 532 | `f32c18e2ab` | target/riscv: Fix fp4 hp check | 2025-04-08 | LIU Zhiwei | target/riscv | ✅ 是 |
| 533 | `c760c6fd19` | target/riscv: Support mfmacc.s.e2m1 | 2025-04-08 | LIU Zhiwei | Matrix | ❌ 否 |
| 534 | `db88498c78` | fpu: Support float4e2_to_float32 | 2025-04-08 | LIU Zhiwei | fpu | ❌ 否 |
| 535 | `c183a76959` | target/riscv: Support fp half-precison extension | 2025-04-08 | LIU Zhiwei | target/riscv | ❌ 否 |
| 536 | `418b27cc98` | fpu: Softfpu support basic float4e2 | 2025-04-08 | LIU Zhiwei | fpu | ❌ 否 |
| 537 | `b5d1852dc8` | target/riscv: Fix xmisa encode | 2025-04-08 | LIU Zhiwei | Matrix | ✅ 是 |
| 538 | `c6a937a60d` | target/riscv: Support bf20f32 sub-extension | 2025-04-08 | LIU Zhiwei | Matrix | ❌ 否 |
| 539 | `c9a8861d69` | target/riscv: Support matrix prefetch load | 2025-04-07 | LIU Zhiwei | Matrix | ❌ 否 |
| 540 | `ef46db0643` | target/riscv: Support streaming matrix transposed store | 2025-04-07 | LIU Zhiwei | Matrix | ❌ 否 |
| 541 | `0326489ea8` | target/riscv: Support streaming matrix transposed load | 2025-04-07 | LIU Zhiwei | Matrix | ❌ 否 |
| 542 | `7cda10978a` | target/riscv: Fix memory trace for matrix load/store | 2025-04-07 | LIU Zhiwei | Matrix | ✅ 是 |
| 543 | `4c0ff39a38` | target/riscv: Fix probe for transposed load/store | 2025-04-07 | LIU Zhiwei | Matrix | ✅ 是 |
| 544 | `da4c5ccdff` | target/riscv: Support matrix transposed store | 2025-04-07 | LIU Zhiwei | Matrix | ❌ 否 |
| 545 | `a7d06d873d` | target/riscv: Support transposed matrix load | 2025-04-07 | LIU Zhiwei | Matrix | ❌ 否 |
| 546 | `aeb6a8ee2d` | target/riscv: Use LSB 32 bits for xmisa firstly | 2025-04-07 | LIU Zhiwei | Matrix | ❌ 否 |
| 547 | `c3e91535f3` | target/riscv: Fix mfmacc.h.hb check method | 2025-04-07 | LIU Zhiwei | Matrix | ✅ 是 |
| 548 | `28ba1ecfa2` | target/riscv: Remove i16i64 define from xmisa | 2025-04-07 | LIU Zhiwei | Matrix | ❌ 否 |
| 549 | `e16fb0a767` | target/riscv: Remove tbflags about matrix subextensions | 2025-04-06 | LIU Zhiwei | Matrix | ❌ 否 |
| 550 | `27cee54798` | target/riscv: Update xmisa according to v0.5 spec | 2025-04-03 | LIU Zhiwei | Matrix | ❌ 否 |
| 551 | `141ba858bb` | target/riscv: Support ssdtso | 2025-04-03 | LIU Zhiwei | ssdtso | ❌ 否 |
| 552 | `de96799c15` | target/riscv: Fix vaba/vwaba | 2025-04-02 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 553 | `99c2a14f91` | target/riscv: Fix vabd/vwabd | 2025-04-02 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 554 | `e9c13b120d` | target/riscv: Add independent CSR for fields in mcsr | 2025-04-01 | LIU Zhiwei | CSR | ❌ 否 |
| 555 | `0758a1bf41` | target/riscv: Saturate result for interger matrix multiplication | 2025-04-01 | LIU Zhiwei | Matrix | ❌ 否 |
| 556 | `3af1356cbd` | target/riscv: Rename mmacc_s_bp to mmacc_w_bp | 2025-04-01 | LIU Zhiwei | Matrix | ❌ 否 |
| 557 | `fa7d84cd15` | target/riscv: Rename pmmaqa.b to mmacc.w.p | 2025-04-01 | LIU Zhiwei | Matrix | ❌ 否 |
| 558 | `076f32ff9d` | target/riscv: Add msaten field for xmcsr | 2025-04-01 | LIU Zhiwei | Matrix | ❌ 否 |
| 559 | `ef8758b362` | target/riscv: Rename mmaqa.b(h) to mmacc.w.b(d.h) | 2025-04-01 | LIU Zhiwei | Matrix | ❌ 否 |
| 560 | `ff0db4abee` | fpu: Update conversion interfaces to e4m3 | 2025-03-31 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 561 | `0d9e6890ca` | target/riscv: Add matrix v0.5 CSRs | 2025-03-31 | LIU Zhiwei | Matrix | ❌ 否 |
| 562 | `81d7970f7f` | target/riscv: pmp: remove redundant check in pmp_is_locked | 2025-03-13 | Loïc Lefort | PMP | ❌ 否 |
| 563 | `34d5aa3a12` | target/riscv: pmp: exit csr writes early if value was not changed | 2025-03-13 | Loïc Lefort | PMP | ❌ 否 |
| 564 | `105304c841` | target/riscv: pmp: fix checks on writes to pmpcfg in Smepmp MML mode | 2025-03-13 | Loïc Lefort | PMP | ✅ 是 |
| 565 | `93c2537d63` | target/riscv: pmp: move Smepmp operation conversion into a function | 2025-03-13 | Loïc Lefort | PMP | ❌ 否 |
| 566 | `d40a760257` | target/riscv: pmp: don't allow RLB to bypass rule privileges | 2025-03-13 | Loïc Lefort | PMP | ❌ 否 |
| 567 | `0416ffd7aa` | target/riscv: Don't update mip in CLIC mode | 2025-03-27 | LIU Zhiwei | CLIC | ❌ 否 |
| 568 | `d269df1855` | Merge branch b033459396a59ff661f6240d00816ee0211783d8 into xuantie-v9.0-dev Title: hw/intc: Fix ip write error in CLIC | 2025-03-27 | lzw194868 | CLIC | ✅ 是 |
| 569 | `b033459396` | hw/intc: Fix ip write error in CLIC | 2025-03-27 | LIU Zhiwei | CLIC | ✅ 是 |
| 570 | `97b07cfb0e` | docs: Update cpu names | 2025-03-27 | LIU Zhiwei | docs | ❌ 否 |
| 571 | `3fe72d7e28` | target/riscv: Fix cop subsets in cpu | 2025-03-27 | LIU Zhiwei | CPU | ✅ 是 |
| 572 | `9f17e3079d` | cskysim: Support -cp-xt cpus | 2025-03-26 | LIU Zhiwei | cskysim | ❌ 否 |
| 573 | `aa4a88d016` | target/riscv: Support sscofpmf for c908/r908/c908x/c908v2 | 2025-03-26 | LIU Zhiwei | R908A | ❌ 否 |
| 574 | `08b4c6f0a2` | target/riscv: Fix c908x-cp isa model | 2025-03-26 | LIU Zhiwei | C908 | ✅ 是 |
| 575 | `17e25322cb` | target/riscv: Support r908/c908/c910/c920-cp-xt | 2025-03-26 | LIU Zhiwei | R908A | ❌ 否 |
| 576 | `3ccf9822cb` | target/riscv: Fix vmv.s.x tail process | 2025-03-25 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 577 | `57977e7e08` | target/riscv: Fix vmv.s.x | 2025-03-22 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 578 | `0b3fcbb547` | target/riscv: Fix vabau.vi and vabdu.vi | 2025-03-22 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 579 | `dcaad37756` | target/riscv: Fix vmacc54l/h.vs tail process | 2025-03-22 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 580 | `23b9604220` | target/riscv: Fix vabau.vi and vabdu.vi unsigned imm | 2025-03-22 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 581 | `fddefd7039` | cskysim: Add cpu c908x-cp-xt | 2025-03-19 | LIU Zhiwei | cskysim | ❌ 否 |
| 582 | `e7102f895f` | target/riscv: Fix coprocessor encoding | 2025-03-19 | LIU Zhiwei | CPU | ✅ 是 |
| 583 | `84fc64a33c` | target/riscv: Update mcmov* encoding | 2025-03-19 | LIU Zhiwei | CPU | ❌ 否 |
| 584 | `099a4af2d5` | target/riscv: Fix vmv.s.x tail process | 2025-03-18 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 585 | `d7a23fc707` | target/riscv: Set tail and masked elements to 1 | 2025-03-18 | LIU Zhiwei | target/riscv | ❌ 否 |
| 586 | `9a9dfa4978` | target/riscv: Fix vabau* encoding | 2025-03-18 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 587 | `7c66e09c0f` | target/riscv: Fix uninitialize warning | 2025-03-18 | LIU Zhiwei | Misc | ✅ 是 |
| 588 | `ca69ed7f43` | target/riscv: Fix allow vs1 overide vd for vile | 2025-03-07 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 589 | `267102ecd4` | target/riscv: Fix frac mask setting when not overflow | 2025-03-06 | LIU Zhiwei | target/riscv | ✅ 是 |
| 590 | `0a947c3e01` | target/riscv: Fix zero result process in th.vfredsum* | 2025-03-06 | LIU Zhiwei | xtheadvfreduction | ✅ 是 |
| 591 | `35d0fc88c7` | target/riscv: Fix xt_round_* | 2025-03-06 | LIU Zhiwei | target/riscv | ✅ 是 |
| 592 | `de5120ff02` | target/riscv: Refactor convert from to float16 to float8e4 | 2025-03-04 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 593 | `cdad4e7825` | target/riscv: Fix fncvt.e4.h when greater than e4m3 max | 2025-02-28 | LIU Zhiwei | xtheadvfofp | ✅ 是 |
| 594 | `fb39d9663c` | target/riscv: Fix th.vbfredsum* using xt_canon_bf16 | 2025-02-28 | LIU Zhiwei | xtheadvfreduction | ✅ 是 |
| 595 | `a520fa9850` | target/riscv: Remove lpad code in ctr | 2025-02-25 | LIU Zhiwei | Zicfilp | ❌ 否 ⬅️ Revert |
| 596 | `9026b9c86f` | target/riscv: Fix lpad enable detection | 2025-02-25 | LIU Zhiwei | Zicfilp | ✅ 是 ⬅️ Revert |
| 597 | `ef0b536d26` | target/riscv: Fix cfi mmu index setting | 2025-02-25 | LIU Zhiwei | Zicfiss | ✅ 是 ⬅️ Revert |
| 598 | `4aa5d83eb1` | target/riscv: Fix mtnfastmba predicate | 2025-02-22 | LIU Zhiwei | xtheadfastm | ✅ 是 |
| 599 | `944babe7ce` | target/riscv: Fix senvcfg read | 2025-02-22 | LIU Zhiwei | Privilege | ✅ 是 |
| 600 | `a06d07b00c` | target/riscv: Fix zacas implementation | 2025-02-21 | LIU Zhiwei | Zacas | ✅ 是 |
| 601 | `3713894f2f` | target/riscv: Fix vmacc54* | 2025-02-19 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 602 | `c3af699549` | target/riscv: Fix vile/vilo | 2025-02-19 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 603 | `4d1a811f04` | target/riscv: Fix vabdu* encode | 2025-02-19 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 604 | `61c0965dbc` | target/riscv: Add bfloat16 support for c908v2 | 2025-02-18 | LIU Zhiwei | BF16 | ❌ 否 |
| 605 | `303824212e` | target/riscv: Fix c908v2 program model | 2025-02-17 | LIU Zhiwei | C908 | ✅ 是 |
| 606 | `60869eac70` | target/riscv: Fix vmv.r* check | 2025-02-17 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 607 | `b3d96d725a` | target/riscv: Add correct marchid for zhijiang | 2025-02-14 | LIU Zhiwei | zhijiang | ❌ 否 |
| 608 | `37fb353011` | cskysim: Support -vlen option | 2025-02-14 | LIU Zhiwei | cskysim | ❌ 否 |
| 609 | `abad7ca9f5` | target/riscv: Don't call gvec if maxsz > 2048 | 2025-02-14 | LIU Zhiwei | RVV | ❌ 否 |
| 610 | `200bdfcefe` | target/riscv: Set c908x default vlen to 1024 | 2025-02-14 | LIU Zhiwei | C908 | ❌ 否 |
| 611 | `56ca4fcf6d` | target/riscv: Fix gdb cannot read CSRs | 2025-02-12 | LIU Zhiwei | GDB | ✅ 是 |
| 612 | `79b0d2187a` | target/riscv: Fix e5 inf encoding | 2025-02-12 | LIU Zhiwei | target/riscv | ✅ 是 |
| 613 | `3b86e58aec` | target/riscv: Fix e5m2 snan check | 2025-02-12 | LIU Zhiwei | xtheadvfofp | ✅ 是 |
| 614 | `fe0edfa81e` | target/riscv: Add vlmax check for xtheadvfreduction. | 2025-02-12 | LIU Zhiwei | xtheadvfreduction | ❌ 否 |
| 615 | `7d66174277` | cskysim: Support c908v2 | 2025-02-12 | LIU Zhiwei | cskysim | ❌ 否 |
| 616 | `3775bb3636` | target/riscv: Add c908_v2/c908cp-v2 support | 2025-02-12 | LIU Zhiwei | C908 | ❌ 否 |
| 617 | `eb930e54e5` | target/riscv: Fix fastm for smp | 2025-02-11 | LIU Zhiwei | xtheadfastm | ✅ 是 |
| 618 | `6ea3c36d91` | target/riscv: Fix fresum.64.w calculation | 2025-02-11 | LIU Zhiwei | target/riscv | ✅ 是 |
| 619 | `5f965308b7` | target/riscv: Fix lmul check for vfreduction | 2025-02-11 | LIU Zhiwei | target/riscv | ✅ 是 |
| 620 | `f4b1d9895a` | riscv/dsa: Add system mode option and fix gpr api | 2025-02-11 | HTT | DSA | ✅ 是 |
| 621 | `be3d0af493` | hw/riscv: Remove asp support | 2025-02-11 | LIU Zhiwei | hw/riscv | ❌ 否 |
| 622 | `fd8106e44c` | cskysim: Remove win32 support | 2025-02-11 | LIU Zhiwei | cskysim | ❌ 否 |
| 623 | `f29ba00fa5` | build: DSA supports require gmodule | 2025-02-10 | LIU Zhiwei | DSA | ❌ 否 |
| 624 | `ab47f081c3` | target/riscv: Add sfu source code | 2025-02-10 | LIU Zhiwei | SFU/SFA | ❌ 否 |
| 625 | `f0d00ca7bb` | plugins/bbv: Make it compilable under 32-bit host | 2025-02-10 | LIU Zhiwei | plugins | ❌ 否 |
| 626 | `16c13773a5` | Merge commit '62859b91016666102a15eba3a436f95c7fdfd7c2' into xuantie-v9.0-dev | 2025-02-07 | LIU Zhiwei | Merge | ❌ 否 |
| 627 | `fd59d5d24a` | target/riscv: Fix fast memory size | 2025-01-26 | LIU Zhiwei | xtheadfastm | ✅ 是 |
| 628 | `6dd2a8ce61` | target/riscv: Set max vlen to 4096 | 2025-01-20 | LIU Zhiwei | CPU | ❌ 否 |
| 629 | `22be861965` | target/riscv: Fix vaba* | 2025-01-20 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 630 | `38e6454b33` | target/riscv: Fix c908x convert instruction check | 2025-01-20 | LIU Zhiwei | C908 | ✅ 是 |
| 631 | `f23e232b22` | target/riscv: Enable zvkgs for zhijiang | 2025-01-18 | LIU Zhiwei | zvkgs/zvbc32e/zbc | ❌ 否 |
| 632 | `1cdd63b9fc` | target/riscv: Fix crc instruction | 2025-01-16 | LIU Zhiwei | xtheadcrc | ✅ 是 |
| 633 | `92245e183f` | target/riscv: Add isa check for Xuantie trubo | 2025-01-16 | LIU Zhiwei | target/riscv | ❌ 否 |
| 634 | `4e8f98e2e3` | target/riscv: Support abdu/abau for Xuantie arith turbo | 2025-01-16 | LIU Zhiwei | xtheadvarith | ❌ 否 |
| 635 | `e1383f42d8` | target/riscv: Support Xuantie clean.persist | 2025-01-16 | LIU Zhiwei | xtheadvarith | ❌ 否 |
| 636 | `8114ec060d` | target/riscv: Support Xuantie vcrcfold | 2025-01-15 | LIU Zhiwei | xtheadvcrypto | ❌ 否 |
| 637 | `51f572c58e` | target/riscv: Support xuantie vgmul | 2025-01-14 | LIU Zhiwei | xtheadvcrypto | ❌ 否 |
| 638 | `62859b9101` | target/riscv: Using LAZY way to load dsa library | 2025-01-14 | HTT | DSA | ❌ 否 |
| 639 | `6c375c7df8` | target/riscv: Fix tail process for vlie/vlio | 2025-01-13 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 640 | `0633bc24ca` | target/riscv: Fix vile/vlio tail process | 2025-01-13 | LIU Zhiwei | xtheadvector | ✅ 是 |
| 641 | `83e8e8881c` | target/riscv: Support Xuantie arith | 2025-01-10 | LIU Zhiwei | xtheadvarith | ❌ 否 |
| 642 | `1fd6827b89` | target/riscv: Support Xuantie coder | 2025-01-10 | LIU Zhiwei | xtheadvcoder | ❌ 否 |
| 643 | `b6ae8b7a39` | target/riscv: Support Xuantie vcrypto extension | 2025-01-07 | LIU Zhiwei | target/riscv | ❌ 否 |
| 644 | `dbd3e83c4d` | hw/riscv/virt-acpi-build.c: Update the HID of RISC-V UART | 2024-04-19 | Sunil V L | hw/riscv | ❌ 否 |
| 645 | `f6cbb7c547` | hw/riscv/virt-acpi-build.c: Add namespace devices for PLIC and APLIC | 2024-03-01 | Sunil V L | Board | ❌ 否 |
| 646 | `ee8ea2db02` | gpex-acpi: Support PCI link devices outside the host bridge | 2024-04-15 | Sunil V L | 其他 | ❌ 否 |
| 647 | `c46d941f56` | target/riscv: Let qemu abort when dsa is wrong | 2025-01-03 | HTT | DSA | ❌ 否 |
| 648 | `54d584efa0` | hw/csky: Fix dummyh timer creation | 2025-01-03 | LIU Zhiwei | 其他 | ✅ 是 |
| 649 | `1ae58724ea` | target/riscv: Fix wfe | 2024-12-30 | LIU Zhiwei | target/riscv | ✅ 是 |
| 650 | `74c3331a31` | target/riscv: Fix denormal_shift may be used uninitialized | 2024-12-30 | LIU Zhiwei | target/riscv | ✅ 是 |
| 651 | `3b1d23c409` | target/riscv: Allow exec wfe in user mode | 2024-12-30 | LIU Zhiwei | target/riscv | ❌ 否 |
| 652 | `f3f0a1a717` | target/riscv: Fix th.vfredsum.dup.32 check | 2024-12-30 | LIU Zhiwei | xtheadvfreduction | ✅ 是 |
| 653 | `0db6e8f872` | target/riscv: Fix check function for th.vfredsum.dup.64 | 2024-12-30 | LIU Zhiwei | xtheadvfreduction | ✅ 是 |
| 654 | `3735e62d0a` | target/riscv: Fix titan CSR name | 2024-12-30 | LIU Zhiwei | XuanTie CSR | ✅ 是 |
| 655 | `5cc8d9ef71` | target/riscv: Fix denormal fp16 and bf16 for fredsum | 2024-12-30 | LIU Zhiwei | xtheadvfreduction | ✅ 是 |
| 656 | `8d955e85ba` | target/riscv: Fix denormal exp_max align | 2024-12-30 | LIU Zhiwei | target/riscv | ✅ 是 |
| 657 | `4d7d911ff1` | target/riscv: Update the missing sfu.h | 2024-12-28 | LIU Zhiwei | SFU/SFA | ❌ 否 |
| 658 | `82ae39aeb0` | target/riscv: Fix th.vfrec.v for inf | 2024-12-28 | LIU Zhiwei | target/riscv | ✅ 是 |
| 659 | `6af51e76e5` | target/riscv: Update sfu cmodel for sigmoid and rcp | 2024-12-28 | LIU Zhiwei | SFU/SFA | ❌ 否 |
| 660 | `a4d885f42a` | target/riscv: Fix vary.dup translation | 2024-12-28 | LIU Zhiwei | xtheadvfreduction | ✅ 是 |
| 661 | `911cb83758` | target/riscv: Fix v*fremin* sat result | 2024-12-28 | LIU Zhiwei | xtheadvfreduction | ✅ 是 |
| 662 | `e12e46c643` | target/riscv: Fix v*fredmax* sat result | 2024-12-28 | LIU Zhiwei | xtheadvfreduction | ✅ 是 |
| 663 | `d0ab4d02be` | target/riscv: fix vfredsum.*.64.* | 2024-12-28 | LIU Zhiwei | xtheadvfreduction | ✅ 是 |
| 664 | `21113f3aff` | target/riscv: Fix vbfredsum.64 | 2024-12-28 | LIU Zhiwei | xtheadvfreduction | ✅ 是 |
| 665 | `8aa2d4ed27` | target/riscv: Fix vbfredsum.32.*.h | 2024-12-28 | LIU Zhiwei | xtheadvfreduction | ✅ 是 |
| 666 | `f5a9a9611f` | target/riscv: Don't overide special result in round | 2024-12-27 | LIU Zhiwei | target/riscv | ❌ 否 |
| 667 | `f68c57e8a5` | target/riscv: Fix vfresum_32_h | 2024-12-27 | LIU Zhiwei | xtheadvfreduction | ✅ 是 |
| 668 | `55a75896dc` | target/riscv: Increse f->exp when fraction overflow after round | 2024-12-27 | LIU Zhiwei | target/riscv | ❌ 否 |
| 669 | `cdaffb9013` | target/riscv: Fix vfredsum_dup_32_w | 2024-12-27 | LIU Zhiwei | xtheadvfreduction | ✅ 是 |
| 670 | `fab410a706` | target/riscv: Fix min/max dest position for xtheadvfreduction | 2024-12-26 | LIU Zhiwei | xtheadvfreduction | ✅ 是 |
| 671 | `c9a85d2943` | target/riscv: Remove mfastmbah define | 2024-12-25 | LIU Zhiwei | xtheadfastm | ❌ 否 |
| 672 | `9fa4fe5efb` | target/riscv: C908x support bfloat16 | 2024-12-25 | LIU Zhiwei | BF16 | ❌ 否 |
| 673 | `86169399b3` | target/riscv: Support xtheadpbmt for c907 rv32 | 2024-12-25 | LIU Zhiwei | xtheadpbmt | ❌ 否 |
| 674 | `07e3c9bff7` | target/riscv: MTT L1 reserved bits must be zero | 2024-12-24 | LIU Zhiwei | Smmtt | ❌ 否 |
| 675 | `988557ef8f` |  target/riscv: MTT L2 reserved bits must be zero | 2024-12-24 | LIU Zhiwei | Smmtt | ❌ 否 |
| 676 | `7dc27f5f88` | target/riscv: MTT L3 reserved bits must be zero | 2024-12-24 | LIU Zhiwei | Smmtt | ❌ 否 |
| 677 | `eff31a90c7` | target/riscv: MTT L2 entry nozero reserved bits causes exception | 2024-12-24 | LIU Zhiwei | Smmtt | ❌ 否 |
| 678 | `5f7e3945d4` | target/riscv: MTT L1 perm use 2 bits for perm | 2024-12-24 | LIU Zhiwei | Smmtt | ❌ 否 |
| 679 | `185f3108f0` | target/riscv: Using 4M or 2M pages according to mxl | 2024-12-24 | LIU Zhiwei | target/riscv | ❌ 否 |
| 680 | `2a1218c1b8` | target/riscv: Add valid bit for smmtt L3 | 2024-12-24 | LIU Zhiwei | Smmtt | ❌ 否 |
| 681 | `bd08ccac0c` | target/riscv: Set marchid for c908x | 2024-12-24 | LIU Zhiwei | C908 | ❌ 否 |
| 682 | `9f9a2dbcb1` | target/riscv: Using new encoding for fast memory | 2024-12-24 | LIU Zhiwei | xtheadfastm | ❌ 否 |
| 683 | `ae8574aaf2` | target/riscv: Add c908x csr | 2024-12-24 | LIU Zhiwei | C908 | ❌ 否 |
| 684 | `9d3bb57a10` | target/riscv: Use static library for sfu | 2024-12-23 | LIU Zhiwei | SFU/SFA | ❌ 否 |
| 685 | `4b4dbe65b4` | meson: Support sfu lib on windows | 2024-12-23 | LIU Zhiwei | SFU/SFA | ❌ 否 |
| 686 | `88bab06608` | hw/riscv: Fix size_t print format | 2024-12-23 | LIU Zhiwei | hw/riscv | ✅ 是 |
| 687 | `a1d24d6c2a` | tracestub: Using qemu api instead of posix lock | 2024-12-23 | LIU Zhiwei | Trace | ❌ 否 |
| 688 | `c0271c279d` | cskysim: Bump version to v5.2.0 | 2024-12-23 | LIU Zhiwei | cskysim | ❌ 否 |
| 689 | `98cf5daeca` | docs: Support c908x | 2024-12-23 | LIU Zhiwei | docs | ❌ 否 |
| 690 | `26cb94c5d4` | target/riscv: Support c908x | 2024-12-23 | LIU Zhiwei | C908 | ❌ 否 |
| 691 | `05c9eda3ba` | plugins/hotblocks: New plugin for analyzing indirect branch jumps. | 2024-12-21 | Cooper Qu | plugins | ❌ 否 |
| 692 | `66af3ce58d` | target/riscv: C908x is based on r908fdv | 2024-12-23 | LIU Zhiwei | R908A | ❌ 否 |
| 693 | `ee4d50701a` | hw/riscv: Fix dummyh compile | 2024-12-23 | LIU Zhiwei | hw/riscv | ✅ 是 |
| 694 | `2c4fe26d42` | hw/riscv: Fix missing header file | 2024-12-21 | LIU Zhiwei | hw/riscv | ✅ 是 |
| 695 | `0c374d09e4` | target/riscv: Fix mtt overflow | 2024-12-21 | LIU Zhiwei | Smmtt | ✅ 是 |
| 696 | `f30fa0ce79` | hw/timer: Connect timer to all clic regions if need | 2024-12-12 | LIU Zhiwei | CLIC | ❌ 否 |
| 697 | `6cbdf7c93a` | cskysim: Support c908x cpu | 2024-12-19 | LIU Zhiwei | cskysim | ❌ 否 |
| 698 | `e4d5067bbd` | target/riscv: Add c908x cpu | 2024-12-19 | LIU Zhiwei | C908 | ❌ 否 |
| 699 | `14685ffc6d` | target/riscv: Add support for Xuantie fast memory | 2024-12-19 | LIU Zhiwei | xtheadfastm | ❌ 否 |
| 700 | `cf78236907` | target/riscv: Remove unused code | 2024-12-19 | LIU Zhiwei | Misc | ❌ 否 |
| 701 | `7c8d70f501` | fpu/softfloat: Fix fpu build | 2024-12-19 | LIU Zhiwei | fpu | ✅ 是 |
| 702 | `ecffdf058c` | fpu: Canonicalize the E4M3 as it doesn't have Inf | 2024-12-19 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 703 | `e087e78df8` | fpu: Fix uncanon_normal for e4m3 | 2024-12-17 | LIU Zhiwei | xtheadvfofp | ✅ 是 |
| 704 | `98d1da40f2` | target/riscv: Add vary instructions | 2024-12-12 | LIU Zhiwei | xtheadvfreduction | ❌ 否 |
| 705 | `8a657d3db3` | target/riscv: Add vfredmin.* instructions | 2024-12-12 | LIU Zhiwei | xtheadvfreduction | ❌ 否 |
| 706 | `261867ec45` | target/riscv: Add *max_c_* instructions | 2024-12-12 | LIU Zhiwei | target/riscv | ❌ 否 |
| 707 | `a0efb884a9` | target/riscv: Add *_64 max dup instructions | 2024-12-12 | LIU Zhiwei | target/riscv | ❌ 否 |
| 708 | `4b8440f5b3` | target/riscv: Add vbfredmax.dup.32 | 2024-12-12 | LIU Zhiwei | xtheadvfreduction | ❌ 否 |
| 709 | `3968a51650` | target/riscv: Support th_vfredmax.dup.32 | 2024-12-12 | LIU Zhiwei | xtheadvector | ❌ 否 |
| 710 | `6d893010be` | target/riscv: Support vfredsum.c.* | 2024-12-11 | LIU Zhiwei | xtheadvfreduction | ❌ 否 |
| 711 | `992bd989fd` | target/riscv: Prepare for vfredsum.c* | 2024-12-11 | LIU Zhiwei | xtheadvfreduction | ❌ 否 |
| 712 | `de1b2c15fc` | target/riscv: Support th.vbfredsum.dup.64 for xtheadvfreduction | 2024-12-11 | LIU Zhiwei | xtheadvfreduction | ❌ 否 |
| 713 | `70b489e1d0` | target/riscv: Support th.vfredsum.dup.64 for xtheadvfreduction | 2024-12-11 | LIU Zhiwei | xtheadvfreduction | ❌ 否 |
| 714 | `b3d03116b5` | target/riscv: Support th.vbfredsum.dup.32 for xtheadvfreduction | 2024-12-11 | LIU Zhiwei | xtheadvfreduction | ❌ 否 |
| 715 | `58a4126e95` | target/riscv: Support th_vfredsum.dup.32 for xtheadvfreduction | 2024-12-09 | LIU Zhiwei | xtheadvector | ❌ 否 |
| 716 | `b9322b4214` | target/riscv: Fix user mode compile | 2024-12-11 | LIU Zhiwei | Build | ✅ 是 |
| 717 | `2f26c789ae` | target/riscv: Add support for vfwcvt | 2024-12-07 | LIU Zhiwei | xtheadvfcvt | ❌ 否 |
| 718 | `e976e15266` | target/riscv: Use a nonzero mimpid for zhijiang | 2024-12-05 | LIU Zhiwei | zhijiang | ❌ 否 |
| 719 | `ee4b410590` | target/riscv: Enable xtheadvdot for zhijiang | 2024-12-05 | LIU Zhiwei | xtheadvdot | ❌ 否 |
| 720 | `e488708536` | target/riscv: Add support for vfncvt | 2024-12-05 | LIU Zhiwei | xtheadvfcvt | ❌ 否 |
| 721 | `0e7babc9e0` | target/riscv: Support decode of xtheadvfcvt | 2024-12-04 | LIU Zhiwei | xtheadvfcvt | ❌ 否 |
| 722 | `226feec1e1` | target/riscv: Support utnmode CSR | 2024-12-04 | LIU Zhiwei | XuanTie CSR | ❌ 否 |
| 723 | `d2674b85ce` | cskysim: Don't use 0(IPC_PRIVATE) as a share memory key | 2024-11-27 | LIU Zhiwei | cskysim | ❌ 否 |
| 724 | `57b381303a` | target/riscv: Support scontext | 2024-11-26 | LIU Zhiwei | XuanTie CSR | ❌ 否 |
| 725 | `227d73db90` | plugins/hotblocks: Don't filter if filter_by_func is not enabled | 2024-11-25 | LIU Zhiwei | plugins | ❌ 否 |
| 726 | `89cbca9131` | plugins/hotblock: Add a new parameter func_by_filter | 2024-11-24 | LIU Zhiwei | plugins | ❌ 否 |
| 727 | `8d04fcca2f` | plugins/hotblock: Fix assert for hash conflict | 2024-11-24 | LIU Zhiwei | plugins | ✅ 是 |
| 728 | `6fa435c71a` | plugins/hotblocks: Fix a typo | 2024-11-24 | LIU Zhiwei | plugins | ✅ 是 |
| 729 | `bdb7db403d` | plugins/hotblocks: Assert hash conflict | 2024-11-24 | LIU Zhiwei | plugins | ❌ 否 |
| 730 | `85142a4290` | plugins/hotblock: Avoid hash conflict for tbs | 2024-11-24 | LIU Zhiwei | plugins | ❌ 否 |
| 731 | `eaa4f56f4f` | plugins/hotblock: Append function name to hotblock | 2024-11-23 | LIU Zhiwei | plugins | ❌ 否 |
| 732 | `da45682c25` | plugins/hotblck: Output hotblocks by (rate,ecount) | 2024-11-23 | LIU Zhiwei | plugins | ❌ 否 |
| 733 | `68dcefa685` | plugins/hotblock: Sort hot blocks by (exec,rate) | 2024-11-23 | LIU Zhiwei | plugins | ❌ 否 |
| 734 | `0fc82dfc40` | plugins/hotblock: Use scoreboard for total instructions | 2024-11-23 | LIU Zhiwei | plugins | ❌ 否 |
| 735 | `7777a60075` | plugins: Use scoreboard for func profile | 2024-11-23 | LIU Zhiwei | plugins | ❌ 否 |
| 736 | `582cc75976` | plugins: Use scoreboard for smp block | 2024-11-23 | LIU Zhiwei | plugins | ❌ 否 |
| 737 | `d37fe97971` | csky-trace: Expose more interfaces for all arches | 2024-11-22 | LIU Zhiwei | target/csky | ❌ 否 |
| 738 | `1565875bde` | csky-trace: Fix linux user mode compile | 2024-11-22 | LIU Zhiwei | target/csky | ✅ 是 |
| 739 | `b3758d4b3d` | csky-trace: Don't use tb->pc for PCREL | 2024-11-22 | LIU Zhiwei | csky-trace | ❌ 否 |
| 740 | `eb1c8188d1` | plugins: Enhance hotblock by functions and instructions | 2024-11-22 | LIU Zhiwei | plugins | ❌ 否 |
| 741 | `022c945543` | cskysim: Don't use -cp cpu for xml | 2024-11-21 | LIU Zhiwei | cskysim | ❌ 否 |
| 742 | `7832330d83` | csky-trace: Guard csky trace exit with gen_tb_trace | 2024-11-21 | LIU Zhiwei | csky-trace | ❌ 否 |
| 743 | `9f86557848` | riscv-iommu: use pasid & devid as key of RISCVIOMMUContext | 2024-09-30 | yunying.yyp | riscv-iommu | ❌ 否 |
| 744 | `959c744379` | target/riscv: Fix riscv_iommu_ats deadlock | 2024-09-01 | yunying.yyp | riscv-iommu | ✅ 是 |
| 745 | `1922c4cec0` | virtio-pci: PRI support | 2024-11-04 | yunying.yyp | 其他 | ❌ 否 |
| 746 | `9554d747a1` | virtio-pci: PASID support | 2022-01-04 | Jason Wang | 其他 | ❌ 否 |
| 747 | `0bc95d4796` | pcie: pasid support | 2021-11-18 | Jason Wang | 其他 | ❌ 否 |
| 748 | `88f700b1b3` | target/riscv: Disable xtheadfmv and xtheadfmemidx for cpu without float | 2024-11-21 | LIU Zhiwei | xtheadfmv/xtheadfmemidx | ❌ 否 |
| 749 | `f37e42e92a` | target/riscv: Disable xtheadfmv for 32bit r908 | 2024-11-21 | LIU Zhiwei | xtheadfmv/xtheadfmemidx | ❌ 否 |
| 750 | `aa135a39d1` | target/riscv: Add xtheadvdot check | 2024-11-21 | LIU Zhiwei | xtheadvdot | ❌ 否 |
| 751 | `817447fc8a` | target/riscv: Disable Xuantie turbo for _cp processors | 2024-11-21 | LIU Zhiwei | xtheadvarith | ❌ 否 |
| 752 | `badbca043b` | csky-trace: Exit current tb for MMIO | 2024-11-21 | LIU Zhiwei | csky-trace | ❌ 否 |
| 753 | `58d71a1fd4` | docs: Update user manual | 2024-11-19 | LIU Zhiwei | docs | ❌ 否 |
| 754 | `8fe3bfe6f4` | target/riscv: Support xtheadlpw | 2024-11-15 | LIU Zhiwei | xtheadlpw | ❌ 否 |
| 755 | `8a9c2aee82` | target/riscv: Support xtheadvsfa | 2024-11-15 | LIU Zhiwei | xtheadvsfa | ❌ 否 |
| 756 | `967cb7f188` | target/riscv: Support sfu linux dynamic library | 2024-11-15 | LIU Zhiwei | SFU/SFA | ❌ 否 |
| 757 | `7e1fdd7359` | target/riscv: Fix vsha* check | 2024-11-11 | LIU Zhiwei | xtheadvcrypto | ✅ 是 |
| 758 | `f9a9b25e27` | csky-trace: Add x_mt_trace for multi-thread | 2024-11-08 | LIU Zhiwei | csky-trace | ❌ 否 |
| 759 | `f19bd0ae66` | target/riscv: Only clean edge pending when cpu ack | 2024-11-05 | LIU Zhiwei | target/riscv | ❌ 否 |
| 760 | `41ae9a4f95` | target/riscv: Allow write to htinst in M mode | 2024-11-04 | LIU Zhiwei | target/riscv | ❌ 否 |
| 761 | `37d90dc5dd` | Fix QOS | 2024-10-31 | hanyu.cp | 其他 | ✅ 是 |
| 762 | `722323466d` | target/riscv: Enable ssqosid for zhijiang | 2024-10-30 | LIU Zhiwei | ssqosid | ❌ 否 |
| 763 | `2a9fa949e9` | target/riscv: Add a temp arch id for zhijiang | 2024-10-30 | LIU Zhiwei | zhijiang | ❌ 否 |
| 764 | `30dce35e17` | target/riscv: Fix add RVC to zhijiang | 2024-10-30 | LIU Zhiwei | zhijiang | ✅ 是 |
| 765 | `bf9fc79957` | target/riscv: Fix MDT field define | 2024-10-29 | LIU Zhiwei | target/riscv | ✅ 是 |
| 766 | `cd2d7f6c94` | target/riscv: Add softfloat interface for dsa | 2024-10-25 | Huang Tao | DSA | ❌ 否 |
| 767 | `a0022210d3` | target/riscv: Fix the dsa init bug in release version | 2024-10-26 | Huang Tao | DSA | ✅ 是 |
| 768 | `9e00fb4454` | cskysim: Update version v5.1.2 | 2024-10-23 | LIU Zhiwei | cskysim | ❌ 否 |
| 769 | `c0a92fcc39` | hw/riscv: build example SoC when CBQRI_EXAMPLE_SOC enabled | 2023-04-25 | Drew Fustini | hw/riscv | ❌ 否 |
| 770 | `6f3ded5b78` | hw/riscv: instantiate CBQRI controllers for an example SoC | 2023-04-25 | Nicolas Pitre | hw/riscv | ❌ 否 |
| 771 | `85d1816142` | hw/riscv: add CBQRI controllers to virt machine | 2023-04-25 | Nicolas Pitre | hw/riscv | ❌ 否 |
| 772 | `0adb7b026e` | hw/riscv: meson: add CBQRI controllers to the build | 2023-04-25 | Nicolas Pitre | build-system | ❌ 否 |
| 773 | `038ebe35a8` | hw/riscv: Kconfig: add CBQRI options | 2023-04-25 | Nicolas Pitre | hw/riscv | ❌ 否 |
| 774 | `4bedc93280` | hw/riscv: implement CBQRI bandwidth controller | 2023-04-25 | Nicolas Pitre | hw/riscv | ❌ 否 |
| 775 | `49bfae8e01` | hw/riscv: implement CBQRI capacity controller | 2023-04-25 | Nicolas Pitre | hw/riscv | ❌ 否 |
| 776 | `2e242f249f` | hw/riscv: define capabilities of CBQRI controllers | 2023-04-25 | Nicolas Pitre | hw/riscv | ❌ 否 |
| 777 | `c45d6ae7a2` | riscv: implement Ssqosid extension and sqoscfg CSR | 2023-04-25 | Kornel Dulęba | ssqosid | ❌ 否 |
| 778 | `42edb99fef` | target/riscv: Fix server platform compile | 2024-10-23 | LIU Zhiwei | target/riscv | ✅ 是 |
| 779 | `a10eb6e535` | target/riscv: Add server platform reference cpu | 2024-03-04 | Fei Wu | target/riscv | ❌ 否 |
| 780 | `c50add3bae` | hw/riscv: Add server platform reference machine | 2024-03-04 | Fei Wu | hw/riscv | ❌ 否 |
| 781 | `3b694d9486` | target/riscv: Add interface of gettng reg offset of env to dsa | 2024-10-16 | Huang Tao | DSA | ❌ 否 |
| 782 | `220fa0a7ce` | target/riscv: Fix an g-stage-fail when merge cfi patch | 2024-10-16 | LIU Zhiwei | target/riscv | ✅ 是 |
| 783 | `85c743da8e` | hw/riscv/dummyh: Fix creating csky_timer in dummyh | 2024-10-15 | LIU Zhiwei | hw/riscv | ✅ 是 |
| 784 | `3190e3d755` | hw/net: Use device_class_set_props instead of direct setting | 2024-10-11 | LIU Zhiwei | 其他 | ❌ 否 |
| 785 | `4d30d00f8d` | target/riscv: Update smmtt to v1.0.83 | 2024-09-29 | Huang Tao | Smmtt | ❌ 否 |
| 786 | `69eaaf933b` | targer/riscv: Add a property frac_elen_check | 2024-09-29 | LIU Zhiwei | 其他 | ❌ 否 |
| 787 | `fc546c46d8` | target/riscv: Update maee when write mxstatus | 2024-09-29 | LIU Zhiwei | xtheadmaee | ❌ 否 |
| 788 | `13066357be` | targer/riscv: update dsa api beacuse of cpf support | 2024-09-23 | Huang Tao | DSA | ❌ 否 |
| 789 | `a58d6f3b32` | target/riscv: Expose vlen and elen for vendor cpus | 2024-09-23 | LIU Zhiwei | target/riscv | ❌ 否 |
| 790 | `07c4d8278e` | target/riscv: Update marchid for c908 and r908 | 2024-09-21 | LIU Zhiwei | R908A | ❌ 否 |
| 791 | `beb6fb229e` | target/riscv: Add insn_info to dsa disassemable function | 2024-09-13 | Huang Tao | disas | ❌ 否 |
| 792 | `fae7a8b7c1` | hw/csky_uart: Fix a typo | 2024-09-09 | LIU Zhiwei | Misc | ✅ 是 |
| 793 | `e0fea19257` | target/riscv: Change the dsa disassemable function | 2024-09-11 | Huang Tao | disas | ❌ 否 |
| 794 | `a27fed5ac1` | target/riscv: Expose m/senvcfg and priv to user-mode to enable cfi | 2024-09-09 | Huang Tao | Privilege | ❌ 否 |
| 795 | `c86f580e74` | target/riscv: Delete the user_mode_only constraints | 2024-09-06 | Huang Tao | target/riscv | ❌ 否 |
| 796 | `be05091fea` | target/riscv: Fix the cfi csr bugs | 2024-09-06 | Huang Tao | CSR | ✅ 是 |
| 797 | `50d9b5423b` | target/riscv: Fix the lpad bug | 2024-09-06 | Huang Tao | Zicfilp | ✅ 是 ⬅️ Revert |
| 798 | `df90ea6beb` | cskysim: Add zhijiang cpu | 2024-09-07 | LIU Zhiwei | cskysim | ❌ 否 |
| 799 | `36e2c78829` | cskysim: Add r908/c910v3/c920v3 | 2024-09-07 | LIU Zhiwei | cskysim | ❌ 否 |
| 800 | `02f79863dc` | cskysim/soccfg: Add r908/c910v3/c920v3 xml | 2024-09-07 | LIU Zhiwei | cskysim | ❌ 否 |
| 801 | `ac42b11e79` | cskysim: Update xiaohui xml | 2024-09-07 | LIU Zhiwei | cskysim | ❌ 否 |
| 802 | `0ffbf94058` | hw/dummyh: Split plic_irqs and clic_irqs | 2024-09-07 | LIU Zhiwei | CLIC | ❌ 否 |
| 803 | `0da0989ec2` | hw/csky: Only trigger interrupt if irq is not NULL | 2024-09-07 | LIU Zhiwei | 其他 | ❌ 否 |
| 804 | `15dd8a9ace` | configure: Enable dynsoc in meson | 2024-09-07 | LIU Zhiwei | build-system | ❌ 否 |
| 805 | `e80a1158fb` | tests/riscv: Add dsa test case | 2024-09-06 | Huang Tao | tests | ❌ 否 |
| 806 | `a870c32330` | target/riscv: Add disas support for DSA | 2024-09-03 | Huang Tao | disas | ❌ 否 |
| 807 | `720a588b73` | target/riscv: Add DSA support | 2024-08-22 | Huang Tao | DSA | ❌ 否 |
| 808 | `263e88f0c3` | target/riscv: Add lmul trace for vector | 2024-09-04 | LIU Zhiwei | target/riscv | ❌ 否 |
| 809 | `4c00d31402` | target/riscv: Fix mzero uimm3 bug | 2024-09-04 | Huang Tao | Matrix | ✅ 是 |
| 810 | `ecdd5fd972` | hw/riscv: Fix PLIC priority base for Xuantie | 2024-09-03 | LIU Zhiwei | hw/riscv | ✅ 是 |
| 811 | `a962813a5d` | target/riscv: Fix wfi in not influenced by mstatus.mie | 2024-09-03 | LIU Zhiwei | target/riscv | ✅ 是 |
| 812 | `e3924a0c96` | target/riscv: Add xtheadfmv support for r908-rv32 | 2024-09-01 | LIU Zhiwei | xtheadfmv/xtheadfmemidx | ❌ 否 |
| 813 | `4fd3c4987f` | target/riscv: Fix using sizek instead of all columns | 2024-08-31 | LIU Zhiwei | target/riscv | ✅ 是 |
| 814 | `8927f89523` | target/riscv: Support zama16b for zacas and zabha | 2024-08-31 | LIU Zhiwei | Zabha | ❌ 否 ⬅️ Revert |
| 815 | `f71751e88f` | target/riscv: Fix zvfh depends on zfh for cpu | 2024-08-30 | LIU Zhiwei | RVV | ✅ 是 |
| 816 | `12874500cb` | target/riscv: Fix fmmacc.h.hb | 2024-08-30 | LIU Zhiwei | Matrix | ✅ 是 |
| 817 | `3b65726177` | target/riscv: Fix matrix insn bugs | 2024-08-30 | Huang Tao | Matrix | ✅ 是 |
| 818 | `75eefd8ccb` | target/riscv: Fix mtvec for only CLIC mode | 2024-08-29 | LIU Zhiwei | CLIC | ✅ 是 |
| 819 | `2cbe490f3c` | hw/csky: Set clic_irq to NULL for csky | 2024-08-29 | LIU Zhiwei | CLIC | ❌ 否 |
| 820 | `fc0e333c07` | hw/csky_uart: Only set clic_irq when it is not NULL | 2024-08-29 | LIU Zhiwei | CLIC | ❌ 否 |
| 821 | `b73da07d9e` | target/riscv: Add cpu for r908-rv32 | 2024-08-29 | LIU Zhiwei | R908A | ❌ 否 |
| 822 | `dab9298245` | hw/char: Fix set clic_irq at the same time | 2024-08-29 | LIU Zhiwei | CLIC | ✅ 是 |
| 823 | `e2e6c53f95` | hw/xt_clic: Hardwire clic to machine mode | 2024-08-28 | LIU Zhiwei | CLIC | ❌ 否 |
| 824 | `20fc83eec2` | target/riscv: Update xtvec for a system supports both modes | 2024-08-28 | LIU Zhiwei | target/riscv | ❌ 否 |
| 825 | `3d17b1f7f4` | target/riscv: Update priv spec to 1.13 for c910v3 | 2024-08-28 | LIU Zhiwei | C910 | ❌ 否 |
| 826 | `3f40797123` | hw/xiaohui: Fix clint memory map | 2024-08-27 | LIU Zhiwei | Board | ✅ 是 |
| 827 | `9a010befd1` | hw/xt_clic: Update clic base to 0xc010000 | 2024-08-27 | LIU Zhiwei | CLIC | ❌ 否 |
| 828 | `b59a27af1c` | hw/xt_clic: Fix error mask for mmio | 2024-08-27 | LIU Zhiwei | CLIC | ✅ 是 |
| 829 | `df4f7bd9ea` | hw/xt_clic: Support less than 4 byte rw on common mmio | 2024-08-27 | LIU Zhiwei | CLIC | ❌ 否 |
| 830 | `6434600e1b` | target/riscv: Using strict detect for clic as we may in clint mode | 2024-08-26 | LIU Zhiwei | CLIC | ❌ 否 |
| 831 | `fde495abf1` | hw/xt_clic: Fix irqs number | 2024-08-26 | LIU Zhiwei | CLIC | ✅ 是 |
| 832 | `36918c4bae` | hw/aclint: Fix an typo | 2024-08-26 | LIU Zhiwei | Board | ✅ 是 |
| 833 | `dc9dd1c5fe` | hw/xt_clic: Remove a typo | 2024-08-26 | LIU Zhiwei | CLIC | ❌ 否 |
| 834 | `f05a72d3a9` | target/riscv: Fix the MTT lookup bug | 2024-08-22 | Huang Tao | Smmtt | ✅ 是 |
| 835 | `63be55d768` | hw/xt_clic: Allow more than 1 byte read | 2024-08-22 | LIU Zhiwei | CLIC | ❌ 否 |
| 836 | `8a76c3f640` | target/riscv: Always expose clic CSRs for cpu with clit_clic | 2024-08-21 | LIU Zhiwei | CLIC | ❌ 否 |
| 837 | `514e9dc67a` | hw/xt_clic: We can't use current cpu when debug | 2024-08-21 | LIU Zhiwei | CLIC | ❌ 否 |
| 838 | `21dc2c7beb` | target/riscv: Support mscratchcsl for CLIC | 2024-08-19 | LIU Zhiwei | CLIC | ❌ 否 |
| 839 | `48c4abb28f` | target/riscv: Support mscratchcsw for CLIC | 2024-08-19 | LIU Zhiwei | CLIC | ❌ 否 |
| 840 | `840d558fb5` | target/riscv: Support mclicbase for CLIC | 2024-08-19 | LIU Zhiwei | CLIC | ❌ 否 |
| 841 | `536a416ad1` | target/riscv: Support mnxti in CLIC | 2024-08-19 | LIU Zhiwei | CLIC | ❌ 否 |
| 842 | `44a158d300` | target/riscv: Add a clint_clic status for r908 | 2024-08-19 | LIU Zhiwei | CLIC | ❌ 否 |
| 843 | `bdf3e37bd7` |  hw/csky_timer: Attach to another clic port | 2024-08-19 | LIU Zhiwei | CLIC | ❌ 否 |
| 844 | `3470956453` | target/riscv: Add Zvkgs ISA extension support | 2024-08-13 | Huang Tao | zvkgs/zvbc32e/zbc | ❌ 否 |
| 845 | `2c96bf1751` | target/riscv: Add Zvbc32e ISA extension support | 2024-08-13 | Huang Tao | zvkgs/zvbc32e/zbc | ❌ 否 |
| 846 | `024307b3c0` | target/riscv: Make mdtcmcr/mitcmcr WARL | 2024-08-18 | LIU Zhiwei | target/riscv | ❌ 否 |
| 847 | `851ceb2f08` | target/riscv: Fix tcm size as read only | 2024-08-18 | LIU Zhiwei | target/riscv | ✅ 是 |
| 848 | `11ca7340e3` | target/riscv: Use default 32KB TCM for r910 | 2024-08-18 | LIU Zhiwei | target/riscv | ❌ 否 |
| 849 | `1176893a75` | target/riscv: Add tcm support for r908 | 2024-08-18 | LIU Zhiwei | R908A | ❌ 否 |
| 850 | `97cce5bece` | target/riscv: Initialize e907 TCM to 32KB | 2024-08-18 | LIU Zhiwei | E90x | ❌ 否 |
| 851 | `49b4a682be` | hw/intc/clic: Remove one debug print | 2024-08-18 | LIU Zhiwei | CLIC | ❌ 否 |
| 852 | `b11b83589c` | hw/csky_uart: Attach to another clic port | 2024-08-18 | LIU Zhiwei | CLIC | ❌ 否 |
| 853 | `9dc073a6b1` | target/riscv: add r908 CPU type and initialization | 2024-08-15 | Huang Tao | R908A | ❌ 否 |
| 854 | `202cbceafa` | hw/riscv: Xiaohui support Xuantie CLIC | 2024-08-16 | LIU Zhiwei | CLIC | ❌ 否 |
| 855 | `940bf901b6` | hw/intc: Support Xuantie CLIC | 2024-08-15 | LIU Zhiwei | CLIC | ❌ 否 |
| 856 | `5d5187ebfd` | target/riscv: Add privilege mode check in MTT | 2024-08-16 | Huang Tao | xtheadvector | ❌ 否 |
| 857 | `5a97e5f489` | target/riscv: Fix windows compile error | 2024-08-12 | LIU Zhiwei | Build | ✅ 是 |
| 858 | `3d0907793c` | target/riscv: Add coprocessor support in c910v3_cp/c920v3_cp | 2024-08-12 | LIU Zhiwei | C920 | ❌ 否 |
| 859 | `9792861349` | target/riscv: Add Xuantie coprocessor support | 2024-08-12 | LIU Zhiwei | CPU | ❌ 否 |
| 860 | `008a761413` | target/riscv: Add c910v3/c920v3 cpu support | 2024-08-12 | LIU Zhiwei | C920 | ❌ 否 |
| 861 | `efa89d4478` | target/riscv: Support rvb23 profile | 2024-08-12 | LIU Zhiwei | RVB23 | ❌ 否 |
| 862 | `d628f3e82d` | hw/riscv-iommu: Fix msi redirection address | 2024-08-05 | LIU Zhiwei | riscv-iommu | ✅ 是 |
| 863 | `6c0478a258` | target/riscv: Fix gdb register read | 2024-08-01 | LIU Zhiwei | GDB | ✅ 是 |
| 864 | `d724ff4f94` | target/riscv: Add a cpu named zhijiang | 2024-08-01 | LIU Zhiwei | zhijiang | ❌ 否 |
| 865 | `e99e31e40f` | target/riscv: Make locfip delegatable | 2024-07-31 | LIU Zhiwei | Zicfilp | ❌ 否 ⬅️ Revert |
| 866 | `d1f6aa26b9` | target/riscv: Fix a typo | 2024-07-30 | LIU Zhiwei | Misc | ✅ 是 |
| 867 | `9ab11dcf9b` | target/riscv: Reserve pbmt value 0b11 for future use | 2024-07-30 | LIU Zhiwei | Svpbmt | ❌ 否 |
| 868 | `8f15e7ee99` | target/riscv: Fix stateen0 behavior on MXL32 | 2024-07-30 | LIU Zhiwei | Smstateen | ✅ 是 |
| 869 | `53e46e30d5` | target/riscv: Only makes ctr writable when smctr enabled | 2024-07-30 | LIU Zhiwei | Smctr/Ssctr | ❌ 否 ⬅️ Revert |
| 870 | `5a61d70cad` | target/riscv: Only makes jvt writable when zcmt enabled | 2024-07-30 | LIU Zhiwei | Zcmt | ❌ 否 |
| 871 | `0ec38923e7` | target/riscv: Only makes envcfg writable when RVS enabled | 2024-07-30 | LIU Zhiwei | Privilege | ❌ 否 |
| 872 | `c526408a4c` | meson: Initialize asp_win to remove dependency on Linux | 2024-07-30 | LIU Zhiwei | build-system | ❌ 否 |
| 873 | `242509cc6a` | meson: Probe asp for windows in another way | 2024-07-30 | LIU Zhiwei | build-system | ❌ 否 |
| 874 | `fb4957efb7` | target/riscv: Don't expose plugin API under windows | 2024-07-30 | LIU Zhiwei | plugins | ❌ 否 |
| 875 | `72fffc7d6b` | target/riscv: Using BIT_ULL define mctrctl fields | 2024-07-30 | LIU Zhiwei | Smctr/Ssctr | ❌ 否 ⬅️ Revert |
| 876 | `128dd286f5` | target/riscv: Fix fcvt fp8 with fp16 check | 2024-07-26 | LIU Zhiwei | xtheadvfofp | ✅ 是 |
| 877 | `b2f672ab54` | target/riscv: Fix fcvt fp8 with fp32 check | 2024-07-26 | LIU Zhiwei | xtheadvfofp | ✅ 是 |
| 878 | `30798f1095` | hw: Fix windows compile error | 2024-07-26 | LIU Zhiwei | Build | ✅ 是 |
| 879 | `f480f2b5cc` | target/riscv: Fix write mseccfg | 2024-07-26 | LIU Zhiwei | Ssnpm/Smnpm/Smmpm | ✅ 是 ⬅️ Revert |
| 880 | `9acce8fc02` | target/riscv: Using uimm3 for mzero | 2024-07-26 | LIU Zhiwei | Matrix | ❌ 否 |
| 881 | `f7e6e967ff` | target/riscv: Ingore xuantie CSRs for riscv_csrr | 2024-07-26 | LIU Zhiwei | XuanTie CSR | ❌ 否 |
| 882 | `de39549e22` | target/riscv: Fix mseccfg length | 2024-07-26 | LIU Zhiwei | Ssnpm/Smnpm/Smmpm | ✅ 是 ⬅️ Revert |
| 883 | `e03a53fc62` | target/riscv: Using 64-bit mask for PMM in mseccfg | 2024-07-26 | LIU Zhiwei | Ssnpm/Smnpm/Smmpm | ❌ 否 ⬅️ Revert |
| 884 | `be490fe2b0` | target/riscv: Add support for mseccfgh | 2024-07-26 | LIU Zhiwei | Ssnpm/Smnpm/Smmpm | ❌ 否 ⬅️ Revert |
| 885 | `31bfe3aa10` | target/riscv: Fix mseccfg priv version to 1.12 | 2024-07-26 | LIU Zhiwei | Ssnpm/Smnpm/Smmpm | ✅ 是 ⬅️ Revert |
| 886 | `4f742d1b3d` | target/riscv: Fix boot guest linux error | 2024-07-26 | LIU Zhiwei | Boot | ✅ 是 |
| 887 | `74d6161562` | Merge branch 3390c4884cbfb4e4928eca00b9e5ae0d5a4f3160 into xuantie-v9.0-dev Title: Merge request from 甲一 | 2024-07-23 | lzw194868 | Merge | ❌ 否 |
| 888 | `3390c4884c` | Merge branch 'xuantie-matrix-v0.4-dev' into xuantie-v9.0-dev | 2024-07-23 | LIU Zhiwei | Matrix | ❌ 否 |
| 889 | `eaf96a0c17` | target/riscv: Fix matrix half-byte to byte conversion offset calculation | 2024-07-22 | Zhao.Mingxin | Matrix | ✅ 是 |
| 890 | `8715c5ce82` | target/riscv: Fix matrix floating conversion offset calculation | 2024-07-22 | Zhao.Mingxin | Matrix | ✅ 是 |
| 891 | `2277fd2be3` | target/riscv: Fix matrix unsigned value mask calculation | 2024-07-22 | Zhao.Mingxin | Matrix | ✅ 是 |
| 892 | `39e2b86a38` | target/riscv: Fix matrix m{column,row} slide{up,down} iteration indexes | 2024-07-22 | Zhao.Mingxin | Matrix | ✅ 是 |
| 893 | `60d850fa3a` | target/riscv: Rename fwmmacc.s to fmmacc.d.s | 2024-07-22 | Zhao.Mingxin | Matrix | ❌ 否 |
| 894 | `a03967a893` | target/riscv: Remove legacy matrix-GPR type instructions | 2024-07-22 | Zhao.Mingxin | Matrix | ❌ 否 |
| 895 | `b5a5e3cbc7` | target/riscv: Change matrix config to not return size to GPR | 2024-07-22 | Zhao.Mingxin | Matrix | ❌ 否 |
| 896 | `96c8749ccb` | target/riscv: Update matrix decodetree to the latest v0.4 | 2024-07-22 | Zhao.Mingxin | Matrix | ❌ 否 |
| 897 | `463f52ba42` | target/riscv: Change matrix floating point instructions to use mfp_status | 2024-07-22 | Zhao.Mingxin | Matrix | ❌ 否 |
| 898 | `2b3a847f1b` | target/riscv: Add separate matrix mfrm and mfflags MCSR fields | 2024-07-22 | Zhao.Mingxin | Matrix | ❌ 否 |
| 899 | `23eca4b651` | target/riscv: Add trap and ret support for Zicfilp | 2024-07-17 | Huang Tao | Zicfilp | ❌ 否 ⬅️ Revert |
| 900 | `f4f7f36d32` | target/riscv: Add LPAD instruction for Zicfilp | 2024-07-17 | Huang Tao | Zicfilp | ❌ 否 ⬅️ Revert |
| 901 | `7c67cbc075` | target/riscv: Add csr support for Zicfilp extension | 2024-07-17 | Huang Tao | Zicfilp | ❌ 否 ⬅️ Revert |
| 902 | `3a1329235f` | target/riscv: Add properties for Zicfilp extension | 2024-07-17 | Huang Tao | Zicfilp | ❌ 否 ⬅️ Revert |
| 903 | `b5f2cbd636` | target/riscv: Add instructions and mmu implementation for Zicfiss | 2024-07-17 | Huang Tao | Zicfiss | ❌ 否 ⬅️ Revert |
| 904 | `b017f8558f` | target/riscv: Add matrix float double/single conversion instructions | 2024-07-21 | Zhao.Mingxin | Matrix | ❌ 否 |
| 905 | `575ea660f0` | target/riscv: Add matrix double float binary operations | 2024-07-21 | Zhao.Mingxin | Matrix | ❌ 否 |
| 906 | `1a373350c1` | target/riscv: Add double word matrix binary op instructions | 2024-07-21 | Zhao.Mingxin | Matrix | ❌ 否 |
| 907 | `d05ea7d63f` | target/riscv: Add new matrix instruction legalization checks | 2024-07-21 | Zhao.Mingxin | Matrix | ❌ 否 |
| 908 | `27b8fb3560` | target/riscv: Add matrix misc pack instructions | 2024-07-20 | Zhao.Mingxin | Matrix | ❌ 否 |
| 909 | `ae09d29293` | target/riscv: Add matrix half-byte integer mixed-precision multiplication | 2024-07-20 | Zhao.Mingxin | Matrix | ❌ 否 |
| 910 | `6d8398d3dd` | target/riscv: Add matrix normal multiplication accumulation operations | 2024-07-20 | Zhao.Mingxin | Matrix | ❌ 否 |
| 911 | `33857953dd` | fpu/softfloat: Add float8e4/float8e5 to/from bfloat16 | 2024-07-20 | Zhao.Mingxin | xtheadvfofp | ❌ 否 |
| 912 | `1f2bbca014` | target/riscv: Add matrix mixed-precision multiplication accumulation | 2024-07-19 | Zhao.Mingxin | Matrix | ❌ 否 |
| 913 | `80950553f7` | target/riscv: Add csr support for Zicfiss extension | 2024-07-15 | Huang Tao | Zicfiss | ❌ 否 ⬅️ Revert |
| 914 | `f00379c20d` | target/riscv: Add properties for Zicfiss extension | 2024-07-15 | Huang Tao | Zicfiss | ❌ 否 ⬅️ Revert |
| 915 | `d1d7d9e83c` | target/riscv: Add matrix half-byte integer conversion instructions | 2024-07-18 | Zhao.Mingxin | Matrix | ❌ 否 |
| 916 | `2e8be578c4` | target/riscv: Add matrix floating-point conversion instructions | 2024-07-18 | Zhao.Mingxin | Matrix | ❌ 否 |
| 917 | `9a397d5319` | fpu/softfloat: Add float8e5/float8e4 conversion for float16 | 2024-07-18 | Zhao.Mingxin | xtheadvfofp | ❌ 否 |
| 918 | `bd6161669b` | target/riscv: Add matrix floating point binary operations | 2024-07-18 | Zhao.Mingxin | Matrix | ❌ 否 |
| 919 | `f8f2e1dd30` | target/riscv: Add matrix mn4clip instructions | 2024-07-17 | Zhao.Mingxin | Matrix | ❌ 否 |
| 920 | `05e2a17430` | target/riscv: Add matrix slide/cast-move instructions | 2024-07-16 | Zhao.Mingxin | Matrix | ❌ 否 |
| 921 | `4f31ceecf9` | target/riscv: Add matrix max/min/umax/umin/sll/srl instructions | 2024-07-16 | Zhao.Mingxin | Matrix | ❌ 否 |
| 922 | `1ee1600229` | fpu/softfloat: Add float8e4 and float8e5 interfaces | 2024-07-16 | LIU Zhiwei | xtheadvfofp | ❌ 否 |
| 923 | `11308c5008` | target/riscv: Support Smmtt extension | 2024-07-05 | Huang Tao | Smmtt | ❌ 否 |
| 924 | `adc7da555e` | target/riscv: Support Smsdid extension | 2024-07-03 | Huang Tao | Smsdid | ❌ 否 |
| 925 | `c48465578c` | target/riscv: Fix zimop major opcode to 0x73 | 2024-07-09 | LIU Zhiwei | Zimop | ✅ 是 ⬅️ Revert |
| 926 | `9e95ba0da5` | csky-trace: Enable data filter trace for RISC-V | 2024-07-03 | LIU Zhiwei | csky-trace | ❌ 否 |
| 927 | `33cca187a2` | target/riscv: Disable xtheadfmemidx for e906-rv32 | 2024-06-21 | Huang Tao | xtheadfmv/xtheadfmemidx | ❌ 否 |
| 928 | `f372bad070` | target/riscv: Add RVV misa to boot linux | 2024-06-21 | Huang Tao | Boot | ❌ 否 |
| 929 | `a402c60c98` | target/riscv: Add f16f32 and f32f64 to tbflags | 2024-06-21 | Huang Tao | target/riscv | ❌ 否 |
| 930 | `73fdf75a0c` | target/riscv: Enable zfbfmin for c907fd* cpus | 2024-06-21 | Huang Tao | BF16 | ❌ 否 |
| 931 | `ea225e1768` | target/riscv: Reset xmisa to enable all matrix subextension | 2024-06-21 | LIU Zhiwei | Matrix | ❌ 否 |
| 932 | `02cd3214ef` | RISCV: Merge new features | 2024-06-20 | LIU Zhiwei | Merge | ❌ 否 |
| 933 | `c0e234100b` | target/riscv: add Smdbltrp extension support | 2024-04-18 | Clément Léger | Smdbltrp | ❌ 否 ⬅️ Revert |
| 934 | `8883d4923e` | target/riscv: add Ssdbltrp extension support | 2024-04-18 | Clément Léger | Ssdbltrp | ❌ 否 ⬅️ Revert |
| 935 | `c3d2dc31f9` | target/riscv: Enable updates for pointer masking variables and thus enable pointer masking extension | 2024-05-11 | Alexey Baturo | Ssnpm/Smnpm/Smmpm | ❌ 否 ⬅️ Revert |
| 936 | `48f7175a1d` | target/riscv: Update address modify functions to take into account pointer masking | 2024-05-11 | Alexey Baturo | Ssnpm/Smnpm/Smmpm | ❌ 否 ⬅️ Revert |
| 937 | `4b94ef9f75` | target/riscv: Add pointer masking tb flags | 2024-05-11 | Alexey Baturo | Ssnpm/Smnpm/Smmpm | ❌ 否 ⬅️ Revert |
| 938 | `423d9b59a9` | target/riscv: Add helper functions to calculate current number of masked bits for pointer masking | 2024-05-11 | Alexey Baturo | Ssnpm/Smnpm/Smmpm | ❌ 否 ⬅️ Revert |
| 939 | `bbd2d4cb73` | target/riscv: Add new CSR fields for S{sn, mn, m}pm extensions as part of Zjpm v0.8 | 2024-05-11 | Alexey Baturo | Ssnpm/Smnpm/Smmpm | ❌ 否 ⬅️ Revert |
| 940 | `aed7cec05d` | target/riscv: Remove obsolete pointer masking extension code. | 2024-05-11 | Alexey Baturo | Ssnpm/Smnpm/Smmpm | ❌ 否 ⬅️ Revert |
| 941 | `818c782cce` | target/riscv: Add support to access ctrsource, ctrtarget, ctrdata regs. | 2024-05-24 | Rajnesh Kanwal | Smctr/Ssctr | ❌ 否 ⬅️ Revert |
| 942 | `85ba69453a` | target/riscv: Add CTR sctrclr instruction. | 2024-05-24 | Rajnesh Kanwal | Smctr/Ssctr | ❌ 否 ⬅️ Revert |
| 943 | `f6cd18c434` | target/riscv: Add support to record CTR entries. | 2024-05-24 | Rajnesh Kanwal | Smctr/Ssctr | ❌ 否 ⬅️ Revert |
| 944 | `7ce39ba7bc` | target/riscv: Add support for Control Transfer Records extension CSRs. | 2024-05-24 | Rajnesh Kanwal | CSR | ❌ 否 |
| 945 | `f5a52b54fc` | target/riscv: Add Control Transfer Records CSR definitions. | 2024-05-24 | Rajnesh Kanwal | CSR | ❌ 否 |
| 946 | `9d607b1eb5` | target/riscv: Remove obsolete sfence.vm instruction | 2024-05-24 | Rajnesh Kanwal | ss1p13 | ❌ 否 |
| 947 | `306569a41e` | TEMP: target/riscv: Uprev opensbi to include CTR extension. | 2024-05-15 | Rajnesh Kanwal | Smctr/Ssctr | ❌ 否 ⬅️ Revert |
| 948 | `164d9dc328` | target/riscv: Add counter delegation/configuration support | 2023-06-09 | Kaiwen Xue | Smcdeleg/Ssccfg | ❌ 否 ⬅️ Revert |
| 949 | `7be0ff538a` | target/riscv: Add select value range check for counter delegation | 2023-06-07 | Kaiwen Xue | Smcdeleg/Ssccfg | ❌ 否 ⬅️ Revert |
| 950 | `af5490ce25` | target/riscv: Add counter delegation definitions | 2023-06-07 | Kaiwen Xue | Smcdeleg/Ssccfg | ❌ 否 ⬅️ Revert |
| 951 | `7501602e89` | target/riscv: Add smcdeleg/ssccfg properties | 2023-06-07 | Kaiwen Xue | Smcdeleg/Ssccfg | ❌ 否 ⬅️ Revert |
| 952 | `d4c93a79d1` | target/riscv: Support generic CSR indirect access | 2023-06-02 | Kaiwen Xue | CSR | ❌ 否 |
| 953 | `304101acba` | target/riscv: Enable S*stateen bits for AIA | 2023-09-25 | Atish Patra | Smstateen | ❌ 否 |
| 954 | `27ec00e741` | target/riscv: Decouple AIA processing from xiselect and xireg | 2023-06-02 | Kaiwen Xue | Smcsrind | ❌ 否 ⬅️ Revert |
| 955 | `2a4eee2a02` | target/riscv: Add properties for Indirect CSR Access extension | 2023-06-02 | Kaiwen Xue | Smcsrind | ❌ 否 ⬅️ Revert |
| 956 | `549681099a` | COVER | 2023-12-28 | Atish Patra | 其他 | ❌ 否 |
| 957 | `921710569c` | target/riscv: More accurately model priv mode filtering. | 2024-05-10 | Rajnesh Kanwal | Smcntrpmf | ❌ 否 ⬅️ Revert |
| 958 | `83a9eeb00d` | target/riscv: Start counters from both mhpmcounter and mcountinhibit | 2024-05-14 | Rajnesh Kanwal | Smcntrpmf | ❌ 否 ⬅️ Revert |
| 959 | `4d858a4feb` | target/riscv: Enforce WARL behavior for scounteren/hcounteren | 2024-04-29 | Atish Patra | target/riscv | ❌ 否 |
| 960 | `3ee863f50e` | target/riscv: Save counter values during countinhibit update | 2024-04-29 | Atish Patra | Smcntrpmf | ❌ 否 ⬅️ Revert |
| 961 | `5418511c58` | target/riscv: Implement privilege mode filtering for cycle/instret | 2023-12-28 | Atish Patra | xtheadvector | ❌ 否 |
| 962 | `2cc9803d9c` | target/riscv: Add cycle & instret privilege mode filtering support | 2023-06-13 | Kaiwen Xue | xtheadvector | ❌ 否 |
| 963 | `2da4b21f87` | target/riscv: Add cycle & instret privilege mode filtering definitions | 2023-06-12 | Kaiwen Xue | xtheadvector | ❌ 否 |
| 964 | `5ca114cf5d` | target/riscv: Add cycle & instret privilege mode filtering properties | 2023-06-16 | Kaiwen Xue | xtheadvector | ❌ 否 |
| 965 | `7b4695fe16` | target/riscv: Fix the predicate functions for mhpmeventhX CSRs | 2023-12-20 | Atish Patra | Smcntrpmf | ✅ 是 ⬅️ Revert |
| 966 | `db496bcda2` | target/riscv: Combine set_mode and set_virt functions. | 2024-05-14 | Rajnesh Kanwal | ss1p13 | ❌ 否 |
| 967 | `f6279b1a7a` | COVER | 2023-12-28 | Atish Patra | 其他 | ❌ 否 |
| 968 | `663a859c22` | target/riscv: rvzicbo: Fixup CBO extension register calculation | 2024-05-14 | Alistair Francis | target/riscv | ✅ 是 |
| 969 | `5855051d8e` | target/riscv: Remove experimental prefix from "B" extension | 2024-05-14 | Rob Bradford | B ext | ❌ 否 |
| 970 | `fde1bfea9d` | target/riscv: do not set mtval2 for non guest-page faults | 2024-05-03 | Alexei Filippov | target/riscv | ❌ 否 |
| 971 | `e3c343db8e` | target/riscv: prioritize pmp errors in raise_mmu_exception() | 2024-04-13 | Daniel Henrique Barboza | PMP | ❌ 否 |
| 972 | `aff7775d6f` | target/riscv: rvv: Remove redudant SEW checking for vector fp narrow/widen instructions | 2024-03-22 | Max Chou | RVV | ❌ 否 |
| 973 | `da16c2bccd` | target/riscv: rvv: Check single width operator for vfncvt.rod.f.f.w | 2024-03-22 | Max Chou | xtheadvfcvt | ❌ 否 |
| 974 | `82724c6c4f` | target/riscv: rvv: Check single width operator for vector fp widen instructions | 2024-03-22 | Max Chou | target/riscv | ❌ 否 |
| 975 | `931eb81cc7` | target/riscv: rvv: Fix Zvfhmin checking for vfwcvt.f.f.v and vfncvt.f.f.w instructions | 2024-03-22 | Max Chou | xtheadvfcvt | ✅ 是 |
| 976 | `aa1ba0c5fd` | riscv: thead: Add th.sxstatus CSR emulation | 2024-04-29 | Christoph Müllner | XuanTie CSR | ❌ 否 |
| 977 | `33b27ea279` | target/riscv: Implement dynamic establishment of custom decoder | 2024-05-06 | Huang Tao | xtheadvcoder | ❌ 否 |
| 978 | `f98604ebce` | target/riscv/cpu.c: fix Zvkb extension config | 2024-05-11 | Yangyu Chen | target/riscv | ✅ 是 |
| 979 | `600c61a258` | target/riscv: raise an exception when CSRRS/CSRRC writes a read-only CSR | 2024-04-03 | Yu-Ming Chang via | CSR | ❌ 否 |
| 980 | `62e5b109ba` | target/riscv: Fix the element agnostic function problem | 2024-03-25 | Huang Tao | RVV | ✅ 是 |
| 981 | `465a891e93` | target/riscv: Relax vector register check in RISCV gdbstub | 2024-03-28 | Jason Chien | gdbstub | ❌ 否 |
| 982 | `04905949b3` | target/riscv: Add support for Zve64x extension | 2024-03-28 | Jason Chien | RVV | ❌ 否 ⬅️ Revert |
| 983 | `6a72f64a53` | target/riscv: Add support for Zve32x extension | 2024-03-28 | Jason Chien | RVV | ❌ 否 ⬅️ Revert |
| 984 | `3911c5d1c8` | trans_privileged.c.inc: set (m | s)tval | 2024-04-16 20:04:37 -0300|Daniel Henrique Barboza | xtheadvector | ❌ 否 |
| 985 | `4d6a2c765c` | target/riscv/debug: set tval=pc in breakpoint exceptions | 2024-04-16 | Daniel Henrique Barboza | target/riscv | ❌ 否 |
| 986 | `6352bf2612` | target/riscv/kvm: tolerate KVM disable ext errors | 2024-04-22 | Daniel Henrique Barboza | KVM | ❌ 否 |
| 987 | `7e7e0d8fe2` | target/riscv: fix instructions count handling in icount mode | 2024-04-11 | Clément Léger | target/riscv | ✅ 是 |
| 988 | `de49d25d06` | hw/riscv/boot.c: Support 64-bit address for initrd | 2024-04-01 | Cheng Yang | hw/riscv | ❌ 否 |
| 989 | `0108e9c3e0` | target/riscv/kvm: implement SBI debug console (DBCN) calls | 2024-04-25 | Daniel Henrique Barboza | KVM | ❌ 否 |
| 990 | `74b49615a1` | target/riscv: Raise exceptions on wrs.nto | 2024-04-24 | Andrew Jones | target/riscv | ❌ 否 |
| 991 | `edb3a28da7` | target/riscv/kvm: Fix exposure of Zkr | 2024-04-22 | Andrew Jones | KVM | ✅ 是 |
| 992 | `aeb6b1c62b` | hw/intc/riscv_aplic: APLICs should add child earlier than realize | 2024-04-09 | yang.zhang | Board | ❌ 否 |
| 993 | `2963d30708` | hw/riscv/virt: Add IOPMP support | 2024-06-12 | Ethan Chen | IOPMP | ❌ 否 |
| 994 | `b675e160f7` | hw/misc/riscv_iopmp: Add RISC-V IOPMP device | 2024-06-12 | Ethan Chen | IOPMP | ❌ 否 |
| 995 | `275e0668ae` | target/riscv: Support for the RISCV Zalasr extension | 2024-06-11 | Brendan Sweeney | Zalasr | ❌ 否 |
| 996 | `749437f831` | qtest/riscv-iommu-test: add init queues test | 2024-05-23 | Daniel Henrique Barboza | tests | ❌ 否 |
| 997 | `12f5495bf3` | hw/riscv/riscv-iommu: Add another irq for mrif notifications | 2024-05-23 | Andrew Jones | riscv-iommu | ❌ 否 |
| 998 | `fcf6257ec2` | hw/riscv/riscv-iommu: add DBG support | 2024-05-23 | Tomasz Jeznach | riscv-iommu | ❌ 否 |
| 999 | `d23c16477d` | hw/riscv/riscv-iommu: add ATS support | 2024-05-23 | Tomasz Jeznach | riscv-iommu | ❌ 否 |
| 1000 | `caf93a7d48` | hw/riscv/riscv-iommu: add s-stage and g-stage support | 2024-05-23 | Tomasz Jeznach | riscv-iommu | ❌ 否 |
| 1001 | `eee3a6b825` | hw/riscv/riscv-iommu: add Address Translation Cache (IOATC) | 2024-05-23 | Tomasz Jeznach | riscv-iommu | ❌ 否 |
| 1002 | `97b319ab63` | test/qtest: add riscv-iommu-pci tests | 2024-05-23 | Daniel Henrique Barboza | tests | ❌ 否 |
| 1003 | `9c1c4828a0` | hw/riscv/virt.c: support for RISC-V IOMMU PCIDevice hotplug | 2024-05-23 | Tomasz Jeznach | riscv-iommu | ❌ 否 |
| 1004 | `3132946d4a` | hw/riscv: add riscv-iommu-pci reference device | 2024-05-23 | Tomasz Jeznach | riscv-iommu | ❌ 否 |
| 1005 | `c2b39de659` | pci-ids.rst: add Red Hat pci-id for RISC-V IOMMU device | 2024-05-23 | Daniel Henrique Barboza | riscv-iommu | ❌ 否 |
| 1006 | `955ca345a8` | hw/riscv: add RISC-V IOMMU base emulation | 2024-05-23 | Tomasz Jeznach | riscv-iommu | ❌ 否 |
| 1007 | `81277ba6ba` | hw/riscv: add riscv-iommu-bits.h | 2024-05-23 | Tomasz Jeznach | riscv-iommu | ❌ 否 |
| 1008 | `b147712f61` | exec/memtxattr: add process identifier to the transaction attributes | 2024-05-23 | Tomasz Jeznach | 其他 | ❌ 否 |
| 1009 | `fa9043256d` | target/riscv: Add Smstateen check for CSRIND | 2024-05-28 | LIU Zhiwei | Smcsrind | ❌ 否 ⬅️ Revert |
| 1010 | `94e453c593` | disas/riscv: Support zabha disassemble | 2024-05-23 | LIU Zhiwei | Zabha | ❌ 否 ⬅️ Revert |
| 1011 | `a02976261c` | target/riscv: Enable zabha for max cpu | 2024-05-23 | LIU Zhiwei | Zabha | ❌ 否 ⬅️ Revert |
| 1012 | `488713c0b5` | target/riscv: Add amocas.[b | h] | 2024-05-23 13:51:26 +0800|LIU Zhiwei | Zabha | ❌ 否 ⬅️ Revert |
| 1013 | `4f5bd823aa` | target/riscv: Move gen_cmpxchg before adding amocas.[b | h] | 2024-05-23 13:43:14 +0800|LIU Zhiwei | Zabha | ❌ 否 ⬅️ Revert |
| 1014 | `0c7617c69f` | target/riscv: Add AMO instructions for Zabha | 2024-05-23 | LIU Zhiwei | Zabha | ❌ 否 ⬅️ Revert |
| 1015 | `f58a97240b` | target/riscv: Move gen_amo before implement Zabha | 2024-05-22 | LIU Zhiwei | Zabha | ❌ 否 ⬅️ Revert |
| 1016 | `b36d99b41c` | target/riscv: Support Zama16b extension | 2024-05-22 | LIU Zhiwei | Zama16b | ❌ 否 ⬅️ Revert |
| 1017 | `a1aa695227` | disas/riscv: Support zcmop disassemble | 2024-05-21 | LIU Zhiwei | Zcmop | ❌ 否 ⬅️ Revert |
| 1018 | `21a8457901` | target/riscv: Add zcmop extension | 2024-05-21 | LIU Zhiwei | Zcmop | ❌ 否 ⬅️ Revert |
| 1019 | `57d4afaf34` | disas/riscv: Support zimop disassemble | 2024-05-21 | LIU Zhiwei | Zimop | ❌ 否 ⬅️ Revert |
| 1020 | `c9201f059b` | target/riscv: Add zimop extension | 2024-05-21 | LIU Zhiwei | Zimop | ❌ 否 ⬅️ Revert |
| 1021 | `a52aab43c7` | target/riscv: Support the version for ss1p13 | 2024-06-06 | Fea.Wang | ss1p13 | ❌ 否 |
| 1022 | `431ee92407` | target/riscv: Reserve exception codes for sw-check and hw-err | 2024-06-06 | Fea.Wang | target/riscv | ❌ 否 |
| 1023 | `2b765f1482` | target/riscv: Add MEDELEGH, HEDELEGH csrs for RV32 | 2024-06-06 | Fea.Wang | ss1p13 | ❌ 否 |
| 1024 | `d8f78ad12c` | target/riscv: Add 'P1P13' bit in SMSTATEEN0 | 2024-06-06 | Fea.Wang | ss1p13 | ❌ 否 |
| 1025 | `5ecaf65645` | target/riscv: Define macros and variables for ss1p13 | 2024-06-06 | Fea.Wang | ss1p13 | ❌ 否 |
| 1026 | `33ea46850f` | target/riscv: Reuse the conversion function of priv_spec | 2024-06-06 | Jim Shu | ss1p13 | ❌ 否 |
| 1027 | `682f4cb26e` | tests/csky: Add csky test cases | 2024-06-19 | Huang Tao | tests | ❌ 否 |
| 1028 | `6ef75f6dab` | target/riscv: Relax zpsfoperand PRIV check for rv32 cpus | 2024-06-19 | Huang Tao | P ext | ❌ 否 |
| 1029 | `9fb4ea3807` | target/riscv: Turn ext_zve32f on when ext_zvfh is true | 2024-06-19 | Huang Tao | RVV | ❌ 否 |
| 1030 | `254893dc97` | target/csky: Fix csky system mode and linux-user mode errors | 2024-06-19 | Huang Tao | target/csky | ✅ 是 |
| 1031 | `4b7c20b2d9` | tracestub: Fix the compile error for other archs | 2024-06-18 | Huang Tao | Build | ✅ 是 |
| 1032 | `e2c6bbd6ec` | Fix the file copyright | 2024-06-17 | Huang Tao | Misc | ✅ 是 |
| 1033 | `60c65c1c33` | docs/csky: Fix csky docs compile problem | 2024-06-17 | Huang Tao | docs | ✅ 是 |
| 1034 | `202d8d71b4` | replay: Fix the function definition error for other Archs except riscv | 2024-06-17 | Huang Tao | 其他 | ✅ 是 |
| 1035 | `c938f42eaf` | replay/simpoint: Exit replay after consuming all simpoints | 2024-06-15 | Huang Tao | 其他 | ❌ 否 |
| 1036 | `1d494d93ef` | plugins/bbv: Use exception instead of sret as monitor exit | 2024-06-15 | Huang Tao | plugins | ❌ 否 |
| 1037 | `4389240bef` | Add simpoint for xiaohui platform | 2024-06-15 | Huang Tao | Board | ❌ 否 |
| 1038 | `914da6c729` | tests/plugin: Add bbv plugin to support simpoint | 2024-06-15 | Huang Tao | tests | ❌ 否 |
| 1039 | `32ea388388` | hw/riscv: Add Linux support for xiaohui platform | 2024-06-15 | Huang Tao | Board | ❌ 否 |
| 1040 | `e1124d64fc` | util/log: Add tb_trace log | 2024-06-15 | Huang Tao | Trace | ❌ 否 |
| 1041 | `e2f38b16c7` | target/riscv: Add ms mask for read_sstatus | 2024-06-15 | Huang Tao | target/riscv | ❌ 否 |
| 1042 | `aa868565d5` | target/riscv: Fix matrix and maee problems | 2024-06-15 | Huang Tao | Matrix | ✅ 是 |
| 1043 | `a84e215750` | hw/riscv/xiaohui: Add CPR support | 2024-06-15 | Huang Tao | Board | ❌ 否 |
| 1044 | `7121e164e4` | target/riscv: Swap I8I32 and I16I64 in xmisa | 2024-06-15 | Huang Tao | Matrix | ❌ 否 |
| 1045 | `32e3a624d7` | hw/riscv: Add xiaohui machine | 2024-06-15 | Huang Tao | Board | ❌ 否 |
| 1046 | `e6930a8c39` | target/riscv: Fix tbflags MS field setting | 2024-06-15 | Huang Tao | target/riscv | ✅ 是 |
| 1047 | `74a1739606` | target/riscv: Don't slack the new added CSRs | 2024-06-15 | Huang Tao | CSR | ❌ 否 |
| 1048 | `dec2d42ad6` | target/riscv: Nanbox bfloat16 for tcg variable | 2024-06-15 | Huang Tao | BF16 | ❌ 否 |
| 1049 | `fc68bcb1ff` | target/riscv: Fix frsqrt_bh for zero and +inf | 2024-06-15 | Huang Tao | BF16 | ✅ 是 |
| 1050 | `2ef8b48e72` | target/riscv: Fix mstm which overides ms3 wrongly | 2024-06-15 | Huang Tao | Matrix | ✅ 是 |
| 1051 | `4593dc1ee7` | target/riscv: Fix mldm which overides md wrongly | 2024-06-15 | Huang Tao | Matrix | ✅ 是 |
| 1052 | `7951bf93ae` | target/riscv: Fix xmcsr read | 2024-06-15 | Huang Tao | Matrix | ✅ 是 |
| 1053 | `5e672ede01` | cskysim: Expose more device for cskysim | 2024-06-15 | Huang Tao | cskysim | ❌ 否 |
| 1054 | `5381bd1303` | target/riscv: Fix the sstatus writing for xtheadvector | 2024-06-15 | Huang Tao | xtheadvector | ✅ 是 |
| 1055 | `379e778a4e` | target/riscv: Always use xtheadmaee first as linux do | 2024-06-15 | Huang Tao | xtheadmaee | ❌ 否 |
| 1056 | `6670d437e7` | trace: Add more exit trace for special instructions | 2024-06-15 | Huang Tao | Trace | ❌ 否 |
| 1057 | `867e6dc73f` | trace: Send debug exit trace for risc-v | 2024-06-15 | Huang Tao | Trace | ❌ 否 |
| 1058 | `73974321e8` | trace: Send exception trace for risc-v | 2024-06-15 | Huang Tao | Trace | ❌ 否 |
| 1059 | `0b10707626` | pctrace: Make pctrace target agnostic | 2024-06-14 | Huang Tao | Smctr/Ssctr | ❌ 否 ⬅️ Revert |
| 1060 | `55e92b72d1` | gdbstub: Set is_gdbserver_start for csky-trace | 2024-06-14 | Huang Tao | csky-trace | ❌ 否 |
| 1061 | `ed73827e31` | docs: Add copyright subject | 2024-06-14 | Huang Tao | docs | ❌ 否 |
| 1062 | `25631ad2a0` | hw/riscv: Fix dummyh compile | 2024-06-14 | Huang Tao | hw/riscv | ✅ 是 |
| 1063 | `ba10f327c2` | tests/qtest: Fix riscv qtest error | 2024-06-14 | Huang Tao | tests | ✅ 是 |
| 1064 | `a10f0f1fc2` | target/riscv: Add gdb support for matrix | 2024-06-14 | Huang Tao | Matrix | ❌ 否 |
| 1065 | `fb2419acc6` | csky-trace: Send trace tail when exit | 2024-06-14 | Huang Tao | csky-trace | ❌ 否 |
| 1066 | `fdcc1806e4` | target/riscv: Add memory trace as 6.x | 2024-06-14 | Huang Tao | Trace | ❌ 否 |
| 1067 | `d885f9064a` | target/riscv: Reset MPP to 3 as Xuantie CPUs do | 2024-06-14 | Huang Tao | CPU | ❌ 否 |
| 1068 | `5a746d8892` | cskysim: Update QEMU version to v9.0.0 | 2024-06-14 | Huang Tao | cskysim | ❌ 否 |
| 1069 | `25d0f866b9` | csky-trace: Fix unit test fails for cpf | 2024-06-14 | Huang Tao | csky-trace | ✅ 是 |
| 1070 | `cbf7c4b350` | target/riscv: Add PM support for T-HEAD sxstatus | 2024-06-14 | Huang Tao | XuanTie CSR | ❌ 否 |
| 1071 | `fc99737057` | configure: Add --enable-dynsoc option | 2024-06-14 | Huang Tao | build-system | ❌ 否 |
| 1072 | `6fc46c3196` | cskysim: Add cskysim support | 2024-06-14 | Huang Tao | cskysim | ❌ 否 |
| 1073 | `cdaa435119` | options: Add xuantie extend options | 2024-06-14 | Huang Tao | build-system | ❌ 否 |
| 1074 | `018b9e2131` | csky-trace: Support simple csky-trace for RISC-V | 2024-06-14 | Huang Tao | csky-trace | ❌ 否 |
| 1075 | `8e74c6e2b7` | target/csky: Support csky arch linux user build | 2024-06-14 | Huang Tao | target/csky | ❌ 否 |
| 1076 | `a7a949c1b1` | target/csky: Update cskyv2 softmmu for Linux boot | 2024-06-13 | Huang Tao | target/csky | ❌ 否 |
| 1077 | `8500d91696` | target/riscv: Add xtheadmaee extension | 2024-06-12 | Huang Tao | xtheadmaee | ❌ 否 |
| 1078 | `4ff9cf491c` | hw/riscv: Add another serial for virt | 2024-06-12 | Huang Tao | hw/riscv | ❌ 否 |
| 1079 | `dac12b21b5` | hw/riscv: Add smarth and smartl for RISC-V | 2024-06-12 | Huang Tao | Board | ❌ 否 |
| 1080 | `0ee14b7b64` | Relax the priviledge check for zvfh and zvfmin | 2024-06-12 | Huang Tao | xtheadvector | ❌ 否 |
| 1081 | `3ea250e529` | target/riscv: update max cpu | 2024-06-06 | Huang Tao | CPU | ❌ 否 |
| 1082 | `363566b8fe` | target/riscv: Add xtheadisr for e906/e907 | 2024-06-06 | Huang Tao | xtheadisr | ❌ 否 |
| 1083 | `28004c91f8` | target/riscv: Log clic INT | 2024-06-06 | Huang Tao | CLIC | ❌ 否 |
| 1084 | `eb2ae18044` | target/riscv: Update interrupt return in CLIC mode | 2024-06-06 | Huang Tao | CLIC | ❌ 否 |
| 1085 | `d47c216cb9` | target/riscv: Update interrupt handling in CLIC mode | 2024-06-06 | Huang Tao | CLIC | ❌ 否 |
| 1086 | `f7cdda8dcc` | target/riscv: Update CSR xnxti in CLIC mode | 2024-06-06 | Huang Tao | CLIC | ❌ 否 |
| 1087 | `6b2d470156` | target/riscv: Update CSR xtvt in CLIC mode | 2024-06-06 | Huang Tao | CLIC | ❌ 否 |
| 1088 | `fd2d41f154` | target/riscv: Update CSR xtvec in CLIC mode | 2024-06-06 | Huang Tao | CLIC | ❌ 否 |
| 1089 | `23a077aefc` | target/riscv: Update CSR xip in CLIC mode | 2024-06-06 | Huang Tao | CLIC | ❌ 否 |
| 1090 | `56c1f25c56` | target/riscv: Update CSR xie in CLIC mode | 2024-06-06 | Huang Tao | CLIC | ❌ 否 |
| 1091 | `b1634ea9bf` | hw/intc: Add CLIC device | 2024-06-06 | Huang Tao | CLIC | ❌ 否 |
| 1092 | `8057e23031` | target/riscv: Add CLIC CSRs | 2024-06-05 | Huang Tao | CLIC | ❌ 否 |
| 1093 | `6cc6c15bc5` | target/riscv: Relax the priviledge check for xthead ext, zca/f/d and zfh/hmin | 2024-06-05 | Huang Tao | xtheadvector | ❌ 否 |
| 1094 | `98f0cf5072` | target/riscv: Support Xuantie CSRs | 2024-06-05 | Huang Tao | XuanTie CSR | ❌ 否 |
| 1095 | `bcb3ff4f09` | target/riscv: Add support for Xuantie Vdot extension | 2024-06-05 | Huang Tao | xtheadvdot | ❌ 否 |
| 1096 | `290e880ff6` | target/riscv: Add Xuantie CPUs support | 2024-06-05 | Huang Tao | CPU | ❌ 否 |
| 1097 | `397f20fe65` | target/riscv: Add system mode build | 2024-06-05 | Huang Tao | Build | ❌ 否 |
| 1098 | `a8c45386a2` | target/riscv: Add fxcr CSR | 2024-06-03 | Huang Tao | XuanTie CSR | ❌ 否 |
| 1099 | `40da0a9277` | target/riscv: Add bfloat16 support for fpu | 2024-06-03 | Huang Tao | BF16 | ❌ 否 |
| 1100 | `65c9f0ded0` | target/riscv: Add bfloat16 support for xtheadvector and RVV | 2024-06-03 | Huang Tao | xtheadvector | ❌ 否 |
| 1101 | `561f36f596` | target/riscv: Add support for matrix v0.3 spec | 2024-06-03 | Huang Tao | Matrix | ❌ 否 |
| 1102 | `668d0c58da` | target/riscv: Add support for packed extension 0.9.4 | 2024-06-03 | Huang Tao | P ext | ❌ 否 |
| 1103 | `7bd4fc9902` | tests/riscv: Add unit test cases | 2024-05-30 | Huang Tao | tests | ❌ 否 |
| 1104 | `3819aeb836` | target/riscv: Support XTheadVector extension | 2024-05-30 | Huang Tao | xtheadvector | ❌ 否 |
| 1105 | `cf590ae554` | target/riscv: Reuse th_csr.c to add user-mode csrs | 2024-04-12 | Huang Tao | XuanTie CSR | ❌ 否 |
| 1106 | `561b12709d` | riscv: thead: Add th.sxstatus CSR emulation | 2024-03-29 | Christoph Müllner | XuanTie CSR | ❌ 否 |
| 1107 | `0b3d3542ab` | target/riscv: Implement dynamic establishment of custom decoder | 2024-05-06 | Huang Tao | xtheadvcoder | ❌ 否 |
| 1108 | `70556ed4aa` | target/riscv: Fix the element agnostic function problem | 2024-03-21 | Huang Tao | RVV | ✅ 是 |
| 1109 | `e99b959533` | linux-user: Fix macro | 2024-05-30 | Huang Tao | linux-user | ✅ 是 |
