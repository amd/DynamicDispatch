// Copyright (c) 2024 Advanced Micro Devices, Inc
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <algorithm>
#include <cassert>
#include <iostream>
#include <op_fuser/fusion_rt.hpp>
#include <utils/meta_utils.hpp>
#include <utils/utils.hpp>

#include "ops/ops_common/mladf_matmul_matrix.hpp"
#include <ops/mladfmatmulbias/mladfmatmulbias.hpp>

#include "mladfelwmul_helpers.hpp"
#include "mladfmatmulbias_helpers.hpp"
#include "mladfsilu_helpers.hpp"

using namespace std;

static int test_mmulsilu(const string &meta_json, size_t M, size_t K, size_t N,
                         bool debug = false, const string &x_dtype = "bfloat16",
                         const string &y_dtype = "bfloat16") {

  vector<size_t> x_shape = {1, M, K};
  vector<size_t> y_shape = {1, M, N};

  srand(42);

  cout << "Info: Constructing Op Simulators" << endl;
  mladfmatmulbias_helpers::MladfMatMulBias matmul0(M, K, N, 128);
  mladfsilu_helpers::SiLU silu(M, N);

  cout << "Info: Checking Shapes" << endl;
  assert(matmul0.c_golden.size() == silu.x.size());

  // Randomly initialize weights for all matmul
  cout << "Info: Randomizing Weights" << endl;
  matmul0.InitializeRandom();

  // Update weights files for all matmul
  cout << "Info: Writing Weights" << endl;
  std::string test_dir = "test_mmulsilu_abf16";

  {
    std::string weights_name = "0.const", bias_name = "1.const",
                scales_name = "2.const", zeros_name = "3.const";
    matmul0.WriteParams(test_dir, weights_name, bias_name, scales_name,
                        zeros_name);
  }

  cout << "Info: Simulating Matmul0" << endl;
  matmul0.ForwardCPU();

  cout << "Info: Copying Matmul0 Output to Silu" << endl;
  // Take float output from matmul0, and convert it to bfloat16 for silu code
  ryzenai::float_buffer_to_bfloat16(matmul0.c_golden.data(),
                                    matmul0.c_golden.size(),
                                    (uint16_t *)silu.x.data(), false);

  cout << "Info: Simulating Silu" << endl;
  silu.ForwardCPU();

  auto &x = matmul0.a;
  auto &y_golden = silu.y_golden; // Layer output
  vector<int16_t> y(M * N, 0x404A);

  const std::string xclbin_fname =
      Utils::get_env_var("DD_ROOT") +
      ryzenai::
          LLAMA2_MLADF_2x4x4_V1_GEMMBFP16_SILU_MUL_MHA_RMS_ROPE_XCLBIN_PATH;

  cout << "Info: Loading meta json" << endl;
  auto meta = OpsFusion::load_meta_json(meta_json);

  OpsFusion::FusionRuntime rt_cmp;
  OpsFusion::DDConfig cfg;
  auto xclbin_content = OpsFusion::read_bin_file<char>(xclbin_fname);
  cfg.xclbin_content = &xclbin_content;
  rt_cmp.compile(meta, "", cfg);
  rt_cmp.save_state("dd_metastate");

  OpsFusion::FusionRuntime rt(xclbin_fname, xclbin_content);
  rt.load_state("dd_metastate");
  rt.init(meta);

  vector<Tensor> input_Tensors = {{x.data(), x_shape, x_dtype}};
  vector<Tensor> output_Tensors = {{y.data(), y_shape, y_dtype}};

  cout << "Info: Executing on Hardware" << endl;
  rt.execute(input_Tensors, output_Tensors);

  cout << "Info: Expected Values (first 16):" << endl;
  for (int i = 0; i < 16; ++i) {
    cout << y_golden[i] << " ";
  }
  cout << endl;

  cout << "Info: Recieved Values (first 16):" << endl;
  for (int i = 0; i < 16; ++i) {
    cout << ryzenai::bfloat16_to_float(y[i]) << " ";
  }
  cout << endl;

  float const EPSILON = 512.0;
  int err_count = 0;
  float err_max = 0;
  float err_min = 0;
  float err_total = 0;

  for (int i = 0; i < y.size(); i++) {
    float err = abs(ryzenai::bfloat16_rnd_even(y_golden[i]) -
                    ryzenai::bfloat16_to_float(y[i]));
    if (abs(err_max) < abs(err)) {
      err_max = err;
    }
    if (i == 0) {
      err_min = err;
    } else if (abs(err_min) > abs(err)) {
      err_min = err;
    }
    err_total += err;
    if ((err > EPSILON) || std::isnan(err)) {
      cout << dec << "y[" << i << "]: "
           << "Err: " << err << ", "
           << "Expected: " << ryzenai::bfloat16_rnd_even(y_golden[i]) << ", "
           << "Received: " << ryzenai::bfloat16_to_float(y[i]) << "\n";
      err_count++;
    }
  }
  cout << "Info: err_count: " << err_count << endl;
  cout << "Info: err_max: " << err_max << endl;
  cout << "Info: err_min: " << err_min << endl;
  cout << "Info: err_total: " << err_total << endl;
  cout << "Info: err_mean: " << 1.0 * err_total / y_golden.size() << endl;
  return err_count;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    cout << "Usage : test_mmulsilu.exe <meta.json>" << endl;
    return EXIT_FAILURE;
  }

  size_t M = 1;
  size_t K = 4096;
  size_t N = 11008;
  try {
    string meta_json = string(argv[1]);

    int err_count = 0;
    err_count = test_mmulsilu(meta_json, M, K, N, false);
    if (err_count > 0) {
      cout << "mmulsilu test failed with err_count = " << err_count << endl;
      return EXIT_FAILURE;
    }
  } catch (exception &e) {
    cout << e.what() << endl;
    return EXIT_FAILURE;
  } catch (...) {
    // Catch any other exceptions
    cout << "Caught an unknown exception" << endl;
    return EXIT_FAILURE;
  }

  cout << "Finished Successfully" << endl;
  return EXIT_SUCCESS;
}