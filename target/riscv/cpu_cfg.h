/*
 * QEMU RISC-V CPU CFG
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 * Copyright (c) 2021-2023 PLCT Lab
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

#ifndef RISCV_CPU_CFG_H
#define RISCV_CPU_CFG_H
#include "dsa.h"
/*
 * map is a 16-bit bitmap: the most significant set bit in map is the maximum
 * satp mode that is supported. It may be chosen by the user and must respect
 * what qemu implements (valid_1_10_32/64) and what the hw is capable of
 * (supported bitmap below).
 *
 * init is a 16-bit bitmap used to make sure the user selected a correct
 * configuration as per the specification.
 *
 * supported is a 16-bit bitmap used to reflect the hw capabilities.
 */
typedef struct {
    uint16_t map, init, supported;
} RISCVSATPMap;

struct RISCVCPUConfig {
    bool ext_zba;
    bool ext_zbb;
    bool ext_zbc;
    bool ext_zbkb;
    bool ext_zbkc;
    bool ext_zbkx;
    bool ext_zbs;
    bool ext_zca;
    bool ext_zcb;
    bool ext_zcd;
    bool ext_zce;
    bool ext_zcf;
    bool ext_zcmp;
    bool ext_zcmt;
    bool ext_zk;
    bool ext_zkn;
    bool ext_zknd;
    bool ext_zkne;
    bool ext_zknh;
    bool ext_zkr;
    bool ext_zks;
    bool ext_zksed;
    bool ext_zksh;
    bool ext_zkt;
    bool ext_psfoperand;
    bool ext_zifencei;
    bool ext_zicntr;
    bool ext_zicsr;
    bool ext_zicbom;
    bool ext_zicbop;
    bool ext_zicboz;
    bool ext_zicfiss;
    bool ext_zicfilp;
    bool ext_zicond;
    bool ext_zihintntl;
    bool ext_zihintpause;
    bool ext_ssqosid;
    bool ext_zihpm;
    bool ext_zimop;
    bool ext_zcmop;
    bool ext_ztso;
    bool ext_smstateen;
    bool ext_sstc;
    bool ext_smcntrpmf;
    bool ext_smcsrind;
    bool ext_sscsrind;
    bool ext_smcdeleg;
    bool ext_ssccfg;
    bool ext_ssdbltrp;
    bool ext_smdbltrp;
    bool ext_svadu;
    bool ext_svinval;
    bool ext_svnapot;
    bool ext_svpbmt;
    bool ext_zdinx;
    bool ext_zaamo;
    bool ext_zacas;
    bool ext_zama16b;
    bool ext_zabha;
    bool ext_zalasr;
    bool ext_zalrsc;
    bool ext_zawrs;
    bool ext_zfa;
    bool ext_zfbfmin;
    bool ext_zfh;
    bool ext_zfhmin;
    bool ext_zfinx;
    bool ext_zhinx;
    bool ext_zhinxmin;
    bool ext_zve32f;
    bool ext_zve32x;
    bool ext_zve64f;
    bool ext_zve64d;
    bool ext_zve64x;
    bool ext_zvbb;
    bool ext_zvbc;
    bool ext_zvbc32e;
    bool ext_zvkb;
    bool ext_zvkg;
    bool ext_zvkgs;
    bool ext_zvkned;
    bool ext_zvknha;
    bool ext_zvknhb;
    bool ext_zvksed;
    bool ext_zvksh;
    bool ext_zvkt;
    bool ext_zvkn;
    bool ext_zvknc;
    bool ext_zvkng;
    bool ext_zvks;
    bool ext_zvksc;
    bool ext_zvksg;
    bool ext_zmmul;
    bool ext_zvfbfmin;
    bool ext_zvfbfwma;
    bool ext_zvfh;
    bool ext_zvfhmin;
    bool ext_smaia;
    bool ext_ssaia;
    bool ext_smctr;
    bool ext_ssctr;
    bool ext_sscofpmf;
    bool ext_smepmp;
    bool ext_ssnpm;
    bool ext_smnpm;
    bool ext_smmpm;
    bool ext_smsdid;
    bool ext_smmtt;
    bool rvv_ta_all_1s;
    bool rvv_ma_all_1s;

    uint32_t mvendorid;
    uint64_t marchid;
    uint64_t mimpid;

    /* Named features  */
    bool ext_svade;
    bool ext_zic64b;

    /*
     * Always 'true' booleans for named features
     * TCG always implement/can't be user disabled,
     * based on spec version.
     */
    bool has_priv_1_13;
    bool has_priv_1_12;
    bool has_priv_1_11;

