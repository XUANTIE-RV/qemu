//#include "sfu_cmodel.h"
#include "LUT.h"
#ifdef HECTOR
    #include <Hector.h>
#endif
#ifdef JASPER_C
    #include <jasperc.h>
    #include <assert.h>
#endif
typedef unsigned long int ulong;
typedef unsigned short int ushort;
typedef unsigned int uint;
uint64_t to_uint64(double x) {
    uint64_t a = 0;
    ::memcpy(&a,&x,sizeof(x));
    return a;
}

float to_float(uint32_t x) {
    float a = 0;
    ::memcpy(&a,&x,sizeof(x));
    return a;
}

unsigned int to_unsigned_int(float x) {
    unsigned int a = 0;
    ::memcpy(&a,&x,sizeof(x));
    return a;
}

float double_to_float(float64_t result_double, uint_fast8_t round_mode){
    float64_t result_double_softfloat = {0};
    float32_t result_softfloat = {0};
    float result = 0;
    softfloat_roundingMode = round_mode;
    result_double_softfloat.v = (result_double.v);
    result_softfloat = f64_to_f32(result_double_softfloat);
    result = to_float(result_softfloat.v);

    return result;
}

union ui64_f64 { uint64_t ui; float64_t f; };
union ui32_f32 { uint32_t ui; float32_t f; };
uint64_t softfloat_shortShiftRightJam64( uint64_t a, uint_fast8_t dist )
    { return a>>dist | ((a & (((uint_fast64_t) 1<<dist) - 1)) != 0); }

uint32_t softfloat_shiftRightJam32( uint32_t a, uint_fast16_t dist )
{
    return
        (dist < 31) ? a>>dist | ((uint32_t) (a<<(-dist & 31)) != 0) : (a != 0);
}
struct commonNaN {
    bool sign;
    uint64_t v0, v64;
};

void softfloat_f64UIToCommonNaN( uint_fast64_t uiA, struct commonNaN *zPtr )
{
    zPtr->sign = uiA>>63;
    zPtr->v64  = uiA<<12;
    zPtr->v0   = 0;
}

uint_fast32_t softfloat_commonNaNToF32UI( const struct commonNaN *aPtr )
{
    return (uint_fast32_t) aPtr->sign<<31 | 0x7FC00000 | aPtr->v64>>41;
}
#define signF32UI( a ) ((bool) ((uint32_t) (a)>>31))
#define expF32UI( a ) ((int_fast16_t) ((a)>>23) & 0xFF)
#define fracF32UI( a ) ((a) & 0x007FFFFF)
#define packToF32UI( sign, exp, sig ) (((uint32_t) (sign)<<31) + ((uint32_t) (exp)<<23) + (sig))
#define signF64UI( a ) ((bool) ((uint64_t) (a)>>63))
#define expF64UI( a ) ((int_fast16_t) ((a)>>52) & 0x7FF)
#define fracF64UI( a ) ((a) & UINT64_C( 0x000FFFFFFFFFFFFF ))

THREAD_LOCAL  uint_fast8_t softfloat_roundingMode;

float32_t
softfloat_roundPackToF32( bool sign, int_fast16_t exp, uint_fast32_t sig )
{
    uint_fast8_t roundingMode;
    bool roundNearEven;
    uint_fast8_t roundIncrement, roundBits;
    uint_fast32_t uiZ;
    union ui32_f32 uZ;

    /*------------------------------------------------------------------------
     *------------------------------------------------------------------------*/
    roundingMode = softfloat_roundingMode;
    roundNearEven = (roundingMode == softfloat_round_near_even);
    roundIncrement = 0x40;
    if ( ! roundNearEven && (roundingMode != softfloat_round_near_maxMag) ) {
        roundIncrement =
            (roundingMode
             == (sign ? softfloat_round_min : softfloat_round_max))
            ? 0x7F
            : 0;
    }
    roundBits = sig & 0x7F;
    /*------------------------------------------------------------------------
     *------------------------------------------------------------------------*/
    if ( 0xFD <= (unsigned int) exp ) {
        if ( exp < 0 ) {
            /*----------------------------------------------------------------
             *----------------------------------------------------------------*/
            sig = softfloat_shiftRightJam32( sig, -exp );
            exp = 0;
            roundBits = sig & 0x7F;
        } else if ( (0xFD < exp) || (0x80000000 <= sig + roundIncrement) ) {
            /*----------------------------------------------------------------
             *----------------------------------------------------------------*/
            uiZ = packToF32UI( sign, 0xFF, 0 ) - ! roundIncrement;
            goto uiZ;
        }
    }
    /*------------------------------------------------------------------------
     *------------------------------------------------------------------------*/
    sig = (sig + roundIncrement)>>7;
    if ( roundBits ) {
#ifdef SOFTFLOAT_ROUND_ODD
        if ( roundingMode == softfloat_round_odd ) {
            sig |= 1;
            goto packReturn;
        }
#endif
    }
    sig &= ~(uint_fast32_t) (! (roundBits ^ 0x40) & roundNearEven);
    if ( ! sig ) exp = 0;
    /*------------------------------------------------------------------------
     *------------------------------------------------------------------------*/
packReturn:
    uiZ = packToF32UI( sign, exp, sig );
uiZ:
    uZ.ui = uiZ;
    return uZ.f;

}

