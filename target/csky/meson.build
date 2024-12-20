csky_ss = ss.source_set()
csky_ss.add(files(
    'cpu.c',
    'mmu.c',
    'helper.c',
    'gdbstub.c',
    'csky_ldst.c',
))

csky_ss.add(when: 'TARGET_CSKYV1', if_true: files(
    'translate_v1.c',
    'op_helper_v1.c',
))

csky_ss.add(when: 'TARGET_CSKYV2', if_true: files(
    'translate_v2.c',
    'op_helper_v2.c',
    'op_vdsp.c',
    'op_dspv2.c',
    'op_vdsp2.c',
    'op_fpuv3.c',
))

csky_system_ss = ss.source_set()

target_arch += {'csky': csky_ss}
target_system_arch += {'csky': csky_system_ss}