    /* Vendor-specific custom extensions */
    bool ext_xtheadba;
    bool ext_xtheadbb;
    bool ext_xtheadbs;
    bool ext_xtheadcmo;
    bool ext_xtheadcondmov;
    bool ext_xtheadcei;
    bool ext_xtheadcef;
    bool ext_xtheadcev;
    bool ext_xtheadfmemidx;
    bool ext_xtheadfmv;
    bool ext_xtheadmac;
    bool ext_xtheadmemidx;
    bool ext_xtheadmempair;
    bool ext_xtheadsync;
    bool ext_xtheadvector;
    bool ext_xtheadisr;
    bool ext_xtheadmaee;
    bool ext_xtheadpbmt;
    bool ext_xtheadvdot;
    bool ext_XVentanaCondOps;
    bool ext_matrix;
    bool ext_xtheadvsfa;
    bool ext_xtheadlpw;
    bool ext_xtheadvfcvt;
    bool ext_xtheadvfreduction;
    bool ext_xtheadfastm;
    bool ext_xtheadvcrypto;
    bool ext_xtheadvcoder;
    bool ext_xtheadvarith;
    bool ext_xtheadcbop;

    uint32_t pmu_mask;
    uint16_t vlenb;
    uint16_t elen;
    uint16_t mrowlen;
    uint16_t cbom_blocksize;
    uint16_t cbop_blocksize;
    uint16_t cboz_blocksize;
    bool mmu;
    bool pmp;
    bool debug;
    bool misa_w;

    bool short_isa_string;
    bool frac_elen_check;
    dsa_disasm_fn dsa_disasm;

#ifndef CONFIG_USER_ONLY
    RISCVSATPMap satp_mode;
    uint32_t ext_mtvec;
    uint32_t ext_mtvt;
    uint32_t ext_jvt;
#endif
};

typedef struct RISCVCPUConfig RISCVCPUConfig;

/* Helper functions to test for extensions.  */

static inline bool always_true_p(const RISCVCPUConfig *cfg __attribute__((__unused__)))
{
    return true;
}

static inline bool has_xthead_p(const RISCVCPUConfig *cfg)
{
    return cfg->ext_xtheadba || cfg->ext_xtheadbb ||
           cfg->ext_xtheadbs || cfg->ext_xtheadcmo ||
           cfg->ext_xtheadcondmov ||
           cfg->ext_xtheadfmemidx || cfg->ext_xtheadfmv ||
           cfg->ext_xtheadmac || cfg->ext_xtheadmemidx ||
           cfg->ext_xtheadmempair || cfg->ext_xtheadsync ||
           cfg->ext_xtheadisr || cfg->ext_xtheadmaee ||
           cfg->ext_xtheadvdot ||
           cfg->ext_xtheadvsfa || cfg->ext_xtheadlpw ||
           cfg->ext_xtheadvfcvt || cfg->ext_xtheadvfreduction ||
           cfg->ext_xtheadfastm || cfg->ext_xtheadpbmt ||
           cfg->ext_xtheadvcrypto || cfg->ext_xtheadvcoder ||
           cfg->ext_xtheadvarith || cfg->ext_xtheadcbop;

}

#define MATERIALISE_EXT_PREDICATE(ext) \
    static inline bool has_ ## ext ## _p(const RISCVCPUConfig *cfg) \
    { \
        return cfg->ext_ ## ext ; \
    }

MATERIALISE_EXT_PREDICATE(xtheadba)
MATERIALISE_EXT_PREDICATE(xtheadbb)
MATERIALISE_EXT_PREDICATE(xtheadbs)
MATERIALISE_EXT_PREDICATE(xtheadcmo)
MATERIALISE_EXT_PREDICATE(xtheadcondmov)
MATERIALISE_EXT_PREDICATE(xtheadfmemidx)
MATERIALISE_EXT_PREDICATE(xtheadfmv)
MATERIALISE_EXT_PREDICATE(xtheadmac)
MATERIALISE_EXT_PREDICATE(xtheadmemidx)
MATERIALISE_EXT_PREDICATE(xtheadmempair)
MATERIALISE_EXT_PREDICATE(xtheadsync)
MATERIALISE_EXT_PREDICATE(xtheadvector)
MATERIALISE_EXT_PREDICATE(xtheadvdot)
MATERIALISE_EXT_PREDICATE(XVentanaCondOps)
MATERIALISE_EXT_PREDICATE(xtheadvsfa)
MATERIALISE_EXT_PREDICATE(xtheadlpw)
MATERIALISE_EXT_PREDICATE(xtheadvfcvt)
MATERIALISE_EXT_PREDICATE(xtheadvfreduction)
MATERIALISE_EXT_PREDICATE(xtheadfastm)
MATERIALISE_EXT_PREDICATE(xtheadpbmt)
MATERIALISE_EXT_PREDICATE(xtheadvcrypto)
MATERIALISE_EXT_PREDICATE(xtheadvcoder)
MATERIALISE_EXT_PREDICATE(xtheadvarith)
MATERIALISE_EXT_PREDICATE(xtheadcbop)
#endif
