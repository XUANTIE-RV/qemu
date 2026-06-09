#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

uint32_t f32_e4[2][8] = {
    //{ +0, -0, +inf, -inf, +max_e4m3, -max_e4m3, big_than_+max_e4m3, small_than_-max_e4m3 },
    { 0, 0x80000000, 0x7f800000, 0xff800000, 0x43e00000, 0xc3e00000, 0x43f00000, 0xc3f00000 },
    //{ +min_e4m3, -min_e4m3, small_than_+min_e4m3, small_than_-min_e4m3, cnan, snan, 1, -1}
    { 0x3b000000, 0xbb000000, 0x3a000000, 0xba000000, 0x7fc00000, 0x7f800001, 0x3f800000, 0xbf800000}
};

uint8_t e4_sat[2][8] = {
    //{ +0, -0, +inf, -inf, +max_e4m3, -max_e4m3, big_than_+max_e4m3, small_than_-max_e4m3 },
    { 0, 0x80, 0x7e, 0xfe, 0x7e, 0xfe, 0x7e, 0xfe },
    //{ +min_e4m3, -min_e4m3, small_than_+min_e4m3, small_than_-min_e4m3, cnan, snan, 1, -1}
    { 1, 0x81, 0, 0x80, 0x7f, 0x7f, 0x38, 0xb8}
};

uint8_t e4[2][8] = {
    //{ +0, -0, +inf, -inf, +max_e4m3, -max_e4m3, big_than_+max_e4m3, small_than_-max_e4m3 },
    { 0, 0x80, 0x7f, 0x7f, 0x7e, 0xfe, 0x7f, 0x7f },
    //{ +min_e4m3, -min_e4m3, small_than_+min_e4m3, small_than_-min_e4m3, cnan, snan, 1, -1}
    { 1, 0x81, 0, 0x80, 0x7f, 0x7f, 0x38, 0xb8}
};

uint32_t f32_e5[2][8] = {
    //{ +0, -0, +inf, -inf, +max_e5m2, -max_e5m2, big_than_+max_e5m2, small_than_-max_e5m2 },
    { 0, 0x80000000, 0x7f800000, 0xff800000, 0x47600000, 0xc7600000, 0x47700000, 0xc7700000 },
    //{ +min_e5m2, -min_e5m2, small_than_+min_e5m2, small_than_-min_e5m2, cnan, snan, 1, -1}
    { 0x37800000, 0xb7800000, 0x37000000, 0xb7000000, 0x7fc00000, 0x7f800001, 0x3f800000, 0xbf800000}
};

uint8_t e5_sat[2][8] = {
    //{ +0, -0, +inf, -inf, +max_e5m2, -max_e5m2, big_than_+max_e5m2, small_than_-max_e5m2 },
    { 0, 0x80, 0x7b, 0xfb, 0x7b, 0xfb, 0x7b, 0xfb },
    //{ +min_e5m2, -min_e5m2, small_than_+min_e5m2, small_than_-min_e5m2, cnan, snan, 1, -1}
    { 1, 0x81, 0, 0x80, 0x7f, 0x7f, 0x3c, 0xbc}
};

uint8_t e5[2][8] = {
    //{ +0, -0, +lfn, -lfn, +max_e5m2, -max_e5m2, max_e5m2, -max_e5m2 },
    { 0, 0x80, 0x7c, 0xfc, 0x7b, 0xfb, 0x7c, 0xfc },
    //{ +min_e5m2, -min_e5m2, small_than_+min_e5m2, small_than_-min_e5m2, cnan, snan, 1, -1}
    { 1, 0x81, 0, 0x80, 0x7f, 0x7f, 0x3c, 0xbc}
};

uint32_t f32_e8[2][8] = {
    //{ +0, -0, +inf, -inf, +max_e8m0, -max_e8m0, big_than_+max_e8m0, small_than_-max_e8m0 },
    { 0, 0x80000000, 0x7f800000, 0xff800000, 0x7f000000, 0xff000000, 0x7f400000, 0xff400000 },
    //{ +big_than_min_e8m0, -big_than_min_e8m0, small_than_+min_e8m0, small_than_-min_e8m0, cnan, snan, 1, -1}
    { 0x00800000, 0x80800000, 0x1, 0x80000001, 0x7fc00000, 0x7f800001, 0x3f800000, 0xbf800000}
};

