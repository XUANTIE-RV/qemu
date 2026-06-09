#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* e4m3 16x64 1.0 */
uint8_t ma[16][64];
/* e2m1 16x64 1.0 */
uint8_t mb[16][64];

union result_t {
    uint32_t u32;
    float f32;
};
/* fp32 16x16 1.0 */
union result_t mc[16][16];

uint32_t a_blocksize = 32;
uint32_t b_blocksize = 32;

/* ma_s 16x64 1.0 */
uint8_t ma_s[16][64];
/* mb_s 16x64 2.0 */
uint8_t mb_s[16][64];

static void do_mfmacc_mx_test(const char *pass_str, const char *fail_str,
                              uint32_t sizek, union result_t mc[][16],
                              union result_t md[][16])
{
    int i, j;
    /* Fill ma/mb/ma_s/mb_s */
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 64; j++) {
            /* e4m3 1.0->0b00111000=0x38 */
            ma[i][j] = 0x38;
            /* e2m1 1.0->0b0010=0x2 */
            mb[i][j] = 0x22;
            /* e8m0 1.0->0b01111111=0x7f */
            ma_s[i][j] = 0x7f;
            /* e8m0 2.0->0b10000000=0x80 */
            mb_s[i][j] = 0x80;
        }
    }
    /* Load to matrix registers */
    uint64_t stride = 64;
    asm("th.mcfgmi 16\n\t"
        "th.mcfgni 16\n\t"
        "th.mcfgki 64\n\t"
        "th.mlde8 m0, %[stride], (%[ma_ptr])\n\t"
        "th.mlde8 m1, %[stride], (%[ma_s_ptr])\n\t"
        "th.mlde8 m2, %[stride], (%[mb_ptr])\n\t"
        "th.mlde8 m3, %[stride], (%[mb_s_ptr])\n\t"
        "th.mlde32 m4, %[stride], (%[mc_ptr])\n\t"
        "th.mxcfg.blksize %[a_blk], mxa\n\t"
        "th.mxcfg.blksize %[b_blk], mxb\n\t"
        "th.mxcfgi.colidx 0, mxa\n\t"
        "th.mxcfgi.colidx 32, mxb\n\t"
        "th.mcfgk %[sizek]\n\t"
        "th.mfmacc.s.mxe4m3mxe2m1 m4, m2[1], m3, m0, m1\n\t"
        "th.mste32 m4, %[stride], (%[mc_ptr])\n\t"
        :: [stride] "r"(stride), [ma_ptr] "r"(ma), [mb_ptr] "r"(mb),
           [ma_s_ptr] "r"(ma_s), [mb_s_ptr] "r"(mb_s), [mc_ptr] "r"(mc),
           [a_blk] "r"(a_blocksize), [b_blk] "r"(b_blocksize),
           [sizek] "r"(sizek)
          :);
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j++) {
            if (mc[i][j].f32 !=  md[i][j].f32) {
                printf("mc[%d][%d]: %0.2f, md: %0.2f\n", i, j, mc[i][j].f32,
                       md[i][j].f32);
                printf("%s\n", fail_str);
                return;
            }
        }
    }
    printf("%s\n", pass_str);
}

static void sizek_mod_k1_0(const char *pass_str, const char *fail_str)
{
    int i, j;
    /* Expect */
    union result_t md[16][16];
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j++) {
           md[i][j].f32 = 129.0f;
           mc[i][j].f32 = 1.0f;
        }
    }
    do_mfmacc_mx_test(pass_str, fail_str, 64, mc, md);
}

static void test_mxa_eq_mxb(void)
{
    sizek_mod_k1_0("1. test_mxa_eq_mxb pass", "1. test_mxa_eq_mxb fail");
}

static void test_mxa_eq_2mxb(void)
{
    a_blocksize = 64;
    b_blocksize = 32;
    sizek_mod_k1_0("2. test_mxa_eq_2mxb pass", "2. test_mxa_eq_2mxb fail");
}

static void test_mxb_eq_2mxa(void)
{
    a_blocksize = 32;
    b_blocksize = 64;
    sizek_mod_k1_0("3. test_mxb_eq_2mxa pass", "3. test_mxb_eq_2mxa fail");
}

static void sizek_mod_k1_31(const char *pass_str, const char *fail_str)
{
    int i, j;
    /* Expect */
    union result_t md[16][16];
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j++) {
            if (j == 15) {
                md[i][j].f32 = 2.0f;
                mc[i][j].f32 = 2.0f;
            } else {
                md[i][j].f32 = 127.0f;
                mc[i][j].f32 = 1.0f;
            }
        }
    }
    do_mfmacc_mx_test(pass_str, fail_str, 63, mc, md);
   }

static void test_mxa_eq_mxb_k31(void)
{
    a_blocksize = 32;
    b_blocksize = 32;
    sizek_mod_k1_31("1. test_mxa_eq_mxb pass", "1. test_mxa_eq_mxb fail");
}

static void test_mxa_eq_2mxb_k31(void)
{
    a_blocksize = 64;
    b_blocksize = 32;
    sizek_mod_k1_31("2. test_mxa_eq_2mxb pass", "2. test_mxa_eq_2mxb fail");
}

static void test_mxb_eq_2mxa_k31(void)
{
    a_blocksize = 32;
    b_blocksize = 64;
    sizek_mod_k1_31("3. test_mxb_eq_2mxa pass", "3. test_mxb_eq_2mxa fail");
}

int main(void)
{
    /* Test sizek % k1 == 0 */
    printf("Test: sizek % k1_blocksize == 0 \n");
    /* 1. Test mxa.blocksize == mxb.blocksize */
    test_mxa_eq_mxb();
    /* 2. Test mxa.blocksize = 2*mxb.blocksize */
    test_mxa_eq_2mxb();
    /* 3. Test mxb.blocksize = 2*mxa.blocksize */
    test_mxb_eq_2mxa();
    printf("Test: sizek % k1_blocksize == 31\n");
    /* 1. Test mxa.blocksize == mxb.blocksize */
    test_mxa_eq_mxb_k31();
    /* 2. Test mxa.blocksize = 2*mxb.blocksize */
    test_mxa_eq_2mxb_k31();
    /* 3. Test mxb.blocksize = 2*mxa.blocksize */
    test_mxb_eq_2mxa_k31();
    printf("Pass mxf8mxf4\n");
}
