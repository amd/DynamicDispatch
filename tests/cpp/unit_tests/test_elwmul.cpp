/*
 * Copyright © 2024 Advanced Micro Devices, Inc. All rights reserved.
 */

#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <tuple>

#include "../src/ops/ops_common/matmul_matrix.hpp"
#include <ops/elwmul/elwmul.hpp>
#include <stdexcept>

#include "enable_perf.hpp"

#include "test_common.hpp"

using namespace matmul_matrix;
template <typename LhsT = int16_t, typename RhsT = int16_t,
          typename OuT = int16_t>
int test_elwmul(size_t M, size_t K, bool debug = false,
                const std::string &a_dtype = "bfloat16",
                const std::string &b_dtype = "bfloat16",
                const std::string &c_dtype = "bfloat16",
                const std::string &model_name = "LLAMA2",
                const std::string &op_version = "v1") {
  int err_count = 0;
  size_t Ms = static_cast<size_t>(M);
  size_t Ks = static_cast<size_t>(K);

  std::vector<size_t> a_shape = {Ms, Ks};
  std::vector<size_t> b_shape = {Ms, Ks};

  std::vector<LhsT> a(M * K);
  std::vector<LhsT> b(M * K);
  std::vector<float> cpu_out(M * K);
  std::vector<OuT> aie_out(M * K, garbage_value);

  dd::initialize_random_bfloat16(a, 40);
  dd::initialize_random_bfloat16(b, 40);

  // compute golden
  for (int r = 0; r < M; r++) {
    for (int c = 0; c < K; c++) {
      cpu_out.at(r * K + c) = bfloat16_to_float(a.at(r * K + c)) *
                              bfloat16_to_float(b.at(r * K + c));
    }
  }

  std::map<std::string, std::any> attr;
  std::vector<int> size_matmul_M{1, 128, 256, 512, 1024, 2048};
  std::vector<std::vector<int>> shape_list;
  for (auto m : size_matmul_M) {
    if (K <= 14336) {
      shape_list.push_back({m, (int)K});
    } else {
      shape_list.push_back({m, 14336});
    }
  }
  attr["shapes"] = shape_list;
  attr["op_version"] = op_version;
  ryzenai::elw_mul elwmul_ =
      ryzenai::elw_mul<LhsT, RhsT, OuT>(a_dtype, true, attr);

  std::vector<Tensor> const_Tensor;
  std::vector<Tensor> input_Tensor;

  struct Tensor a_T = {a.data(), a_shape, a_dtype};
  struct Tensor b_T = {b.data(), a_shape, a_dtype};
  struct Tensor c_T = {aie_out.data(), a_shape, c_dtype};
  input_Tensor.push_back(a_T);
  input_Tensor.push_back(b_T);

  std::vector<Tensor> output_Tensor;
  output_Tensor.push_back(c_T);

  elwmul_.debug(debug);

#ifdef UNIT_TEST_PERF
  LOG_THIS("M = " << M << ", K = " << K);
  PROFILE_THIS(elwmul_.execute(input_Tensor, output_Tensor));
#else
  elwmul_.execute(input_Tensor, output_Tensor);
#endif

  err_count = dd::count_errors_floatvsbfloat16(cpu_out, aie_out, a_shape, 4);

  return err_count;
}

// ElwMUL v1
TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel2048x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      2048, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}
TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel128x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      128, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}
TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel256x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      256, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}
TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel512x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      512, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}
TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1024x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1024, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}
TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

// ElwMUL v1
TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel2048x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      2048, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}
TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel128x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      128, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}
TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel256x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      256, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}
TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel512x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      512, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}
TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1024x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1024, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}
TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

// new shapes

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel384x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      384, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel640x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      640, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel768x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      768, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel896x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      896, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1152x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1152, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1280x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1280, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1408x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1408, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1536x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1536, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1664x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1664, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1792x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1792, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1920x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1920, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel384x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      384, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel640x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      640, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel768x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      768, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel896x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      896, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1152x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1152, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1280x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1280, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1408x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1408, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1536x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1536, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1664x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1664, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1792x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1792, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel1920x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      1920, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

// tiling

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel4096x14336) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      4096, 14336, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel4096x11008) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      4096, 11008, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel2048x22016) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      2048, 22016, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel2048x28672) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      2048, 28672, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}

TEST(LLAMA2_ELWMUL_V1_Testa16, Kernel2048x20000) {
  int err_count = test_elwmul<uint16_t, uint16_t, uint16_t>(
      2048, 20000, false, "bfloat16", "bfloat16", "bfloat16", "LLAMA2", "v1");
  EXPECT_TRUE(err_count == 0) << "Error Count = " << err_count;
}
