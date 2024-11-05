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

#pragma once

#include <ops/op_interface.hpp>
#include <ops/ops_common.hpp>

namespace ryzenai {

/*
 * rms_norm is a class to offload matrix
 * RMS Norm to AIE. this class uses lite runtime stack to
 * interface with XRT
 */
template <typename LhsT, typename WtsT, typename OutT>
class rms_norm : public OpInterface {
private:
  std::map<std::string, std::string> txnbin_operand_header;
  std::map<std::string, std::vector<std::tuple<int, int>>> default_shapes_;
  /* BxMxK dimension of base elwmul being offloaded to AIE */
  int64_t kernel_x_shape_[2];
  /*Kernel shape selected in runtime*/
  /* actual BxMxK of matrix A */
  int64_t operand_shape_[3];
  size_t operand_size_in_bytes_;
  size_t wts_size_in_bytes_;
  /* xrt context handle */
  // xrt_context *xrt_ctx_;
  // static instruction_registry instr_reg_add_;
  static std::once_flag instr_reg_flag_;
  static std::once_flag instr_reg_v1_flag_;
  /* XRT BO for tiled LHS matrix */
  xrt::bo a_bo_;
  /* XRT BO for tiled RHS matrix */
  xrt::bo b_bo_;
  /* XRT BO for tiled OUT matrix */
  xrt::bo c_bo_;
  /* size for activation dtype*/
  int operand_dtype_size_;
  std::string instr_bo_key_;
  std::vector<std::pair<size_t, size_t>> thresholds_;

  /* variables to store profile data */
  int64_t a_copy_time_;
  int64_t a_sync_time_;
  int64_t b_copy_time_;
  int64_t b_sync_time_;
  int64_t c_copy_time_;
  int64_t c_sync_time_;
  int64_t run_aie_time_;
  int64_t num_run_aie_;
  uint64_t num_execute_ = 0;
  static std::once_flag logger_flag_;
  uint64_t rms_norm_id_;
  static uint64_t rms_norm_count;
  /* debug flag */
  bool debug_ = false;
  /*xclbin and mc_code selection variables*/
  std::string operand_dtype_;
  std::string txn_fname_prefix_;
  std::string param_fname_prefix_;
  std::string op_version_;
  /*
   * Utility function that setups for context.
   */
  void setup_instr_init();
  /*
   * Utility function that setups the instruction registry with transaction
   * binaries.
   */
  void setup_instr_registry(const std::map<std::string, std::any> &attr);
  /*
   * Utility function that checks if an operands shape is supported before
   * execution.
   */
  bool isSupportedShape(const Tensor &operand) const;

  std::string get_instr_key(std::string prefix, size_t m, size_t k) const;

public:
  rms_norm(const std::string &operand_dtype, bool load_xrt,
           const std::map<std::string, std::any> &attr = {});
  void execute(std::vector<Tensor> &input,
               std::vector<Tensor> &output) override;
  void execute(std::vector<xrt::bo> &input, std::vector<xrt::bo> &output,
               bool wait = true);
  void execute(std::vector<uint64_t> &input, std::vector<uint64_t> &output,
               bool wait = true);
  void debug(bool enable);
  std::vector<xrt::bo> get_inputs();
  std::vector<xrt::bo> get_outputs();

  void set_kernel_shape(const std::vector<size_t> &shape);
  const std::vector<uint8_t> get_transaction_bin(
      std::vector<Tensor> &input, std::vector<Tensor> &output,
      const std::map<std::string, std::any> &attr = {}) const override;
  const std::vector<uint8_t> get_super_kernel_params(
      std::vector<Tensor> &input, std::vector<Tensor> &output,
      const std::map<std::string, std::any> &attr = {}) const override;
  std::vector<OpArgMap> get_buffer_reqs(
      std::vector<Tensor> &input, std::vector<Tensor> &output,
      const std::map<std::string, std::any> &attr = {}) const override;
  void initialize_const_params(
      const std::vector<Tensor> &const_params,
      const std::map<std::string, std::any> &attr = {}) override {}
  void initialize_const_params(
      ConstBufferIO &io, const std::vector<Tensor> &const_params,
      const std::map<std::string, std::any> &attr = {}) override {}
  // TBD
  inline static float EPSILON = 0;
  bool load_xrt_;
};

} // namespace ryzenai
