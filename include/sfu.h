// 文件路径：qemu/include/mycpp.h
#ifdef __cplusplus
extern "C" {
#endif
    #define SFU_NV 16
    #define SFU_DZ  8
    #define SFU_OF  4
    #define SFU_UF  2
    #define SFU_NX  1
    typedef struct sfu_output{
        float sfu_data_output;
        float sfu_err_output;
        int sfu_exception_output;
        long int sfu_booth_output;
    } sfu_output;
    sfu_output sfu_exp2(uint32_t a);
    sfu_output sfu_rcp(uint32_t a);
    sfu_output sfu_sigmoid(uint32_t a);
    sfu_output sfu_tanh(uint32_t a);

#ifdef __cplusplus
}
#endif
