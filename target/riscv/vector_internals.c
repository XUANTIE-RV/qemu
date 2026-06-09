/*
 * RISC-V Vector Extension Internals
 *
 * Copyright (c) 2020 T-Head Semiconductor Co., Ltd. All rights reserved.
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
#include "vector_internals.h"

/* set agnostic elements to 1s */
void vext_set_elems_1s(void *base, uint32_t is_agnostic, uint32_t cnt,
                       uint32_t tot)
{
    if (is_agnostic == 0) {
        /* policy undisturbed */
        return;
    }
    if (tot - cnt == 0) {
        return ;
    }

    if (HOST_BIG_ENDIAN) {
        /*
         * Deal the situation when the elements are insdie
         * only one uint64 block including setting the
         * masked-off element.
         */
        if (((tot - 1) ^ cnt) < 8) {
            memset(base + H1(tot - 1), -1, tot - cnt);
            return;
        }
        /*
         * Otherwise, at least cross two uint64_t blocks.
         * Set first unaligned block.
         */
        if (cnt % 8 != 0) {
            uint32_t j = ROUND_UP(cnt, 8);
            memset(base + H1(j - 1), -1, j - cnt);
            cnt = j;
        }
        /* Set other 64bit aligend blocks */
    }
    memset(base + cnt, -1, tot - cnt);
}

/* set agnostic fp6 elements to 1s */
void vext_set_fp6_1s(void *base, uint32_t is_agnostic,
                     uint32_t index, uint32_t tot)
{
    if (is_agnostic == 0) {
        /* policy undisturbed */
        return;
    }
    /* If first tail element start from a new byte */
    if (index * 6 % 8 == 0) {
        vext_set_elems_1s(base, 1, index * 6 / 8, tot);
    } else {
        /* First process the tail element bits in last body byte */
        uint8_t last_body_byte = *((uint8_t *)base + H1(index * 6 / 8));
        int start_pos = index * 6 % 8;
        int width = 8 - start_pos;
        last_body_byte = deposit64(last_body_byte, start_pos, width, -1);
        *((uint8_t *)base + H1(index * 6 / 8)) = last_body_byte;
        vext_set_elems_1s(base, 1, index * 6 / 8 + 1, tot);
    }
}

static inline bool crossbyte(int32_t index)
{
    return (index * 6) % 8 > (8 - 6);
}

static void process_crossbyte(void *base, int index, uint8_t val)
{
    /* Find the position of first byte and bit position */
    uint8_t first_byte = *((uint8_t *)base + H1(index * 6 / 8));
    uint8_t second_byte = *((uint8_t *)base + H1(index * 6 / 8 + 1));
    int start_pos = index * 6 % 8;
    int width = 8 - start_pos;
    first_byte = deposit64(first_byte, start_pos, width, val);
    *((uint8_t *)base + H1(index * 6 / 8)) = first_byte;
    /* Find the position of second byte and bit position*/
    second_byte = deposit64(second_byte, 0, 6 - width, val >> width);
    *((uint8_t *)base + H1(index * 6 / 8 + 1)) = second_byte;
}

static void process_onebyte(void *base, int32_t index, uint8_t val)
{
    uint8_t first_byte = *((uint8_t *)base + H1(index * 6 / 8));
    int start_pos = index * 6 % 8;
    first_byte = deposit64(first_byte, start_pos, 6, val);
    *((uint8_t *)base + H1(index * 6 / 8)) = first_byte;
}

/* set agnostic elements to 1s */
void vext_set_fp6_1s_by_index(void *base, uint32_t is_agnostic, int index)
{
    if (is_agnostic == 0) {
        /* policy undisturbed */
        return;
    }
    if (crossbyte(index)) {
        process_crossbyte(base, index, -1);
    } else {
        process_onebyte(base, index, -1);
    }
}

/* set fp6 element */
void vext_set_fp6_elem(void *base, int index, uint8_t val, CPURISCVState *env)
{
    if (crossbyte(index)) {
        process_crossbyte(base, index, val);
    } else {
        process_onebyte(base, index, val);
    }
}

static uint8_t get_crossbyte(void *base, int index)
{
    /* Find the position of first byte and bit position */
    uint8_t first_byte = *((uint8_t *)base + H1(index * 6 / 8));
    uint8_t second_byte = *((uint8_t *)base + H1(index * 6 / 8 + 1));
    uint8_t ret = 0;

    int start_pos = index * 6 % 8;
    int width = 8 - start_pos;
    ret = extract64(first_byte, start_pos, width);
    /* Find the position of second byte and bit position*/
    ret |= extract64(second_byte, 0, 6 - width) << width;
    return ret;
}

static uint8_t get_onebyte(void *base, int32_t index)
{
    uint8_t first_byte = *((uint8_t *)base + H1(index * 6 / 8));
    int start_pos = index * 6 % 8;
    return extract64(first_byte, start_pos, 6);
}

/* get fp6 element */
uint8_t vext_get_fp6_elem(void *base, int index, CPURISCVState *env)
{
    if (crossbyte(index)) {
        return get_crossbyte(base, index);
    } else {
        return get_onebyte(base, index);
    }
}

void do_vext_vv(void *vd, void *v0, void *vs1, void *vs2,
                CPURISCVState *env, uint32_t desc,
                opivv2_fn *fn, uint32_t esz)
{
    uint32_t vm = vext_vm(desc);
    uint32_t vl = env->vl;
    uint32_t total_elems = vext_get_total_elems(env, desc, esz);
    uint32_t vta = vext_vta(desc);
    uint32_t vma = vext_vma(desc);
    uint32_t i;

    VSTART_CHECK_EARLY_EXIT(env);

    for (i = env->vstart; i < vl; i++) {
        if (!vm && !vext_elem_mask(v0, i)) {
            /* set masked-off elements to 1s */
            vext_set_elems_1s(vd, vma, i * esz, (i + 1) * esz);
            continue;
        }
        fn(vd, vs1, vs2, i);
    }
    env->vstart = 0;
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vta, vl * esz, total_elems * esz);
}

void do_vext_vx(void *vd, void *v0, target_long s1, void *vs2,
                CPURISCVState *env, uint32_t desc,
                opivx2_fn fn, uint32_t esz)
{
    uint32_t vm = vext_vm(desc);
    uint32_t vl = env->vl;
    uint32_t total_elems = vext_get_total_elems(env, desc, esz);
    uint32_t vta = vext_vta(desc);
    uint32_t vma = vext_vma(desc);
    uint32_t i;

    VSTART_CHECK_EARLY_EXIT(env);

    for (i = env->vstart; i < vl; i++) {
        if (!vm && !vext_elem_mask(v0, i)) {
            /* set masked-off elements to 1s */
            vext_set_elems_1s(vd, vma, i * esz, (i + 1) * esz);
            continue;
        }
        fn(vd, s1, vs2, i);
    }
    env->vstart = 0;
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vta, vl * esz, total_elems * esz);
}