uint8_t e8[2][8] = {
    //{ +0, -0, +inf, -inf, +max_e8m0, -max_e8m0, big_than_+max_e8m0, small_than_-max_e8m0 },
    { 0, 0x0, 0xfe, 0x0, 0xfe, 0x0, 0xfe, 0x0 },
    //{ +big_than_min_e8m0, -big_than_min_e8m0, small_than_+min_e8m0, small_than_-min_e8m0, cnan, snan, 1, -1}
    { 1, 0, 0, 0, 0xff, 0xff, 0x7f, 0x0}
};

/* Todo: we should also check flags */
static void do_vfncvt_f_f_q_e5_test(const char *pass_str, const char *fail_str)
{
    uint8_t e5_result[2][8] = {0};
    __asm__ volatile ("vsetvli t0, x0, e32, m2, ta, mu\n\t"
                      "vle32.v v2, (%[f32_e5])\n\t"
                      "vmv.v.x v4, x0\n\t"
                      "li t0, 16\n\t"
                      "vsetvli t0, t0, e8alt, mf2, ta, ma\n\t"
                      "th.vfncvt.f.f.q v4, v2\n\t"
                      "vse8.v v4, (%[e5_result])\n\t"
                      :: [f32_e5] "r"(f32_e5), [e5_result] "r"(e5_result)
                      : "v2", "t0", "v4", "memory"); 
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 8; j++) {
            if (e5_result[i][j] !=  e5[i][j]) {
                printf("f32[%d][%d]: 0x%x, expect e5: 0x%0x, result: 0x%x ",
                        i, j, f32_e5[i][j], e5[i][j], e5_result[i][j]);
                printf("%s\n", fail_str);
                return;
            }
        }
    }
    printf("%s\n", pass_str);
}

static void test_vfncvt_f_f_q_e5()
{
    do_vfncvt_f_f_q_e5_test("1. test vfncvt.f.f.q e5 nosat pass", 
                            "1. test vfncvt.f.f.q e5 nosat fail");
}

static void do_vfncvt_f_f_q_e5_sat_test(const char *pass_str, const char *fail_str)
{
    uint8_t e5_result[2][8] = {0};
    __asm__ volatile ("vsetvli t0, x0, e32, m2, ta, mu\n\t"
                      "vle32.v v2, (%[f32_e5])\n\t"
                      "vmv.v.x v4, x0\n\t"
                      "li t0, 16\n\t"
                      "vsetvli t0, t0, e8alt, mf2, ta, ma\n\t"
                      "th.vfncvt.sat.f.f.q v4, v2\n\t"
                      "vse8.v v4, (%[e5_result])\n\t"
                      :: [f32_e5] "r"(f32_e5), [e5_result] "r"(e5_result)
                      : "v2", "t0", "v4", "memory"); 
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 8; j++) {
            if (e5_result[i][j] !=  e5_sat[i][j]) {
                printf("f32[%d][%d]: 0x%x, expect e5: 0x%0x, result: 0x%x ",
                        i, j, f32_e5[i][j], e5_sat[i][j], e5_result[i][j]);
                printf("%s\n", fail_str);
                return;
            }
        }
    }
    printf("%s\n", pass_str);
}

static void test_vfncvt_f_f_q_e5_sat()
{
    do_vfncvt_f_f_q_e5_sat_test("2. test vfncvt.f.f.q e5 sat pass", 
                                "2. test vfncvt.f.f.q e5 sat fail");
}

static void do_vfncvt_f_f_q_e4_test(const char *pass_str, const char *fail_str)
{
    uint8_t e4_result[2][8] = {0};
    __asm__ volatile ("vsetvli t0, x0, e32, m2, ta, mu\n\t"
                      "vle32.v v2, (%[f32_e4])\n\t"
                      "vmv.v.x v4, x0\n\t"
                      "li t0, 16\n\t"
                      "vsetvli t0, t0, mf2, ta, ma\n\t"
                      "th.vfncvt.f.f.q v4, v2\n\t"
                      "vse8.v v4, (%[e4_result])\n\t"
                      :: [f32_e4] "r"(f32_e4), [e4_result] "r"(e4_result)
                      : "v2", "t0", "v4", "memory"); 
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 8; j++) {
            if (e4_result[i][j] !=  e4[i][j]) {
                printf("f32[%d][%d]: 0x%x, expect e4: 0x%0x, result: 0x%x ",
                        i, j, f32_e4[i][j], e4[i][j], e4_result[i][j]);
                printf("%s\n", fail_str);
                return;
            }
        }
    }
    printf("%s\n", pass_str);
}