float32_t f64_to_f32( float64_t a )
{
    union ui64_f64 uA;
    uint_fast64_t uiA;
    bool sign;
    int_fast16_t exp;
    uint_fast64_t frac;
    struct commonNaN commonNaN;
    uint_fast32_t uiZ, frac32;
    union ui32_f32 uZ;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    uA.f = a;
    uiA = uA.ui;
    sign = signF64UI( uiA );
    exp  = expF64UI( uiA );
    frac = fracF64UI( uiA );
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if ( exp == 0x7FF ) {
        if ( frac ) {
            softfloat_f64UIToCommonNaN( uiA, &commonNaN );
            uiZ = softfloat_commonNaNToF32UI( &commonNaN );
        } else {
            uiZ = packToF32UI( sign, 0xFF, 0 );
        }
        goto uiZ;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    frac32 = softfloat_shortShiftRightJam64( frac, 22 );
    if ( ! (exp | frac32) ) {
        uiZ = packToF32UI( sign, 0, 0 );
        goto uiZ;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    return softfloat_roundPackToF32( sign, exp - 0x381, frac32 | 0x40000000 );
 uiZ:
    uZ.ui = uiZ;
    return uZ.f;
}
float double_to_float_softfloat(double result_double, uint_fast8_t round_mode){
    float64_t result_double_softfloat = {0};
    float32_t result_softfloat = {0};
    float result = 0;
    softfloat_roundingMode = round_mode;
    result_double_softfloat.v = to_uint64(result_double);
    result_softfloat = f64_to_f32(result_double_softfloat);
    result = to_float(result_softfloat.v);

    return result;
}
//Booth partial product list
int PP(int x_3){
    int a = 0;
    switch(x_3){
        case 0:
            a = 0;
            break;
        case 1:
            a = 1;
            break;
        case 2:
            a = 1;
            break;
        case 3:
            a = 2;
            break;
        case 4:
            a = -2;
            break;
        case 5:
            a = -1;
            break;
        case 6:
            a = -1;
            break;
        case 7:
            a = -3;
            break;
    }
    return a;
}
//Booth partial product generator
int *booth_transform_c1(unsigned long long mul){
    long *p=(long *)&mul;
    long mul_0 = 0;

    mul_0 = (long)(*p);
    long mul_0_0 = (mul_0 & 0b11) << 1;
    *PP_out_c1 = PP(mul_0_0);
    long int a = 0b1110;
    long b = 0;
    //for (int i = 0; i < sizeof(long)*8/2; i++){
    for (int i = 0; i < 11; i++){
        b = (mul_0 & (a << (i*2))) >> (i*2+1);
	*(PP_out_c1 + i+1) = (PP(b));
    }
    return PP_out_c1;
}
//Booth partial product generator
int *booth_transform_c2(unsigned long long mul){
    long *p=(long *)&mul;
    long mul_0 = 0;

    mul_0 = (long)(*p);
    long mul_0_0 = (mul_0 & 0b11) << 1;
    *PP_out_c2 = PP(mul_0_0);
    long int a = 0b1110;
    long b = 0;
    //for (int i = 0; i < sizeof(long)*8/2; i++){
    for (int i = 0; i < 18; i++){
        b = (mul_0 & (a << (i*2))) >> (i*2+1);
	    *(PP_out_c2 + i+1) = (PP(b));
    }
    return PP_out_c2;
}
/*
//Booth multiplier with truncation, depending on difference function
long int booth_mul_c1(long mul_0, int *mul_1, int weight, int pos_c){
    long int result = 0;
    //double result = 0;
    long int result_1 = 0;
    //double result_1 = 0;
    int weight_first = 0;
    int position = 0;
    int position_com = 0;
    int position_first = 0;
    int booth_cut_num = 0;
    int sum = 0;
    int booth = 0;
    int mul_1_now = 0;
    int booth_pos = 0;

    weight_first = weight - BOOTH_X2 * 2;
	position_first = weight_first * -1 + P - P_N_D - MAX_POS;
	booth_cut_num = 13 - position_first;

	//printf("The position_first is %d\n", position_first);

        for (int i = 0; i < 11; i++){
            if (*(mul_1+i) >= 0)
                booth_pos = 1;
            else
                booth_pos = -1;
	            mul_1_now = *(mul_1 + i);
            if (mul_1_now == -3)
                booth = 0;
            else
                booth = mul_1_now;

            position = -(weight_first + 2*i) + P - P_N_D - MAX_POS;
            position_com = position+booth_cut_num;


	if (pos_c*booth_pos >=0){
	//if (mul_1_now * mul_0 > 0 || ( mul_0 > 0 && mul_1_now == 0)||(mul_0 == 0 && mul_1_now < 0) ){
            result_1 = cut_bit((mul_0 << (2*i)) * booth, position_first);
            result = result + result_1;
			*(ex3_c1x2_pp + i) = (mul_0 * booth) << booth_cut_num;
			*(ex3_c1x2_pp_A_sign + i) = 0;
			*(ex3_c1x2_pp_sign + i) = 0;
        } else{
            result_1 = ((long int)1  << position_first) +(cut_bit((mul_0 << (2*i)) * (-booth), position_first));
		    *(ex3_c1x2_pp + i) = ((mul_0 * booth) << booth_cut_num)-1;
		    *(ex3_c1x2_pp_A_sign + i) = 63;
		    *(ex3_c1x2_pp_sign + i) = 1;
            if (position > 0){
                result = result - result_1;
            }
            else{
                if (position_com > 0 ){
                    result = result - result_1;
                } else{
                    result = result - (cut_bit((mul_0 << (2*i)) * (-booth), position_first));
                }
            }
        }
    }
    return result;
}

//Booth multiplier with truncation, depending on difference function
long int booth_mul_c2(long mul_0, int *mul_1, int weight, int pos_c){
    long int result = 0;
    //double result = 0;
    long int result_1 = 0;
    //double result_1 = 0;
    int weight_first = 0;
    int position = 0;
    int position_com = 0;
    int position_first = 0;
    int booth_cut_num = 0;
    int sum = 0;
    int booth = 0;
    int mul_1_now = 0;
    int index = 0;
    int booth_pos = 0;
    weight_first = weight - BOOTH_X2X2 * 2;
	position_first = weight_first * -1 + Q - Q_N_D - MAX_POS;
    booth_cut_num = 34 - position_first;
	index = 18;

	//printf("The position_first is %d\n", position_first);

        for (int i = 0; i < 18; i++){
            if (*(mul_1+i) >= 0)
                booth_pos = 1;
            else
                booth_pos = -1;
	            mul_1_now = *(mul_1 + i);
            if (mul_1_now == -3)
                booth = 0;
            else
                booth = mul_1_now;
            position = -(weight_first + 2*i) + Q - Q_N_D - MAX_POS;
            position_com = position+booth_cut_num;


	if (pos_c*booth_pos >=0){
	//if (mul_1_now * mul_0 > 0 || ( mul_0 > 0 && mul_1_now == 0)||(mul_0 == 0 && mul_1_now < 0) ){
            result_1 = cut_bit((mul_0 << (2*i)) * booth, position_first);
            result = result + result_1;
			*(ex3_c2x2x2_pp + i) = (mul_0 * booth) << booth_cut_num;
			*(ex3_c2x2x2_pp_A_sign + i) = 0;
			*(ex3_c2x2x2_pp_sign + i) = 0;
        } else{
            result_1 = ((long int)1  << position_first) +(cut_bit((mul_0 << (2*i)) * (-booth), position_first));
		    *(ex3_c2x2x2_pp + i) = ((mul_0 * booth) << booth_cut_num)-1;
		    *(ex3_c2x2x2_pp_A_sign + i) = 63;
		    *(ex3_c2x2x2_pp_sign + i) = 1;
            if (position > 0){
                result = result - result_1;
            }
            else{
                if (position_com > 0 ){
                    result = result - result_1;
                } else{
                    result = result - (cut_bit((mul_0 << (2*i)) * (-booth), position_first));
                }
            }
        }
        }
    return result;
}
*/
//Booth multiplier with truncation, depending on difference function
int64_t booth_mul(int64_t mul_0, int *mul_1, int weight, int pos_c, int c1_or_c2){
    int64_t result = 0;
    //double result = 0;
    int64_t result_base = 0;
    int64_t result_base_neg = 0;
    int64_t result_1 = 0;
    int64_t result_temp[18];
    int64_t result_base_temp,result_base_temp_neg;
    //double result_1 = 0;
    int weight_first = 0;
    int position = 0;
    int position_com = 0;
    int position_first = 0;
    int booth_cut_num = 0;
    int sum = 0;
    int booth = 0;
    int mul_1_now = 0;
    int index = 0;
    int booth_pos = 0;
    int ex3_base;
    if (c1_or_c2 == _C1){
        //weight_first = weight - (BOOTH_X2 << 1);
        weight_first = weight - 20;
		position_first = -weight_first   + P_D - P_N_D - MAX_POS;
		booth_cut_num = 13 - position_first;
	    index = 11;
    }
    else if (c1_or_c2 == _C2){
        weight_first = weight - 34;
		position_first = -weight_first + Q_D - Q_N_D - MAX_POS;
        booth_cut_num = 34 - position_first;
	    index = 18;
    }
	//printf("The position_first is %d\n", position_first);
    #ifdef HECTOR
        uint position_first_sign = 0;
        if (position_first < 0)
            position_first_sign = 1;
        Hector::show("position_first_sign",position_first_sign);
    #endif

        for (int i = 0; i < index; i++){
            if (*(mul_1+i) >= 0)
                booth_pos = 1;
            else
                booth_pos = -1;
	            mul_1_now = *(mul_1 + i);
            if (mul_1_now == -3)
                booth = 0;
            else
                booth = mul_1_now;
    		//if (c1_or_c2 == C1)
            	//position = -(weight_first + (i<<1)) + P - P_N_D - MAX_POS;
            position = position_first - (i<<1);
			//else if (c1_or_c2 == C2)
            	//position = -(weight_first + (i<<1)) + Q - Q_N_D - MAX_POS;

            position_com = position+booth_cut_num;

    result_base_temp = (mul_0 << (i<<1)) * booth;
    result_base_temp_neg = -result_base_temp;

    //result_base =  cut_bit((mul_0 << (i<<1)) * booth, position_first);
    //result_base_neg =  cut_bit((mul_0 << (i<<1)) * (-booth), position_first);

    result_base =  cut_bit(result_base_temp, position_first);
    result_base_neg =  cut_bit(result_base_temp_neg, position_first);
    //ex3_base = (mul_0 * booth) << booth_cut_num;
	//if (pos_c*booth_pos >=0){
 	if (((pos_c >=0) && (booth_pos >=0)) || ((pos_c <0) && (booth_pos <0))){
            result_temp[i] = result_base;
            /*
            if (c1_or_c2 == C1){
			    *(ex3_c1x2_pp + i) = ex3_base;
			    *(ex3_c1x2_pp_A_sign + i) = 0;
			    *(ex3_c1x2_pp_sign + i) = 0;
		    }else{
			    *(ex3_c2x2x2_pp + i) = ex3_base;
			    *(ex3_c2x2x2_pp_A_sign + i) = 0;
			    *(ex3_c2x2x2_pp_sign + i) = 0;
		    }
            */
        } else{
            //result_1 = ((long int)1  << position_first) +(cut_bit((mul_0 << (i<<1)) * (-booth), position_first));
            result_1 = ((long int)1  << position_first) + result_base_neg;
            /*
            if (c1_or_c2 == C1){
		    	//*(ex3_c1x2_pp + i) = ((mul_0 * booth) << booth_cut_num)-1;
                *(ex3_c1x2_pp + i) = ex3_base -1;
		    	*(ex3_c1x2_pp_A_sign + i) = 63;
		    	*(ex3_c1x2_pp_sign + i) = 1;
		    }
		    else{
		    	*(ex3_c2x2x2_pp + i) = ex3_base -1;
		    	*(ex3_c2x2x2_pp_A_sign + i) = 63;
		    	*(ex3_c2x2x2_pp_sign + i) = 1;
		    }
            */
            if (position > 0){
                 result_temp[i] = -result_1;
            }
            else{
                if (position_com > 0 ){
                    result_temp[i] = -result_1;
                } else{
                    result_temp[i] = -result_base_neg;
                }
            }
        }
        result += result_temp[i];
    }
    return result;
}
//Generate square of input with 6 bits truncation
unsigned long long square_cut_6_bit(unsigned long int x2){
//double square_cut_6_bit(unsigned long int x2){
    unsigned long long x2_square = 0;
    x2_square = x2 * x2;

    unsigned long long x2_square_cut_6_bit = 0;
    char *p=(char *)&x2_square;
    *p = *p & 0b11000000;

    x2_square_cut_6_bit = x2_square >> 6;
    return (x2_square_cut_6_bit);
    //return double(x2_square_cut_6_bit);

}
//Generate responding output of input with cut_num truncation
int64_t cut_bit(int64_t x, int cut_num){
//double cut_bit(int64_t x, int cut_num){
    int64_t *p = (int64_t *)&x;
    int64_t a = ~0b0;
    int64_t result = 0;
    #ifdef HECTOR
        uint cut_num_sign = 0;
        if(cut_num<0) {
            cut_num_sign = 1;
        }
        Hector::show("cut_num_neg",cut_num_sign);
    #endif

    if (cut_num >= 0)
        result = *p & (a << cut_num);
    else
        result = x;
    return (result);
}
//Main function of SFU cmodel
//sfu_output sfu_cmodel(int a_int,int opcode, bool debug){
sfu_output sfu_cmodel(float a_float,int opcode, bool debug){
	unsigned int a_int = to_unsigned_int(a_float);
    int ex1_src0 = a_int;
    float *a_pointer =(float *)&a_int;
    float a = *a_pointer;
    float a_sig = 0;
    float special_sig_float = 0;
    int special_sig = 0;
    int opcode_new = SIGMOID;
    float a_new = 0;
    //double err = 0;
    //double golden = 0;
	//float golden_fp32 = 0;
    unsigned char float_745_1_input = 0;
    unsigned char float_745_2_input = 0;
    unsigned char float_745_3_input = 0;
    unsigned char float_745_4_input = 0;
    char *input_p=(char *)&a_int;
    float_745_1_input = (unsigned char)(*input_p);
    float_745_2_input = (unsigned char)(*(input_p+1));
    float_745_3_input = (unsigned char)(*(input_p+2));
    float_745_4_input = (unsigned char)(*(input_p+3));


    //golden_fp32 = double_to_float_softfloat(golden,softfloat_round_max);
    //char *q = (char *)&golden_fp32;
    unsigned char golden_3 = 0;
    unsigned char golden_4 = 0;
    int golden_exp = 0;

    // Input special value
    long int special_output = 0;
	int special = 0;
	int denormal;
    int fflags = 0;
    int exp_8 = (((float_745_4_input) & 0b1111111) << 1)  + ((float_745_3_input >> 7) & 0b1);
    int ex1_exponent = exp_8;
    int mantissa = (float_745_1_input) + (float_745_2_input << (8)) + (uint(float_745_3_input & 0b1111111) << (8+8));
    int ex1_mantissa = mantissa;
    int nan = (exp_8 == 255 && mantissa != 0);
    int qnan = (nan == 1) && (mantissa >= pow(2,22));
    int snan = (nan == 1) && (mantissa < pow(2,22));
    int cnan = (nan == 1) && (mantissa == pow(2,22));
    int denorm = exp_8 == 0 && mantissa != 0;
	// sigmoid, y = x/4 + 1/2
	unsigned int mantissa_sig_pos = 0;
    unsigned int mantissa_sig_neg = 0;

    if (exp_8 > 96){
        mantissa_sig_pos = ((unsigned int)(mantissa+ pow(2,23)) >> (128 - exp_8)) + pow(2,23);
        mantissa_sig_neg = pow(2,24) - 1 - ((unsigned int)(mantissa+ pow(2,23)) >> (127 - exp_8));
    }
    else{
        mantissa_sig_pos = pow(2,23);
        mantissa_sig_neg = pow(2,24)-1;
    }
    if (a >= 0)
        a_sig = mantissa_sig_pos / pow(2,23) * pow(2, -1);
    else
        a_sig = mantissa_sig_neg / pow(2,23) * pow(2, -2);


    // For formal input
    int ex1_is_inf = 0;
    int ex1_is_zero = 0;
    int ex1_is_denorm = 0;
    int ex1_is_qnan = 0;
    int ex1_is_snan = 0;

    float sig_range;
    sig_range = double_to_float_softfloat(double(23.0/16.0*a), softfloat_round_max);

    if (a_int == _NINF || a_int == _INF)
	ex1_is_inf = 1;
    if (a_int == _ZERO || a_int == _NZERO)
	ex1_is_zero = 1;
    ex1_is_denorm = denorm;
    ex1_is_qnan = qnan;
    ex1_is_snan = snan;

    #ifdef HECTOR
        Hector::show("ex1_src0",ex1_src0);
        Hector::show("a_int",a_int);
        Hector::show("a_float",a);
        Hector::show("ex1_exponent",ex1_exponent);
        Hector::show("ex1_mantissa",ex1_mantissa);
        Hector::show("ex1_is_inf",ex1_is_inf);
        Hector::show("ex1_is_zero",ex1_is_zero);
        Hector::show("ex1_is_denorm",ex1_is_denorm);
        Hector::show("ex1_is_qnan",ex1_is_qnan);
        Hector::show("ex1_is_snan",ex1_is_snan);
    #endif
    #ifdef JASPER_C
	    JG_OUTPUT(ex1_src0);
        JG_SHOW(opcode);
	    JG_OUTPUT(ex1_exponent);
	    JG_OUTPUT(ex1_mantissa);
	    JG_OUTPUT(ex1_is_inf);
	    JG_OUTPUT(ex1_is_zero);
	    JG_OUTPUT(ex1_is_denorm);
	    JG_OUTPUT(ex1_is_qnan);
	    JG_OUTPUT(ex1_is_snan);
    #endif

    if (opcode == EXP2){
        if ( a_int == _NINF){
            special_output = _ZERO;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
		else if (fpclassify( a ) == FP_SUBNORMAL ){
            special_output = EXP_NEAR_ONE;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if (a < -149){
            special_output = _ZERO;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if ( a >= -149 && a < -126){
            special = 0;
            denormal = 1;
            fflags = 0;
        }
		else if ( a >= -126 && a <= -1*pow(2,-23)){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
        else if ( a > -1*pow(2,-23) && a <-0){
            special_output = EXP_NEAR_ONE;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if ( a_int == _ZERO || a_int == _NZERO){
            special_output = ONE;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if ( a < 1*pow(2,-23) && a > 0){
            special_output = EXP_NEAR_ONE;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if ( a >= pow(2,-23) && a < 128){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
        else if ( a_int == _INF ){
            special_output = _INF;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if ( a >= 128 ){
            special_output = _INF;
            special = 1;
            denormal = 0;
            fflags = OF;
        }
        else if (snan == 1){
            special_output = CNAN;
            special = 1;
            denormal = 0;
            fflags = NV;
        }
        else if (qnan == 1){
            special_output = CNAN;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
    }
    if (opcode == RCP){
        if ( a_int == _NINF){
            special_output = _NZERO;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if (a > -pow(2,128) && a < -pow(2,126)){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
        else if ( a >= -pow(2,126) && a <= -pow(2,-126)){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
		else if ( a > -pow(2,-126) && a <= -pow(2,-1) * pow(2,-126)){
            special = 0;
            denormal = 2;
            fflags = 0;
        }
        else if ( a > -pow(2,-1) * pow(2,-126) && a < -pow(2,-2) * pow(2,-126)){
            special = 0;
            denormal = 2;
            fflags = 0;
        }
        else if ( a >=  -pow(2,-2) * pow(2,-126) && a < 0){
            special_output = MIN;
            special = 1;
            denormal = 0;
            fflags = OF;
        }
        else if ( a_int == _NZERO ){
            special_output = _NINF;
            special = 1;
            denormal = 0;
            fflags = DZ;
        }
        else if ( a_int == _ZERO ){
            special_output = _INF;
            special = 1;
            denormal = 0;
            fflags = DZ;
        }
		else if ( a <=  pow(2,-2) * pow(2,-126) && a > 0){
            special_output = _INF;
            special = 1;
            denormal = 0;
            fflags = OF;
        }
        else if ( a < pow(2,-126) && a > pow(2,-1) * pow(2,-126)){
            special = 0;
            denormal = 2;
            fflags = 0;
        }
        else if ( a <= pow(2,-1) * pow(2,-126) && a > pow(2,-2) * pow(2,-126)){
            special = 0;
            denormal = 2;
            fflags = 0;
        }
        else if ( a >= pow(2,-126) && a <= pow(2,126)){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
        else if (a > pow(2,126) && a < pow(2,128)){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
        else if ( a_int == _INF ){
            special_output = _ZERO;
            special = 1;
            denormal = 0;
        }
        else if (snan == 1){
            special_output = CNAN;
            special = 1;
            denormal = 0;
            fflags = NV;
        }
        else if (qnan == 1){
            special_output = CNAN;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
    }
    else if (opcode == TANH){
        if ( a_int == _NINF){
            special_output = _NONE;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if (a <= -8){
            special_output = _NONE;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if (a > -8 && a <= -pow(2,-1)){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
        else if (a > -pow(2,-1) && a <= -pow(2,-10)){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
        else if (a > -pow(2,-10) && a < 0){
            special_output = a_int;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if ( a_int == _NZERO ){
            special_output = _NZERO;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if ( a_int == _ZERO ){
            special_output = _ZERO;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if (a < pow(2,-10) && a > 0){
            special_output = a_int;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if (a < pow(2,-1) && a >= pow(2,-10)){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
         else if (a < 8 && a >= pow(2,-1)){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
        else if ( a_int == _INF){
            special_output = ONE;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if (a >= 8){
            special_output = ONE;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if (snan == 1){
            special_output = CNAN;
            special = 1;
            denormal = 0;
            fflags = NV;
        }
        else if (qnan == 1){
            special_output = CNAN;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
    }
    else if (opcode == SIGMOID){
        if ( a_int == _NINF){
            special_output = _ZERO;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if (sig_range < -149.0){
        //else if (a < -151*16.0/23.0){
            special_output = _ZERO;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        //else if (a < -126*16.0/23.0 && a >= -151*16.0/23.0){
        //    special = 0;
        //    denormal = 0;
        //    fflags = 0;
       // }
        //else if (a <= -16 && a >= -126*16.0/23.0){
        else if (a <= -16 && sig_range >= -149.0){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
        else if (a <= -1 && a > -16){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
        else if (a <= -pow(2,-9) && a > -1){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
        else if (a < 0 && a > -pow(2,-9)){
            special_sig_float = a_sig;
            special_sig = 1;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if ( a_int == _NZERO ){
            special_output = HALF;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if ( a_int == _ZERO ){
            special_output = HALF;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if (a > 0 && a < pow(2,-9)){
            special_sig_float = a_sig;
            special_sig = 1;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if (a >= pow(2,-9) && a < 1){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
        else if (a >= 1 && a < 16){
            special = 0;
            denormal = 0;
            fflags = 0;
        }
        else if ( a_int == _INF){
            special_output = ONE;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if (a >= 16){
            special_output = ONE;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
        else if (snan == 1){
            special_output = CNAN;
            special = 1;
            denormal = 0;
            fflags = NV;
        }
        else if (qnan == 1){
            special_output = CNAN;
            special = 1;
            denormal = 0;
            fflags = 0;
        }
    }

    sfu_output sfu_output_value = {0,0,0,0};
    float *special_pointer =(float *)&special_output;
    float special_output_float = *special_pointer;
    #ifdef HECTOR
        Hector::show("special",special);
    #endif
    #ifdef JASPER_C
	    JG_OUTPUT(special);
    #endif
    if (special == 1){
        if (special_sig == 0)
            sfu_output_value.sfu_data_output = special_output_float;
        else
            sfu_output_value.sfu_data_output = special_sig_float;
        sfu_output_value.sfu_exception_output = fflags;;
        return sfu_output_value;
    }


//Transform one function to another function
    if (opcode == TANH){
        if ( a >= 0.5 && a < 8){
            opcode_new = SIGMOID;
            a_new = 2*a;
        }
        else if (a <= -0.5 && a > -8){
            opcode_new = SIGMOID;
            a_new = -2*a;
        }
        else{
            opcode_new = opcode;
            a_new = a;
        }
    }
    else if (opcode == SIGMOID){
        if ( a >= pow(2,-9) && a < 1){
            opcode_new = TANH;
            a_new = a/2;
        }
        else if (a <= (-1 * pow(2,-9)) && a > -1){
            opcode_new = TANH;
            a_new = -a/2;
        }
        else if (a <=-16 && sig_range >= -149.0){
        //else if (a <=-16 && a >= -151*16.0/23.0){
            opcode_new = EXP2;
  	        a_new = sig_range;
            //a_new = 23/16*a;
        }
        else{
            opcode_new = opcode;
            a_new = a;
        }
    }
	else if (opcode == RCP ){
        //C dead code
        //if (denormal == 1){
        //    opcode_new = opcode;
        //    a_new = a*2;
        //}
        if (denormal == 2){
            opcode_new = opcode;
            a_new = a*4;
        }
        else{
            opcode_new = opcode;
            a_new = a;
        }
    }
    else if (opcode == EXP2){
        opcode_new = opcode;
        a_new = a;
    }
//Input reduction of exp2
	int b = 0;
    float exp_rro_mx = 0;
    float exp_rro_ex = 0;
    if (opcode_new == EXP2){
        if (a_new >= 0){
            b = floor(a_new);
            exp_rro_mx = double_to_float_softfloat(double(a_new) - b +1, softfloat_round_min);
            exp_rro_ex = b;
        }
        else{
            b = floor(-a_new);
            exp_rro_mx = double_to_float_softfloat(double(a_new) + b +1+1, softfloat_round_max);
	    if (b+a_new == 0)
		    exp_rro_ex = -b;
	    else
        	exp_rro_ex = -(b+1);
        }
    }

//Transform float number to corresponding hex number
    unsigned char float_745_1 = 0;
    unsigned char float_745_2 = 0;
    unsigned char float_745_3 = 0;
    unsigned char float_745_4 = 0;
    unsigned long int float_745_5 = 0;

    if (opcode_new == EXP2){
        char *m=(char *)&exp_rro_mx;
        float_745_1 = (unsigned char)(*m);
        float_745_2 = (unsigned char)(*(m+1));
        float_745_3 = (unsigned char)(*(m+2));
        float_745_4 = (unsigned char)(*(m+3));
    } else{
        char *p=(char *)&a_new;
        float_745_1 = (unsigned char)(*p);
        float_745_2 = (unsigned char)(*(p+1));
        float_745_3 = (unsigned char)(*(p+2));
        float_745_4 = (unsigned char)(*(p+3));
    }
    int src_exp = 0;
    unsigned int lut_index = 0;
    unsigned long int unsignedfixbin = 0;
    int x_exp = 0;
    int pos = 0;
    int x2_slide = 0;
    float_745_5 = (float_745_1) + (float_745_2 << (8));
//Generate input of LUT and booth mul
    // exp, m = 5
    if (opcode_new == EXP2){
        lut_index = float_745_3 >> 2 & 0b11111;
        src_exp = exp_rro_ex;
        x2_slide = 2;
        //unsignedfixbin = ((float_745_1) + (float_745_2 << (8)) + (uint(float_745_3 & 0b11) << (8+8)))<<x2_slide;
        unsignedfixbin = (float_745_5 + (uint(float_745_3 & 0b11) << (8+8)))<<x2_slide;
        x_exp = 0;
    }
    // rcp, m = 7
    else if (opcode_new == RCP){
        lut_index = float_745_3 & 0b1111111;
        src_exp = 127 - (((((float_745_4) & 0b1111111) << 1)  + (((float_745_3 >> 7) & 0b1))));
        x2_slide = 4;
        //unsignedfixbin = ((float_745_1) + (float_745_2 << (8))) << x2_slide;
        unsignedfixbin = (float_745_5) << x2_slide;
        x_exp = 0;
    }
    // tanh, m = 3
    else if (opcode_new == TANH){
        lut_index = float_745_3 >> 4 & 0b111;
        src_exp = 0;
        x2_slide = 0;
        //unsignedfixbin = (float_745_1) + (float_745_2 << (8)) + (uint(float_745_3 & 0b1111) << (8+8)) << x2_slide;
        unsignedfixbin = float_745_5 + (uint(float_745_3 & 0b1111) << (8+8)) << x2_slide;
        x_exp = (((((float_745_4) & 0b1111111) << 1)  + (((float_745_3 >> 7) & 0b1))))-127;
    }
    else if (opcode_new == SIGMOID){
        if (a_new < 0){
            a_new = -a_new;
            pos = -1;
        }
        else{
            a_new = a_new;
            pos = 1;
        }
        if ( a_new < pow(2,1) && a_new >= pow(2,0)){
            lut_index = float_745_3 >> 4 & 0b111;
            src_exp = 0;
            x2_slide = 0;
            //unsignedfixbin = (float_745_1) + (float_745_2 << (8)) + (uint(float_745_3 & 0b1111) << (8+8)) << x2_slide;
            unsignedfixbin =  float_745_5 + (uint(float_745_3 & 0b1111) << (8+8)) << x2_slide;
        }
        else if ( a_new < pow(2,3) && a_new >= pow(2,1)){
            lut_index = float_745_3 >> 3 & 0b1111;
            src_exp = 0;
            x2_slide = 1;
            //unsignedfixbin = (float_745_1) + (float_745_2 << (8)) + (uint(float_745_3 & 0b111) << (8+8)) << x2_slide;
            unsignedfixbin = float_745_5 + (uint(float_745_3 & 0b111) << (8+8)) << x2_slide;
        }
        else if ( a_new < pow(2,4) && a_new >= pow(2,3)){
            lut_index = float_745_3 >> 2 & 0b11111;
            src_exp = 0;
            x2_slide = 2;
            //unsignedfixbin = (float_745_1) + (float_745_2 << (8)) + (uint(float_745_3 & 0b11) << (8+8)) << x2_slide;
            unsignedfixbin =  float_745_5 + (uint(float_745_3 & 0b11) << (8+8)) << x2_slide;
        }
        x_exp = (((((float_745_4) & 0b1111111) << 1)  + (((float_745_3 >> 7) & 0b1))))-127;
    }

    int ex2_x1 = lut_index;
    int ex2_x2 = unsignedfixbin;

    #ifdef HECTOR
        Hector::show("ex2_x1",ex2_x1);
        Hector::show("ex2_x2",ex2_x2);
    #endif
    #ifdef JASPER_C
        JG_SHOW(a_new);
        JG_SHOW(float_745_3);
	    JG_SHOW(ex2_x1);
	    JG_SHOW(ex2_x2);
    #endif
//Generate LUT parameter
    long int *c0_lut;
    long int *c1_lut;
    long int *c2_lut;
/*
    vector<double> c0_lut;
    vector<double> c1_lut;
    vector<double> c2_lut;
*/
    int c2_p17 = 0;
	int c1_p10 = 0;
    // exp, m = 5
    if (opcode_new == EXP2){
        c0_lut = (c0_exp);
        c1_lut = (c1_exp);
        c2_lut = (c2_exp);
        pos = 1;
        c2_p17 = -10;
		c1_p10 = -5;
    }
    // rcp, m = 7
    else if (opcode_new == RCP){
        c0_lut = (c0_rcp);
        c1_lut = (c1_rcp);
        c2_lut = (c2_rcp);
        if (a < 0){
            pos = -1;
        }
        else{
            pos = 1;
        }
        c2_p17 = -14;
		c1_p10 = -7;
    }
    // tanh, m = 3
    else if (opcode_new == TANH){
        #ifdef HECTOR
            Hector::show("a_new",a_new);
        #endif
        if (a_new < 0){
            a_new = -a_new;
            pos = -1;
        }
        else{
            a_new = a_new;
            pos = 1;
        }
        if ( a_new < pow(2,-1) && a_new >= pow(2,-2)){
            c0_lut = (c0_tanh_2_1_2_2);
            c1_lut = (c1_tanh_2_1_2_2);
            c2_lut = (c2_tanh_2_1_2_2);
            c2_p17 = -10;
			c1_p10 = -5;
        }
        else if ( a_new < pow(2,-2) && a_new >= pow(2,-3)){
            c0_lut = (c0_tanh_2_2_2_3);
            c1_lut = (c1_tanh_2_2_2_3);
            c2_lut = (c2_tanh_2_2_2_3);
            c2_p17 = -12;
			c1_p10 = -6;
        }
        else if ( a_new < pow(2,-3) && a_new >= pow(2,-4)){
            c0_lut = (c0_tanh_2_3_2_4);
            c1_lut = (c1_tanh_2_3_2_4);
            c2_lut = (c2_tanh_2_3_2_4);
            c2_p17 = -14;
			c1_p10 = -7;
        }
        else if ( a_new < pow(2,-4) && a_new >= pow(2,-5)){
            c0_lut = (c0_tanh_2_4_2_5);
            c1_lut = (c1_tanh_2_4_2_5);
            c2_lut = (c2_tanh_2_4_2_5);
            c2_p17 = -16;
			c1_p10 = -8;
        }
        else if ( a_new < pow(2,-5) && a_new >= pow(2,-6)){
            c0_lut = (c0_tanh_2_5_2_6);
            c1_lut = (c1_tanh_2_5_2_6);
            c2_lut = (c2_tanh_2_5_2_6);
            c2_p17 = -18;
			c1_p10 = -9;
        }
        else if ( a_new < pow(2,-6) && a_new >= pow(2,-7)){
            c0_lut = (c0_tanh_2_6_2_7);
            c1_lut = (c1_tanh_2_6_2_7);
            c2_lut = (c2_tanh_2_6_2_7);
            c2_p17 = -20;
			c1_p10 = -10;
        }
        else if ( a_new < pow(2,-7) && a_new >= pow(2,-8)){
            c0_lut = (c0_tanh_2_7_2_8);
            c1_lut = (c1_tanh_2_7_2_8);
            c2_lut = (c2_tanh_2_7_2_8);
            c2_p17 = -22;
			c1_p10 = -11;
        }
        else if ( a_new < pow(2,-8) && a_new >= pow(2,-9)){
            c0_lut = (c0_tanh_2_8_2_9);
            c1_lut = (c1_tanh_2_8_2_9);
            c2_lut = (c2_tanh_2_8_2_9);
            c2_p17 = -24;
			c1_p10 = -12;
        }
        else if ( a_new < pow(2,-9) && a_new >= pow(2,-10)){
            c0_lut = (c0_tanh_2_9_2_10);
            c1_lut = (c1_tanh_2_9_2_10);
            c2_lut = (c2_tanh_2_9_2_10);
            c2_p17 = -26;
			c1_p10 = -13;
        }
    }
    else if (opcode_new == SIGMOID){
        if ( a_new < pow(2,1) && a_new >= pow(2,0)){
            c0_lut = (c0_sigmoid_1_2);
            c1_lut = (c1_sigmoid_1_2);
            c2_lut = (c2_sigmoid_1_2);
            c2_p17 = -6;
			c1_p10 = -3;
        }
        else if ( a_new < pow(2,2) && a_new >= pow(2,1)){
            c0_lut = (c0_sigmoid_2_4);
            c1_lut = (c1_sigmoid_2_4);
            c2_lut = (c2_sigmoid_2_4);
            c2_p17 = -6;
			c1_p10 = -3;
        }
        else if ( a_new < pow(2,3) && a_new >= pow(2,2)){
            c0_lut = (c0_sigmoid_4_8);
            c1_lut = (c1_sigmoid_4_8);
            c2_lut = (c2_sigmoid_4_8);
            c2_p17 = -4;
			c1_p10 = -2;
        }
        else if ( a_new < pow(2,4) && a_new >= pow(2,3)){
            c0_lut = (c0_sigmoid_8_16);
            c1_lut = (c1_sigmoid_8_16);
            c2_lut = (c2_sigmoid_8_16);
            c2_p17 = -4;
			c1_p10 = -2;
        }
    }
    double x1 = 0;
    long int y0 = *(c0_lut + lut_index);
    //double y0 = c0_tanh_2_3_2_4[3];
    long int y1 = *(c1_lut + lut_index);
    long int y2 = *(c2_lut + lut_index);
/*
    double y0 = c0_lut[lut_index];
    double y1 = c1_lut[lut_index];
    double y2 = c2_lut[lut_index];
*/
    //double ex2_c0 = y0;
    //double ex2_c1 = y1;
    //double ex2_c2 = y2;

    //long int ex2_c0_formal = (lon;
    //long int ex2_c1_formal = (long int)ex2_c1;
    //long int ex2_c2_formal = (long int)ex2_c2;

    int y1_pos = 0;
    int y2_pos = 0;
    if (y1 > 0 || y1 == 0)
        y1_pos = 1;
    else
        y1_pos = -1;
    if (y2 < 0 || y2 == 0)
        y2_pos = -1;
    else
        y2_pos = 1;
//Booth mul
    /*
    double c0 = 0;
    double c1 = 0;
    double c2 = 0;
    double c = 0;
    */
    long int c0 = 0;
    long int c1 = 0;
    long int c2 = 0;
    long int c = 0;
    //long int ex2_x2x2 = (long int)square_cut_6_bit(unsignedfixbin);

     #ifdef HECTOR
        //Hector::show("ex2_x2x2",ex2_x2x2);
        Hector::show("ex2_c0",y0);
        Hector::show("ex2_c1",y1);
        Hector::show("ex2_c2",y2);
        Hector::show("c2_p17",c2_p17);
        Hector::show("c1_p10",c1_p10);
    #endif
    #ifdef JASPER_C
	    //JG_SHOW(ex2_x2x2);
    	JG_SHOW(y0);
	    JG_SHOW(y1);
	    JG_SHOW(y2);
	    JG_SHOW(c2_p17);
	    JG_SHOW(c1_p10);
    #endif
    int *unsignedfixbin_booth = booth_transform_c1(unsignedfixbin);
    int *unsignedfixbin_2_booth = booth_transform_c2((unsigned long long)(square_cut_6_bit(unsignedfixbin)));

    if (opcode_new == EXP2 || opcode_new == RCP){
        c0 = (long int)y0 << (MAX_POS-T_D+T_N_D);
	    c1 = booth_mul(y1,unsignedfixbin_booth,c1_p10,y1_pos,_C1) >> -(MAX_POS-P_D+P_N_D-DEC_FP32);
        c2 = booth_mul(y2,unsignedfixbin_2_booth,c2_p17,y2_pos,_C2) >> -(MAX_POS-Q_D+Q_N_D-X2_2);
        c = c0 + c1 + c2;
    } else if (opcode_new == TANH || opcode_new == SIGMOID){
        c0 = (long int)y0 << (MAX_POS-T_D+T_N_D);
	    c1 = booth_mul(y1,unsignedfixbin_booth,c1_p10,y1_pos,_C1) >>  (-x_exp -(MAX_POS-P_D+P_N_D-DEC_FP32));
        c2 = booth_mul(y2,unsignedfixbin_2_booth,c2_p17,y2_pos,_C2) >> (-(MAX_POS-Q_D+Q_N_D-X2_2) - x_exp - x_exp);
        c = c0 + c1 + c2;
    }
    //Generate final result
    /*
       #ifdef HECTOR
        for (size_t i = 0; i < 11; i++)
        {
            Hector::show("ex3_c1x2_pp_%1",i,*(ex3_c1x2_pp + i));
            Hector::show("ex3_c1x2_pp_sign_%1",i,*(ex3_c1x2_pp_sign + i));
            Hector::show("ex3_c1x2_pp_A_sign_%1",i,*(ex3_c1x2_pp_A_sign + i));
        }
        for (size_t i = 0; i < 18; i++)
        {
            Hector::show("ex3_c2x2x2_pp_%1",i,*(ex3_c2x2x2_pp + i));
            Hector::show("ex3_c2x2x2_pp_sign_%1",i,*(ex3_c2x2x2_pp_sign + i));
            Hector::show("ex3_c2x2x2_pp_A_sign_%1",i,*(ex3_c2x2x2_pp_A_sign + i));

        }
        #endif
    */

    long int res_0 = c0;
    long int res_1 = c1 >> x2_slide;
    long int res_2 = c2 >> (x2_slide<<1);
    //printf("res_0 = %ld\nres_1 = %ld\nres_2 = %ld\n",res_0,res_1,res_2);


    //long double res = 0;
    long int res = 0;
    //double res = 0;
    long int res_int = 0;
    long int res_int_large = 0;
    int res_int_large_num = 0;
    int res_int_large_try = (MAX_POS - src_exp);
    //res_int_large = (long int)1 << (MAX_POS - src_exp);

    if (res_int_large_try >= 128){
    	res_int_large = (long int)1 << (res_int_large_try - 128);
	    res_int_large_num = 2;
    }
    else if (res_int_large_try >= 64 && res_int_large_try < 128){
    	res_int_large = (long int)1 << (res_int_large_try - 64);
	    res_int_large_num = 1;
    }
    else if (res_int_large_try >= 0 && res_int_large_try < 64){
    	res_int_large = (long int)1 << (res_int_large_try);
	    res_int_large_num = 0;
    }
    else if (res_int_large_try >= -64 && res_int_large_try < 0){
    	res_int_large = (long int)1 << (-MAX_POS + src_exp);
	    res_int_large_num = 0;
    }
    else if (res_int_large_try >= -128 && res_int_large_try < -64){
    	res_int_large = (long int)1 << (-MAX_POS + src_exp - 64);
	    res_int_large_num = 1;
    }
    //else if (res_int_large_try < -128){
    //	res_int_large = (long int)1 << (-MAX_POS + src_exp - 128);
	//    res_int_large_num = 2;
    //}

    //printf("The num is %d, the large is %ld, the pos is %d\n", MAX_POS - src_exp, res_int_large, res_int_large_num);

    res = (res_0 + res_1 + res_2);
    //long int res_int = res_0 + res_1 + res_2;
    if (opcode == TANH){
        //if ( a < pow(2,-10) && a > (-1*pow(2,-10))){
        //    res = a_int ;
        //}
        //else
        if ( a >= 0.5 && a < 8 ){
            res =(res<<1) - res_int_large;
        }
        else if ( a <= -0.5 && a > -8 ){
            res = -(res << 1) + res_int_large;
        }

    } else if (opcode == SIGMOID){
        if ( a >= pow(2,-9) && a < 1){
            res = (res + res_int_large);
        }
        else if (a <= -1*pow(2,-9) && a > -1){
            res = (res_int_large << 1) - (res + res_int_large);
        }
        else if ( a <= -1 && a > -16 ){
            res = res_int_large - res;
            //res = 1 - res;
        }
        //else if ( (a < 0 && a > -pow(2,-9)) || (a > 0 && a < pow(2,-9))){
        //    res = a/4 + 1/2;
        //}
    }
    else if (opcode == RCP){
        //if (denormal == 1)
        //    res = res * 2;
        if (denormal == 2)
            res = res * 4;
        else
            res = res;
    }
    uint res_sign;
    if (res >= 0) {
        res_sign = 1;
	    if (opcode == RCP)
    		res_int = res / ((long int)1 << denormal);
        else
    		res_int = res;
    } else {
        res_sign = 0;
	    if (opcode == RCP)
    		res_int = -res / ((long int)1 << denormal);
	    else
    		res_int = -res;
    }
    long ex4_add_result;
    if (opcode == SIGMOID){
	if ( (a >= pow(2,-9) && a < 1) || (a <= -1*pow(2,-9) && a > -1))
    		ex4_add_result = res_int;
	else
    		ex4_add_result = res_int << 1;
    }
    else{
    	ex4_add_result = res_int << 1;

    }
    //printf("%lx\n", ex4_add_result);
    //printf("%ld\n", res);
    #ifdef CUT_EX4
        uint res_sign_temp;
        long ex4_add_result_temp;
    #endif

    #ifdef HECTOR
        Hector::show("ex4_add_res",res);
        #ifdef CUT_EX4
            Hector::cutpoint("ex4_add_sign_cut",res_sign_temp);
            Hector::cutpoint("ex4_add_result_cut",ex4_add_result_temp);
            Hector::show("ex4_add_sign",res_sign_temp);
            Hector::show("ex4_add_result",ex4_add_result_temp);
        #endif
        Hector::show("ex4_add_sign",res_sign);
        Hector::show("ex4_add_result",ex4_add_result);
    #endif

    #ifdef JASPER_C
	    JG_SHOW(res);
	    JG_SHOW(res_sign);
	    JG_SHOW(ex4_add_result);
    #endif

    long int res_back;
    #ifdef CUT_EX4
        if (opcode == RCP) {
            ex4_add_result_temp =  ex4_add_result_temp << denormal;
        }
         if (opcode == SIGMOID){
            if ( (a >= pow(2,-9) && a < 1) || (a <= -1*pow(2,-9) && a > -1))
                res_back = ex4_add_result_temp;
            else
                res_back = ex4_add_result_temp >> 1;
         } else {
            res_back = ex4_add_result_temp >> 1;
         }
    #else
        if (opcode == RCP) {
            ex4_add_result =  ex4_add_result << denormal;
        }

        if (res_sign == 1) {
            if (opcode == SIGMOID){
                if ( (a >= pow(2,-9) && a < 1) || (a <= -1*pow(2,-9) && a > -1))
                    res_back = ex4_add_result;
                else
                res_back = ex4_add_result >> 1;
            } else {
                res_back = ex4_add_result >> 1;
            }
        } else {
            res_back = -(ex4_add_result >> 1);
        }
    #endif

    #ifdef HECTOR
        Hector::show("res_back",res_back);
        Hector::show("pos",pos);
    #endif

//Golden check
    //res = res / res_int_large;
    //q=(char *)&golden_fp32;
    //golden_3 = (unsigned char)(*(q+2));
    //golden_4 = (unsigned char)(*(q+3));
    //golden_exp = (((((golden_4) & 0b1111111) << 1)  + (((golden_3 >> 7) & 0b1))))-127;
    //double result_double = double(res);


    union ui64_f64 res_64;
    float64_t res_64_t;
    res_64_t.v = to_uint64(res_back);
    res_64.f = res_64_t;
    //printf("The res_64 is %ld\n", res_64.ui);

    uint32_t res_exp_new = (uint32_t((res_64.ui >> 52)) & 0x7ff)  - res_int_large_try;
    int64_t res_temp = (res_64.ui & (~uint64_t(0x7ff0000000000000))) | (uint64_t(res_exp_new) << 52);

    res_64.ui = res_temp;

    #ifdef HECTOR
        Hector::show("res_final",res_64.ui);
    #endif
    #ifdef JASPER_C
	    JG_SHOW(res_temp);
    #endif
    /*
    unsigned char float_745_7_input = 0;
    unsigned char float_745_8_input = 0;
    char *output_p=(char *)&(res_64.ui);
    float_745_7_input = (unsigned char)(*(output_p+6));
    float_745_8_input = (unsigned char)(*(output_p+7));
    //printf("The exp_11 is %d and %d\n", float_745_8_input, float_745_7_input);
    int exp_11 = (((float_745_8_input) & 0b1111111) << 4)  + ((float_745_7_input >> 4) & 0b1111);
    int exp_11_res = exp_11 - res_int_large_try;
    int exp_11_7 = ((exp_11_res & 0b11111110000) >> 4) + (float_745_8_input & 0b10000000);
    int exp_11_4 = ((exp_11_res & 0b00000001111) << 4) + (float_745_7_input & 0b00001111);
    //printf("The exp_11 is %d and %d\n", exp_11_7, exp_11_4);
    *(output_p+6) = (unsigned char)exp_11_4;
    *(output_p+7) = (unsigned char)exp_11_7;
    */
    //printf("The res_64 is %ld\n", res_64.ui);
    //printf("The res_64 is %f\n", res_64.f);
    //int mantissa = (float_745_1_input) + (float_745_2_input << (8)) + (uint(float_745_3_input & 0b1111111) << (8+8));

    float result = 0;
/*
    //double result_double = 0;
	//printf("The res is %ld\n", res);
	if ( MAX_POS - src_exp <= 0 )
    	result_double = double(res)*((double)(res_int_large));
	else
    	result_double = double(res)/((double)(res_int_large));
	//printf("The result double is %32.32f\n", result_double);
    for(int i = 0; i < res_int_large_num; i++){
	if ( MAX_POS - src_exp <= 0 )
	result_double = result_double * (double)((long int)1 << 62) * 4;
	else
	result_double = result_double / (double)((long int)1 << 62) / 4;
    }
*/
    if (opcode == SIGMOID){
        if ( (a >= pow(2,-9) && a < 1) || (a <= -1*pow(2,-9) && a > -1))
    	    result = double_to_float(res_64.f,softfloat_round_max)/2;
        else
            result = double_to_float(res_64.f,softfloat_round_max);
    }
    else{
        #ifdef CUT_EX4
            if(res_sign_temp == 0)
		        result = double_to_float((res_64.f),softfloat_round_max);
	        else
                result = -double_to_float((res_64.f),softfloat_round_min);
        #else
            if(pos >= 0)
		        result = double_to_float((res_64.f),softfloat_round_max);
	        else
                result = -double_to_float((res_64.f),softfloat_round_min);
        #endif
    }
	//printf("The result double is %32.32f\n", result_double);
// softfloat round up
	//result = float(result_double);
//	result = double_to_float(res_64.f,softfloat_round_max);
	//result = double_to_float_softfloat(result_double,softfloat_round_max);
// softfloat round up
    //result = float(result_double);
    //if (result == golden_fp32)
	//	err = 0;
	//else
    //	err = (result - golden_fp32) / (pow(2,((golden_exp)-23)));
    #ifdef JASPER_C
	    JG_SHOW(result);
    #endif
    sfu_output_value.sfu_err_output = 0;
    sfu_output_value.sfu_data_output = result;
    sfu_output_value.sfu_exception_output = fflags;
    sfu_output_value.sfu_booth_output = 0;
    return sfu_output_value;
}

extern "C" {
    sfu_output sfu_exp2(uint32_t a)
    {
        return sfu_cmodel(*(float *)&a, EXP2, false);
    }

    sfu_output sfu_tanh(uint32_t a)
    {
        return sfu_cmodel(*(float *)&a, TANH, false);
    }

    sfu_output sfu_sigmoid(uint32_t a)
    {
        return sfu_cmodel(*(float *)&a, SIGMOID, false);
    }

    sfu_output sfu_rcp(uint32_t a)
    {
        return sfu_cmodel(*(float *)&a, RCP, false);
    }
}
//int main(){
////float f1 = 0;
////sfu_output result;
////unsigned int f1_int = to_unsigned_int(f1);
////
////for (unsigned long int i = 0; i < pow(2,31); i++){
////	result = sfu_cmodel(f1_int, EXP2, false);
////	if (result.sfu_err_output <= 2 && result.sfu_err_output >= -2){
////		f1_int = f1_int + 1;
////    	printf("Success! The %d num is %X, the result is %X, the fflags is %X, the error is %f\n", i, f1_int, to_unsigned_int(result.sfu_data_output), result.sfu_exception_output, result.sfu_err_output);
////	}
////	else{
////    	printf("Fail! The %d num is %X, the result is %X, the fflags is %X, the error is %f\n", i, f1_int, to_unsigned_int(result.sfu_data_output), result.sfu_exception_output, result.sfu_err_output);
////		break;
////	}
////}
////
////}
////}
////float a = sfu_cmodel(0x3D9F6952, EXP2, false).sfu_data_outputn;
//unsigned int data = 0xa0463981;
//float a = sfu_cmodel(*(float *)&data, SIGMOID, false).sfu_data_output;
////float a = sfu_cmodel(0x3bc00000, TANH, false).sfu_data_output;
//printf("The number is %f\n",(a));
//printf("The number is %X\n",to_unsigned_int(a));
//return 0;
//}
