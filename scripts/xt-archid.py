#!/usr/bin/python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# A script to show marchid for Xuantie cpu models
#

import re

def get_marchid(cpu_string, version=6):
    """
    根据 CPU 描述字符串计算玄铁处理器的 marchid
    :param cpu_string: 输入字符串 (不区分大小写)
    :param version: 硬件版本 (默认6)
    :return: 整数 marchid, 无法识别则返回 0
    """
    if not cpu_string:
        return 0

    # 1. 规范化处理
    s = cpu_string.upper()

    # 2. 定义编码表 (Family, Class)
    # Family: E=0, R=1, I=2, S=3, C=4
    # 注意：这里去除了 C960
    mapping = {
        # C Series (Family 4)
        "C908X": (4, 13), "C925": (4, 7),
        "C930": (4, 8), "C950": (4, 9),
        "C910": (4, 3), "C920": (4, 3),
        "C906": (4, 4), "C908": (4, 5),
        "C907": (4, 6),

        # E Series (Family 0)
        "E902": (0, 1), "E906": (0, 4), "E907": (0, 5),
        "E901PLUS": (0, 8), "E901P": (0, 8), "E901": (0, 9),
        "E908A": (0, 10),

        # R Series (Family 1)
        "R910": (1, 1), "R920": (1, 4), "R908": (1, 2),
    }

    # 3. 最长匹配逻辑：确保 C908X 优先于 C908 匹配
    matched_core = None
    sorted_keys = sorted(mapping.keys(), key=len, reverse=True)

    for core in sorted_keys:
        if core in s:
            matched_core = core
            break

    if not matched_core:
        return 0

    # 4. XLEN 识别逻辑
    # 优先级: RV32关键字 > E908A特例 > E系列默认 > 其他默认64位
    if "RV32" in s:
        xlen = 32
    elif matched_core == "E908A":
        xlen = 32
    elif matched_core.startswith("E"):
        xlen = 32
    else:
        xlen = 64

    # 5. 构造寄存器字段
    fam, cls = mapping[matched_core]
    arch = 0b10  # 27:26 位固定为 RISC-V (10)

    marchid = 0
    # MSB (64位为63, 32位为31)
    marchid |= (1 << (xlen - 1))

    # 27:26: Arch
    marchid |= (arch & 0x3) << 26

    # 25:22: Family
    marchid |= (fam & 0xF) << 22

    # 21:18: Class
    marchid |= (cls & 0xF) << 18

    # 15:11: ISA Version (固定为 0b00001)
    marchid |= (0b00001 & 0x1F) << 11

    # 10:8: Version (硬件版本)
    marchid |= (version & 0x7) << 8

    return marchid

# --- 测试用例清单 ---
# { ./qemu-system-riscv64 -cpu help; ./qemu-system-riscv32 -cpu help;
# ./qemu-riscv32 -cpu help; ./qemu-riscv64 -cpu help; } | sort -u
raw_cases = """
  c906 c906fd c906fdv c907 c907fd c907fd-rv32 c907fd-rv64 c907fdv c907fdvm
  c907fdvm-rv32 c907fdvm-rv64 c907fdv-rv32 c907fdv-rv64 c907-rv32 c907-rv64
  c908 c908-cp_v2 c908-cp_v2-rv32 c908-cp-xt_v2 c908-cp-xt_v2-rv32 c908i
  c908i-cp_v2 c908i-cp_v2-rv32 c908i-cp-xt_v2 c908i-cp-xt_v2-rv32 c908i-rv32
  c908i_v2 c908i_v2-rv32 c908-rv32 c908v c908_v2 c908_v2-rv32 c908v-cp_v2
  c908v-cp_v2-rv32 c908v-cp-xt_v2 c908v-cp-xt_v2-rv32
  c908vk-cp_v2 c908vk-cp-xt_v2
  c908vk_v2 c908v-rv32 c908v_v2 c908v_v2-rv32 c908x c908x-cp c908x-cp-xt c910
  c910v c910v2 c910v3 c910v3-cp c910v3-cp-xt c920 c920v2 c920v3 c920v3-cp
  c920v3-cp-xt c960 e901b-cp e901bzm-cp e901-cp e901plusb-cp e901plusbm-cp
  e901plus-cp e901plusm-cp e901zm-cp e902 e902m e902mt e902t e906 e906f
  e906fd e906fdp e906fp e906p e907 e907f e907fd e907fdp e907fp e907p
  lowrisc-ibex max r908 r908-cp r908-cp-rv32 r908-cp-xt
  r908-cp-xt-rv32 r908fd r908fd-cp r908fd-cp-rv32 r908fd-cp-xt r908fd-cp-xt-rv32
  r908fd-rv32 r908fdv r908fdv-cp r908fdv-cp-rv32 r908fdv-cp-xt r908fdv-cp-xt-rv32
  r908fdv-rv32 r908fdvk-cp r908fdvk-cp-xt r908-rv32
  r910 r920 rv32 rv32e rv32i rv64 rv64e rv64i
  rva22s64 rva22u64 rvb23s64 rvb23u64 rvsp-ref shakti-c sifive-e31 sifive-e34
  sifive-e51 sifive-u34 sifive-u54 thead-c906 veyron-v1
  x-rv128 xt-c930-cp xt-c930v-cp zhijiang
  xt-c925-cp xt-c925v-cp
  xt-e908a1 xt-e908a1f xt-e908a1fd xt-e908a1fdv xt-e908a1fdvk
  xt-e908a1t xt-e908a1ft xt-e908a1fdt xt-e908a1fdvt xt-e908a1fdvkt
"""

# 清洗并排序测试用例
test_cases = sorted(list(set(re.split(r'[,\s\n]+', raw_cases.strip()))))

if __name__ == "__main__":
    print(f"{'Input String':<25} | {'XLEN':<4} | {'marchid (Hex)':<20}")
    print("-" * 65)

    for tc in test_cases:
        if not tc: continue
        m_id = get_marchid(tc)

        # 确定 XLEN 以便格式化显示
        # 逻辑：如果结果不为0，根据之前函数的逻辑计算 XLEN
        up = tc.upper()
        xl = 64
        if m_id != 0:
            e_cores = ["E901", "E902", "E906", "E907"]
            if ("RV32" in up or "E908A" in up
                    or any(core in up for core in e_cores)):
                xl = 32

        if m_id == 0:
            hex_str = "0"
        else:
            hex_str = f"0x{m_id:016X}" if xl == 64 else f"0x{m_id:08X}"

        print(f"{tc:<25} | {xl:<4} | {hex_str}")