static void test_vfncvt_f_f_q_e4()
{
    do_vfncvt_f_f_q_e4_test("3. test vfncvt.f.f.q e4 nosat pass", 
                            "3. test vfncvt.f.f.q e4 nosat fail");
}

static void do_vfncvt_f_f_q_e4_sat_test(const char *pass_str, const char *fail_str)
{
    uint8_t e4_result[2][8] = {0};
    __asm__ volatile ("vsetvli t0, x0, e32, m2, ta, mu\n\t"
                      "vle32.v v2, (%[f32_e4])\n\t"
                      "vmv.v.x v4, x0\n\t"
                      "li t0, 16\n\t"
                      "vsetvli t0, t0, mf2, ta, ma\n\t"
                      "th.vfncvt.sat.f.f.q v4, v2\n\t"
                      "vse8.v v4, (%[e4_result])\n\t"
                      :: [f32_e4] "r"(f32_e4), [e4_result] "r"(e4_result)
                      : "v2", "t0", "v4", "memory"); 
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 8; j++) {
            if (e4_result[i][j] !=  e4_sat[i][j]) {
                printf("f32[%d][%d]: 0x%x, expect e4: 0x%0x, result: 0x%x ",
                        i, j, f32_e4[i][j], e4_sat[i][j], e4_result[i][j]);
                printf("%s\n", fail_str);
                return;
            }
        }
    }
    printf("%s\n", pass_str);
}

static void test_vfncvt_f_f_q_e4_sat()
{
    do_vfncvt_f_f_q_e4_sat_test("4. test vfncvt.f.f.q e4 sat pass", 
                                "4. test vfncvt.f.f.q e4 sat fail");
}

static void do_vfncvt_f_f_q_e8_test(const char *pass_str, const char *fail_str)
{
    uint8_t e8_result[2][8] = {0};
    __asm__ volatile ("vsetvli t0, x0, e32, m2, ta, mu\n\t"
                      "vle32.v v2, (%[f32_e8])\n\t"
                      "vmv.v.x v4, x0\n\t"
                      "li t0, 16\n\t"
                      "vsetvli t0, t0, mf2, ta, ma\n\t"
                      "th.vfncvt.e8m0.f.q v4, v2\n\t"
                      "vse8.v v4, (%[e8_result])\n\t"
                      :: [f32_e8] "r"(f32_e8), [e8_result] "r"(e8_result)
                      : "v2", "t0", "v4", "memory"); 
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 8; j++) {
            if (e8_result[i][j] !=  e8[i][j]) {
                printf("f32[%d][%d]: 0x%x, expect e8: 0x%0x, result: 0x%x ",
                        i, j, f32_e8[i][j], e8[i][j], e8_result[i][j]);
                printf("%s\n", fail_str);
                return;
            }
        }
    }
    printf("%s\n", pass_str);
}

static void test_vfncvt_f_f_q_e8()
{
    do_vfncvt_f_f_q_e8_test("5. test vfncvt.f.f.q e8 nosat pass", 
                            "5. test vfncvt.f.f.q e8 nosat fail");
}

int main(void)
{
    printf("Test: xtheadvfofp8min \n");
    /* 1. test vfncvt.f.f.q e5 nosat */
    test_vfncvt_f_f_q_e5();
    /* 2. test vfncvt.f.f.q e5 sat */
    test_vfncvt_f_f_q_e5_sat();
    /* 3. test vfncvt.f.f.q e4 nosat */
    test_vfncvt_f_f_q_e4();
    /* 4. test vfncvt.f.f.q e4 sat */
    test_vfncvt_f_f_q_e4_sat();
    /* 5. test vfncvt.f.f.q e4 sat */
    test_vfncvt_f_f_q_e8();
    /* Fixme: check every test case return value */
    printf("Pass xtheadvfofp8min\n");
    return 0;
}
