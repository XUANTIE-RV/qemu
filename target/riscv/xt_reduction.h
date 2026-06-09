float16 do_fredsum_32_h_internal(void*, int, float_status *, target_ulong, bool);
float32 do_fredsum_32_w_internal(void*, int, float_status *, target_ulong, bool);
bfloat16 do_bfredsum_32_h_internal(void*, int, float_status *, target_ulong, bool);
float16 do_fredsum_64_h_internal(void*, int, float_status *, target_ulong, bool);
float32 do_fredsum_64_w_internal(void*, int, float_status *, target_ulong, bool);
bfloat16 do_bfredsum_64_h_internal(void*, int, float_status *, target_ulong, bool);
float16 do_fredmax_32_h_internal(void*, int, float_status *, target_ulong, bool);
float32 do_fredmax_32_w_internal(void*, int, float_status *, target_ulong, bool);
bfloat16 do_bfredmax_32_h_internal(void*, int, float_status *, target_ulong, bool);
float16 do_fredmax_64_h_internal(void*, int, float_status *, target_ulong, bool);
float32 do_fredmax_64_w_internal(void*, int, float_status *, target_ulong, bool);
bfloat16 do_bfredmax_64_h_internal(void*, int, float_status *, target_ulong, bool);
float16 do_fredmin_32_h_internal(void*, int, float_status *, target_ulong, bool);
float32 do_fredmin_32_w_internal(void*, int, float_status *, target_ulong, bool);
bfloat16 do_bfredmin_32_h_internal(void*, int, float_status *, target_ulong, bool);
float16 do_fredmin_64_h_internal(void*, int, float_status *, target_ulong, bool);
bfloat16 do_bfredmin_64_h_internal(void*, int, float_status *, target_ulong, bool);
float32 do_fredmin_64_w_internal(void*, int, float_status *, target_ulong, bool);

int64_t
do_mfredmax_h_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs);
int64_t
do_mfredmin_h_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs);
int64_t
do_mfredsum_h_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs);
int64_t
do_mfredmax_s_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs);
int64_t
do_mfredmin_s_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs);
int64_t
do_mfredsum_s_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs);
int64_t
do_mfredmax_bf16_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs);
int64_t
do_mfredmin_bf16_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs);
int64_t
do_mfredsum_bf16_internal(void *start, CPURISCVState *env, uint32_t elem_count,
                       uint32_t init_val, bool abs);
