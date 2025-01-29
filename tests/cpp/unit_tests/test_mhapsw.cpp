/*
 * Copyright � 2023 Advanced Micro Devices, Inc. All rights reserved.
 */

#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <limits>

#include "enable_perf.hpp"
#include "ops/ops_common/help_file.hpp"
#include "ops/ops_common/mha_validation.cpp"
#include "ops/ops_common/mhagprb_matrix.hpp"
#include <ops/dmacompiler/mhapsw/mhapsw.hpp>

#include "test_common.hpp"

#define RANDOM_DATA

using namespace mhagprb_matrix;
using namespace std;
template <typename Tqkv = uint16_t, typename WgT = uint8_t,
          typename OuT = uint8_t>
int test_mhapsw(int M, int K, bool debug = false,
                const std::string &a_dtype = "uint16",
                const std::string &b_dtype = "uint8",
                const std::string &c_dtype = "uint16",
                const std::string &model_name = "mzdk5") {
  int err_count = 0;

  int const AieRows = 4;
  int const AieCols = 4;

  size_t Ms = static_cast<size_t>(M);
  size_t Ks = static_cast<size_t>(K);

  size_t batch, heads, x, y;
  batch = 1;
  heads = 12; // all heads processed together in PSW unlike PSR
  x = Ks / heads;
  y = 64;

  size_t Stq, St, H, S, Dh, St_pad;
  Stq = Ms;
  St = x;
  St_pad = (((St - 1) / 8) + 1) * 8;
  H = heads;
  Dh = 64;
  size_t Sq = std::min<std::size_t>(
      16, std::max<std::size_t>(16, Stq / (AieCols * AieRows)));
  std::cout << "Sq = " << Sq << std::endl;

  size_t Dt = Dh * H; // total volume K across all heads

  size_t qry_rows = Stq;
  size_t qry_cols = Dt;
  size_t key_rows = St;
  size_t key_cols = Dt;
  size_t val_rows = St;
  size_t val_cols = Dt;
  size_t out_rows = Stq;
  size_t out_cols = Dt;

  size_t qry_subv_rows = Stq;
  size_t qry_subv_cols = Dh;
  size_t key_subv_rows = St;
  size_t key_subv_cols = Dh;
  size_t val_subv_rows = St;
  size_t val_subv_cols = Dh;
  size_t out_subv_rows = Stq;
  size_t out_subv_cols = Dh;

  std::vector<size_t> qkv_shape = {3 * Ms, Ks};

  std::vector<size_t> q_shape = {qry_rows, qry_cols};
  std::vector<size_t> k_shape = {key_rows, key_cols};
  std::vector<size_t> v_shape = {val_rows, val_cols};
  std::vector<size_t> add_shape = {heads, x, y};
  std::vector<size_t> msk_shape = {qry_rows, qry_rows};

  std::cout << "Q shape: (" << qry_rows << "," << qry_cols << ")" << std::endl;
  std::cout << "K shape: (" << key_rows << "," << key_cols << ")" << std::endl;
  std::cout << "V shape: (" << val_rows << "," << val_cols << ")" << std::endl;
  std::cout << "Add shape: (" << heads << "," << x << "," << y << ")"
            << std::endl;
  std::cout << "Attn shape: (" << qry_rows << "," << qry_rows << ")"
            << std::endl;

  std::vector<Tqkv> q(qry_rows * qry_cols);
  std::vector<Tqkv> k(key_rows * key_cols);
  std::vector<Tqkv> v(val_rows * val_cols);
  std::vector<Tqkv> add(heads * x * y);
  std::vector<uint16_t> msk(qry_rows * qry_rows);

  std::cout << "Q size : " << q.size() * sizeof(Tqkv) << std::endl;
  std::cout << "K size : " << k.size() * sizeof(Tqkv) << std::endl;
  std::cout << "V size : " << v.size() * sizeof(Tqkv) << std::endl;
  std::cout << "Add size : " << add.size() * sizeof(Tqkv) << std::endl;
  std::cout << "attn mask size : " << msk.size() * sizeof(Tqkv) << std::endl;

  std::vector<int32_t> qdq_params(QDQparam_size * num_qdq_nodes);
  std::vector<size_t> qdq_shape = {num_qdq_nodes, QDQparam_size};

  std::vector<OuT> out(Ms * Ks);
  RowMajorMatrix<Tqkv> aie_Y(out_rows, out_cols, out.data());
  std::vector<OuT> cpu_out(Ms * Ks);
  RowMajorMatrix<Tqkv> cpu_Y(out_rows, out_cols, cpu_out.data());
  std::vector<size_t> out_shape = {Ms, Ks};

#ifdef RANDOM_DATA
  RowMajorMatrix<Tqkv> aie_Q(qry_rows, qry_cols, q.data());
  RowMajorMatrix<Tqkv> aie_K(key_rows, key_cols, k.data());
  RowMajorMatrix<Tqkv> aie_V(val_rows, val_cols, v.data());
  gemm_qdq_param<Tqkv> qdq_params_l[4];

  // Initialize q, k and v
  srand(0xABCD);
  int64_t qk_qdq_c0, smv_qdq_c0;
  int32_t C1, C2, C3;
  uint8_t SQb, Sout, Stm_qkt, Stm_smv;

  // DQ before Attention Mask Add and SM
  Tqkv add_dq_zp, add_attn_dq_zp, qkt_dq_zp, sm_dq_zp;
  float add_dq_scale, add_attn_dq_scale, qkt_dq_scale, sm_dq_scale;
  // Q after Attention Mask Add and SM
  Tqkv msk_q_zp, sm_q_zp;
  float msk_q_scale, sm_q_scale;

  std::vector<Tqkv> qkv(3 * (M * K));
  // std::vector<Tqkv> q(M * N);
  // std::vector<Tqkv> k(K * N);
  // std::vector<Tqkv> v(K * N);
  std::cout << "QKV size : " << qkv.size() * sizeof(Tqkv) << " bytes"
            << std::endl;

  initialize_random<Tqkv>(qkv, 3 * (M * K), 32, 0);
  init_random<RowMajorMatrix<Tqkv>>(aie_Q, 0, 32);
  init_random<RowMajorMatrix<Tqkv>>(aie_K, 0, 32);
  init_random<RowMajorMatrix<Tqkv>>(aie_V, 0, 64);

  qk_qdq_c0 = 1;
  smv_qdq_c0 = 1;
  C1 = 1;
  C2 = 1;
  C3 = 1;
  SQb = 0;  // 8;
  Sout = 8; // 15;
  Stm_qkt = 2;
  Stm_smv = 0;

  // DQ before SM
  sm_dq_zp = 126;
  sm_dq_scale = 0.45;
  // Q after SM
  sm_q_zp = 0;
  sm_q_scale = 0.003921568;

  // DQ before Attention Mask Add (includes two Adds)
  add_dq_zp = 43174;
  add_dq_scale = 0.0003920507151633501;
  add_attn_dq_zp = 65535;
  add_attn_dq_scale = 0.15259021520614624;
  qkt_dq_zp = 39968;
  qkt_dq_scale = 0.004580912180244923;

  // Q after Attention Mask Add
  msk_q_zp = 65473;
  msk_q_scale = 0.15292927622795105;

  float div_value =
      65535 * 0.00021143521007616073; // value is actually sqrt(192)

  // Set 0 -  DQ before Attention Mask (3 quantity)
  qdq_params[(16 * 0) + 0] = add_dq_zp;
  qdq_params[(16 * 0) + 1] = float_to_bfloat16(add_dq_scale).value;
  qdq_params[(16 * 0) + 2] = add_attn_dq_zp;
  qdq_params[(16 * 0) + 3] = float_to_bfloat16(add_attn_dq_scale).value;
  qdq_params[(16 * 0) + 4] = qkt_dq_zp;
  qdq_params[(16 * 0) + 5] = float_to_bfloat16(qkt_dq_scale / div_value).value;

  // Set 1 - Q after Attention Mask
  qdq_params[(16 * 1) + 0] = msk_q_zp;
  qdq_params[(16 * 1) + 1] = float_to_bfloat16(1.0 / msk_q_scale).value;

  // Set 2 - QKT
  *(int64_t *)(&qdq_params[(16 * 2) + qdq_c0_idx]) = qk_qdq_c0;
  qdq_params[(16 * 2) + qdq_c1_idx] = C1;
  qdq_params[(16 * 2) + qdq_c2_idx] = C2;
  qdq_params[(16 * 2) + qdq_c3_idx] = C3;
  qdq_params[(16 * 2) + qdq_Mv_idx] = Sq;
  qdq_params[(16 * 2) + qdq_Nv_idx] = St_pad;
  qdq_params[(16 * 2) + qdq_SQb_idx] = 0;   // SQb;
  qdq_params[(16 * 2) + qdq_Sout_idx] = 30; // Sout;
  qdq_params[(16 * 2) + qdq_Stdm_idx] = 7;  // Stm_qkt;

  // Set 3 - SM *V
  *(int64_t *)(&qdq_params[(16 * 3) + qdq_c0_idx]) = smv_qdq_c0;
  qdq_params[(16 * 3) + qdq_c1_idx] = C1;
  qdq_params[(16 * 3) + qdq_c2_idx] = C2;
  qdq_params[(16 * 3) + qdq_c3_idx] = C3;
  qdq_params[(16 * 3) + qdq_Mv_idx] = Sq;
  qdq_params[(16 * 3) + qdq_Nv_idx] = val_subv_cols;
  qdq_params[(16 * 3) + qdq_SQb_idx] = 0;   // SQb;
  qdq_params[(16 * 3) + qdq_Sout_idx] = 29; // Sout;
  qdq_params[(16 * 3) + qdq_Stdm_idx] = 7;  // Stm_smv;

  // Set 4 - DQ before SM
  qdq_params[(16 * 4) + 0] = sm_dq_zp;
  // additional scaling to emulate exp using exp2.
  qdq_params[(16 * 4) + 1] = float_to_bfloat16(sm_dq_scale * 1.442695041).value;

  // Set 5 - Q after SM
  qdq_params[(16 * 5) + 0] = sm_q_zp;
  qdq_params[(16 * 5) + 1] = float_to_bfloat16(1.0 / sm_q_scale).value;

  // for CPU model
  qdq_params_l[0].C0 = qk_qdq_c0;
  qdq_params_l[0].C1 = C1;
  qdq_params_l[0].C2 = C2;
  qdq_params_l[0].C3 = C3;
  qdq_params_l[0].sqb = SQb;
  qdq_params_l[0].sout = Sout;
  qdq_params_l[0].zero_point = 3;
  qdq_params_l[0].scale = 0.0034;

  qdq_params_l[1].C0 = smv_qdq_c0;
  qdq_params_l[1].C1 = C1;
  qdq_params_l[1].C2 = C2;
  qdq_params_l[1].C3 = C3;
  qdq_params_l[1].sqb = SQb;
  qdq_params_l[1].sout = Sout;
  qdq_params_l[1].zero_point = 3;
  qdq_params_l[1].scale = 0.0034;

  qdq_params_l[2].zero_point = sm_dq_zp;
  qdq_params_l[2].scale = sm_dq_scale;

  qdq_params_l[3].zero_point = sm_q_zp;
  qdq_params_l[3].scale = sm_q_scale;

  int const out_size = out_rows * out_cols * sizeof(Tqkv);

  void *cpu_head_qry =
      malloc(RowMajorMatrix<Tqkv>::size(qry_rows, qry_subv_cols));
  void *cpu_head_key =
      malloc(RowMajorMatrix<Tqkv>::size(key_rows, key_subv_cols));
  void *cpu_head_val =
      malloc(RowMajorMatrix<Tqkv>::size(val_rows, val_subv_cols));
  void *cpu_head_out =
      malloc(RowMajorMatrix<uint32_t>::size(out_rows, out_subv_cols));
  void *cpu_head8_out =
      malloc(RowMajorMatrix<Tqkv>::size(out_rows, out_subv_cols));

  RowMajorMatrix<Tqkv> head_Q(qry_rows, qry_subv_cols, cpu_head_qry);
  RowMajorMatrix<Tqkv> head_K(key_rows, key_subv_cols, cpu_head_key);
  RowMajorMatrix<Tqkv> head_V(val_rows, val_subv_cols, cpu_head_val);
  RowMajorMatrix<uint32_t> head_Y(out_rows, out_subv_cols, cpu_head_out);
  RowMajorMatrix<Tqkv> head_Y16(out_rows, out_subv_cols, cpu_head8_out);

  // calculate cpu_Y
  bool transposedK = false;

  // int const num_heads = qry_cols / qry_subv_cols;
  for (int h = 0; h < H; ++h) {
    for (int i = 0; i < head_Q.num_rows; ++i) {
      for (int j = 0; j < head_Q.num_cols; ++j) {
        head_Q.at(i, j) = aie_Q.at(i, (h * qry_subv_cols) + j);
      }
    }
    for (int i = 0; i < head_K.num_rows; ++i) {
      for (int j = 0; j < head_K.num_cols; ++j) {
        head_K.at(i, j) = aie_K.at(i, (h * key_subv_cols) + j);
      }
    }
    for (int i = 0; i < head_V.num_rows; ++i) {
      for (int j = 0; j < head_V.num_cols; ++j) {
        head_V.at(i, j) = aie_V.at(i, (h * val_subv_cols) + j);
      }
    }

    ref_qxkxv<Tqkv>(head_Q.data, head_K.data, head_V.data, head_Y16.data,
                    head_Y.data, qry_rows, key_subv_cols, val_rows,
                    val_subv_cols, qdq_params_l, transposedK);

    for (int i = 0; i < head_Y.num_rows; ++i) {
      for (int j = 0; j < head_Y.num_cols; ++j) {
        cpu_Y.at(i, (h * out_subv_cols) + j) = head_Y16.at(i, j);
      }
    }
  }

  free(cpu_head_qry);
  free(cpu_head_key);
  free(cpu_head_val);
  free(cpu_head_out);
  free(cpu_head8_out);
#else

  std::string fld_name =
      OpInterface::get_dd_base_dir() + "//.." + "//bins//PSW//mha//";
  std::vector<Tqkv> all_in_data(4 * (M * K) + (M * M));
  std::cout << "All input data size : " << all_in_data.size() * sizeof(Tqkv)
            << " bytes" << std::endl;

  std::cout << "Model Data : " << fld_name << std::endl;
  read_bin_file(fld_name + "ifm.bin",
                reinterpret_cast<char *>(all_in_data.data()));

  memcpy(q.data(), all_in_data.data(), qry_rows * qry_cols * sizeof(Tqkv));
  memcpy(k.data(),
         reinterpret_cast<char *>(all_in_data.data()) +
             qry_rows * qry_cols * sizeof(Tqkv),
         key_rows * key_cols * sizeof(Tqkv));
  memcpy(v.data(),
         reinterpret_cast<char *>(all_in_data.data()) +
             qry_rows * qry_cols * sizeof(Tqkv) +
             key_rows * key_cols * sizeof(Tqkv),
         val_rows * val_cols * sizeof(Tqkv));
  memcpy(add.data(),
         reinterpret_cast<char *>(all_in_data.data()) +
             qry_rows * qry_cols * sizeof(Tqkv) +
             key_rows * key_cols * sizeof(Tqkv) +
             val_rows * val_cols * sizeof(Tqkv),
         qry_rows * qry_cols * sizeof(Tqkv));
  memcpy(msk.data(),
         reinterpret_cast<char *>(all_in_data.data()) +
             qry_rows * qry_cols * sizeof(Tqkv) +
             key_rows * key_cols * sizeof(Tqkv) +
             val_rows * val_cols * sizeof(Tqkv) +
             qry_rows * qry_cols * sizeof(Tqkv),
         qry_rows * qry_rows * sizeof(Tqkv));

  read_bin_file(fld_name + "ofm.bin", reinterpret_cast<char *>(cpu_out.data()));
  read_bin_file(fld_name + "wgt.bin",
                reinterpret_cast<char *>(qdq_params.data()));
#endif
  // calculate aie_Y
  std::map<std::string, std::any> attr;

  if (model_name.find("4x4") != std::string::npos) {
    attr["design_param"] = std::vector<string>{"4x4"};
  }
  ryzenai::mhapsw mhapsw_ =
      ryzenai::mhapsw<Tqkv, WgT, OuT>(a_dtype, b_dtype, c_dtype, false, attr);
  mhapsw_.debug(debug);
  // std::vector<size_t> a_shape = {qry_rows, qry_cols, key_rows, val_cols};
  mhapsw_.set_params(model_name, qkv_shape);

  std::vector<Tensor> const_Tensor;

  const_Tensor = {{qdq_params.data(), qdq_shape, "int32"}};
  mhapsw_.initialize_const_params(const_Tensor);

  std::vector<Tensor> input_Tensor;

  input_Tensor = {{q.data(), q_shape, a_dtype},
                  {k.data(), k_shape, a_dtype},
                  {v.data(), v_shape, a_dtype},
                  {add.data(), add_shape, a_dtype},
                  {msk.data(), msk_shape, a_dtype}};

  std::vector<Tensor> output_Tensor;
  output_Tensor = {{out.data(), out_shape, a_dtype}};
#ifdef UNIT_TEST_PERF
  LOG_THIS("M = " << M << ", H = " << H << ", St = " << St << ", Dh = " << Dh);
  PROFILE_THIS(mhapsw_.execute(input_Tensor, output_Tensor));
#else
  mhapsw_.execute(input_Tensor, output_Tensor);
#endif

  float const max_pct_diff = 1.0;
  float average_error_rate = check_result_mha(cpu_Y, aie_Y, max_pct_diff, 0);

  // int err_cnt_TH = int(qry_rows * val_cols * 0.02);
  if (average_error_rate > 15) {
    err_count = 0;
  }

  return err_count;
}

// mhapsw 4x4
TEST(PSW_mhapsw_Testa16w8, Kernel_64_768) {
  int err_count = test_mhapsw<uint16_t, uint8_t, uint16_t>(
      64, 768, false, "uint16", "uint8", "uint16", "4x4PSW1.0");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}
