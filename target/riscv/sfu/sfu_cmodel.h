#include <vector>
#include <iostream>
#include <iostream>
#include <cmath>
#include <iomanip>
#include <limits>
#include <fstream>
#include <float.h>
#include <cstring>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
//#include "/guesthome/damo/ctl/dm_qichen.zhang/Documents/softfloat_3d/SoftFloat-3d/source/include/specialize.h"
//#include "/guesthome/damo/ctl/dm_qichen.zhang/Documents/softfloat_3d/SoftFloat-3d/source/include/internals.h"
//#include "internals.h"

extern "C" {
	#include "softfloat.h"
}
using namespace std;  
#define T_D 31
#define P_D 22
#define Q_D 15
// T not Decimal
#define T_N_D 2
#define P_N_D 2
#define Q_N_D 1
// DEC_FP32
#define DEC_FP32 23
// EXP_FP32
#define EXP_FP32 8
#define MAX_POS 40 
#define X2_2 40
#define BOOTH_X2 10
#define BOOTH_X2X2 17
// exception
#define NV 16
#define DZ  8
#define OF  4
#define UF  2
#define NX  1
// special value
#define _INF 0x7f800000
#define _NINF 0xff800000
#define _ZERO 0x00000000
#define _NZERO 0x80000000
#define CNAN 0x7fc00000
#define MIN 0xff7fffff
#define ONE 0x3F800000
#define _NONE 0xBF800000
#define HALF 0x3F000000
#define EXP_DENORMAL 0x3f800001
#define EXP_NEAR_ONE 0x3f800000
// function
#define EXP2 1
#define RCP 8
#define TANH 2
#define SIGMOID 4
#define _C1 0
#define _C2 1

vector<double> fixbin2fixdec(vector<string> fix_number_vec);
unsigned long long square_cut_6_bit(unsigned long int x2);
//double square_cut_5_bit(unsigned long int x2);
//long int cut_bit(long int x, int cut_num);
int64_t cut_bit(int64_t x, int cut_num);
//double cut_bit(long int x, int cut_num);
int PP(int x_3);
//vector<int> booth_transform(unsigned long long mul);
int *booth_transform_c1(unsigned long long mul);
int *booth_transform_c2(unsigned long long mul);
//long int booth_mul(long mul_0, int *mul_1, int weight, int pos_c, int c1_or_c2);
int64_t booth_mul(int64_t mul_0, int *mul_1, int weight, int pos_c, int c1_or_c2);
//long int booth_mul_c1(long mul_0, int *mul_1, int weight, int pos_c);
//long int booth_mul_c2(long mul_0, int *mul_1, int weight, int pos_c);
//double booth_mul(long mul_0, int *mul_1, int weight, int pos_c, int c1_or_c2);
//long double booth_mul(long mul_0, vector<int> mul_1, int weight, int c1_or_c2);
float double_to_float_softfloat(float64_t result_double, uint_fast8_t round_mode);
float double_to_float(double result_double, uint_fast8_t round_mode);

uint64_t to_uint64(double x);
float to_float(uint32_t x);
unsigned int to_unsigned_int(float x);

static int PP_out_c1[100];
static int PP_out_c2[170];
/*
static int ex3_c1x2_pp[100];
static int ex3_c2x2x2_pp[170];

static int ex3_c1x2_pp_A_sign[100];
static int ex3_c2x2x2_pp_A_sign[170];

static int ex3_c1x2_pp_sign[100];
static int ex3_c2x2x2_pp_sign[170];
*/

struct sfu_output{
    float sfu_data_output;
    float sfu_err_output;
    int sfu_exception_output;
    long int sfu_booth_output;
};
sfu_output sfu_cmodel(float a_float, int opcode, bool debug);
//sfu_output sfu_cmodel(int a_int, int opcode, bool debug);
