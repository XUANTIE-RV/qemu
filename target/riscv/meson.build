# FIXME extra_args should accept files()
gen = [
  decodetree.process('insn16.decode', extra_args: ['--static-decode=decode_insn16', '--insnwidth=16']),
  decodetree.process('insn32.decode', extra_args: '--static-decode=decode_insn32'),
  decodetree.process('xthead.decode', extra_args: '--static-decode=decode_xthead'),
  decodetree.process('xtheadvector.decode', extra_args: '--static-decode=decode_xtheadvector'),
  decodetree.process('rvp094.decode', extra_args: '--static-decode=decode_rvp094'),
  decodetree.process('XVentanaCondOps.decode', extra_args: '--static-decode=decode_XVentanaCodeOps'),
]

riscv_ss = ss.source_set()
riscv_ss.add(gen)
riscv_ss.add(files(
  'cpu.c',
  'cpu_helper.c',
  'csr.c',
  'th_csr.c',
  'fpu_helper.c',
  'gdbstub.c',
  'op_helper.c',
  'vector_helper.c',
  'xtheadvector_helper.c',
  'packed_helper.c',
  'matrix_helper.c',
  'vector_internals.c',
  'bitmanip_helper.c',
  'translate.c',
  'm128_helper.c',
  'crypto_helper.c',
  'zce_helper.c',
  'vcrypto_helper.c',
  'cfi_helper.c',
  'dsa_helper.c',
))

riscv_system_ss = ss.source_set()
riscv_system_ss.add(files(
  'arch_dump.c',
  'pmp.c',
  'mtt.c',
  'debug.c',
  'monitor.c',
  'machine.c',
  'pmu.c',
  'th_csr.c',
  'time_helper.c',
  'riscv-qmp-cmds.c',
  'riscv-power.c',
))

subdir('tcg')
subdir('kvm')
subdir('sfu')

target_arch += {'riscv': riscv_ss}
target_system_arch += {'riscv': riscv_system_ss}
