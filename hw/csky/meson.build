csky_ss = ss.source_set()
csky_ss.add(files('csky_boot.c'), fdt)
csky_ss.add(when: 'CONFIG_DYNSOC', if_true: files('dynsoc.c'))
csky_ss.add(when: 'CONFIG_SMARTL', if_true: files('smartl.c'))
csky_ss.add(when: 'CONFIG_YUNVOICE_V2', if_true: files('yunvoice_v2.c'))
csky_ss.add(when: 'CONFIG_CSKY_VIRT', if_true: files('virt.c'))
csky_ss.add(when: 'CONFIG_SMARTH', if_true: files('smarth.c'))
csky_ss.add(when: 'CONFIG_HOBBIT1_V2', if_true: files('hobbit1_v2.c'))
csky_ss.add(when: 'CONFIG_DEIMOS', if_true: files('deimos.c'))
csky_ss.add(when: 'CONFIG_ERAGON', if_true: files('eragon.c'))
csky_ss.add(when: 'CONFIG_TRILOBITE', if_true: files('trilobite.c'))
csky_ss.add(when: 'CONFIG_TRILOBITE_V2', if_true: files('trilobite_v2.c'))
if csky_dynsoc
    csky_ss.add(files('dynsoc.c'))
    csky_ss.add(files('dummyh.c'))
endif
csky_ss.add(when: 'CONFIG_SMART_CARD', if_true: files('smart_card.c'))

hw_arch += {'csky': csky_ss}
