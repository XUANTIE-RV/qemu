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
