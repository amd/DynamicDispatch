/*
 * Copyright © 2023 Advanced Micro Devices, Inc. All rights reserved.
 */

#include <any>
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <tuple>
#include <utility>

// XRT headers
#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"

// dpu kernel metadata
#include <utils/dpu_mdata.hpp>

#include <txn_container.hpp>
#include <utils/instruction_registry.hpp>
#include <xrt_context/xrt_context.hpp>

#include <ops/mhawindow/mhawindow.hpp>
#include <ops/op_interface.hpp>
#include <utils/logging.hpp>
#include <utils/utils.hpp>

// AIE Driver header
#include "xaiengine.h"

#include "ops/ops_common/mhagprb_matrix.hpp"

namespace ryzenai {

static std::array<size_t, 3> extract_shape(const Tensor &tensor) {
  std::array<size_t, 3> res;
  if (tensor.shape.size() == 3) {
    res = {tensor.shape.at(0), tensor.shape.at(1), tensor.shape.at(2)};
  } else {
    throw std::runtime_error("MHAWINDOW : Invalid shape received for Matrix");
  }
  return res;
}

template <typename InT, typename WtT, typename OutT>
std::once_flag mhawindow<InT, WtT, OutT>::instr_reg_flag_;

template <typename InT, typename WtT, typename OutT>
void mhawindow<InT, WtT, OutT>::debug(bool enable) {
  debug_ = enable;
}

template <typename InT, typename WtT, typename OutT>
std::string
mhawindow<InT, WtT, OutT>::get_instr_key(std::string prefix,
                                         std::vector<size_t> &mat) const {
  std::string out_str = "mhawindow_" + prefix;
  for (size_t i = 0; i < mat.size(); i++) {
    out_str += "_" + std::to_string(mat[i]);
  }
  return out_str;
}

template <typename InT, typename WtT, typename OutT>
std::once_flag mhawindow<InT, WtT, OutT>::logger_flag_;

template <typename InT, typename WtT, typename OutT>
uint64_t mhawindow<InT, WtT, OutT>::mhawindow_count = 0;

template <typename InT, typename WtT, typename OutT>
void mhawindow<InT, WtT, OutT>::setup_instr_registry() {
  std::vector<std::pair<std::string, bool>> instructions;
  std::vector<std::pair<std::string, bool>> layer_params;
  // mhawindow
  std::vector<std::vector<size_t>> supported_shapes =
      default_shapes_.find(txn_fname_prefix_)->second;
  for (size_t i = 0; i < supported_shapes.size(); i++) {
    auto mat = supported_shapes.at(i);
    auto key = get_instr_key(txn_fname_prefix_, mat);
    auto param_key = get_instr_key(param_fname_prefix_, mat) + "_param";
    instructions.push_back(std::make_pair(key, false));
    layer_params.push_back(std::make_pair(param_key, false));
  }

  xrt_ctx_->get_registry().add_instructions(instructions);
  xrt_ctx_->get_registry().add_layer_params(layer_params);
}

template <typename InT, typename WtT, typename OutT>
mhawindow<InT, WtT, OutT>::mhawindow(const std::string &a_dtype,
                                     const std::string &b_dtype,
                                     const std::string &c_dtype,
                                     bool load_xrt) {

  txnbin_a_header = {{"uint16", "a16"}, {"uint8", "a8"}};

  txnbin_b_header = {{"uint16", "w16"}, {"uint8", "w8"}};

  txnbin_acc_header = {{"uint16", "acc16"}, {"uint8", "acc8"}};

  default_shapes_["mhawindow_a16w8acc16"] = std::vector<std::vector<size_t>>();
  default_shapes_["mhawindow_a16w8acc16"].push_back(
      std::vector<size_t>{64, 49, 384});
  default_shapes_["mhawindow_a16w8acc16"].push_back(
      std::vector<size_t>{16, 49, 768});
  default_shapes_["mhawindow_a16w8acc16"].push_back(
      std::vector<size_t>{4, 49, 1536});
  default_shapes_["mhawindow_a16w8acc16"].push_back(
      std::vector<size_t>{1, 49, 3072});

  a_dtype_ = a_dtype;
  b_dtype_ = b_dtype;
  c_dtype_ = c_dtype;

  a_dtype_size_ = sizeof(InT);
  b_dtype_size_ = sizeof(WtT);
  c_dtype_size_ = sizeof(OutT);

  mhawindow_id_ = mhawindow_count++;

  /*select xclbin based on the input/output types*/
  std::string XCLBIN_FNAME =
      OpInterface::get_dd_base_dir() + ryzenai::mdsqr_A8W8_QDQ_XCLBIN_PATH;

  if (a_dtype_ == "uint16") {
    XCLBIN_FNAME =
        OpInterface::get_dd_base_dir() + ryzenai::mxpzi_A16W8_QDQ_XCLBIN_PATH;
  }

  txn_fname_prefix_ = "mhawindow_" + txnbin_a_header.at(a_dtype_) +
                      txnbin_b_header.at(b_dtype_) +
                      txnbin_acc_header.at(c_dtype_);

  param_fname_prefix_ = "mhawindow_" + txnbin_a_header.at(a_dtype_) +
                        txnbin_b_header.at(b_dtype_) +
                        txnbin_acc_header.at(c_dtype_);

  KERNEL_M_MAX = 512;

  if (load_xrt == true) {
    xrt_ctx_ = dynamic_dispatch::xrt_context::get_instance(XCLBIN_FNAME);

    std::call_once(instr_reg_flag_, [this]() { setup_instr_registry(); });
  }

  a_copy_time_ = 0;
  a_sync_time_ = 0;
  b_copy_time_ = 0;
  b_format_time_ = 0;
  b_sync_time_ = 0;
  c_copy_time_ = 0;
  c_sync_time_ = 0;
  run_aie_time_ = 0;
  cpu_acc_time_ = 0;
  num_run_aie_ = 0;

  std::call_once(logger_flag_, []() {
    std::string header =
        "ipu_wrapper_id M K N kernel_m kernel_k kernel_n Execute"
        "time(us) num_aie_runs run_aie_time(ns) "
        "A_copy_time(ns) A_sync_time(ns) "
        "C_copy_time(ns) C_sync_time(ns) "
        "Avg_time_per_aie_run(ns)\n";
    RYZENAI_LOG_INFO(header);
  });

  RYZENAI_LOG_TRACE("[MHAWINDOW] ID: " + std::to_string(mhawindow_id_) +
                    ", XCLBIN: " + XCLBIN_FNAME +
                    ", (a_dtype, b_dtype, c_dtype): (" + a_dtype + ", " +
                    b_dtype + ", " + c_dtype + ")");
}

template <typename InT, typename WtT, typename OutT>
void mhawindow<InT, WtT, OutT>::set_params(const std::string &model_name,
                                           std::vector<size_t> input_shape) {
  std::string XCLBIN_FNAME;

  if (model_name == "m3uec") {
    XCLBIN_FNAME =
        OpInterface::get_dd_base_dir() + ryzenai::m3uec_A16W8_QDQ_XCLBIN_PATH;
  } else {
    throw std::invalid_argument("model_name is not supported");
  }

  kernel_x_shape_[0] = input_shape.at(0);
  kernel_x_shape_[1] = input_shape.at(1);
  kernel_x_shape_[2] = input_shape.at(2);

  xrt_ctx_ = dynamic_dispatch::xrt_context::get_instance(XCLBIN_FNAME);
  std::call_once(instr_reg_flag_, [this]() { setup_instr_registry(); });
}

template <typename InT, typename WtT, typename OutT>
void mhawindow<InT, WtT, OutT>::initialize_const_params(
    ConstBufferIO &io, const std::vector<Tensor> &const_params,
    const std::map<std::string, std::any> &attr) {
  RYZENAI_LOG_TRACE("MHAWINDOW initialize_const_params(ptr) ...");
  DD_THROW_IF(
      (const_params.size() != 1) || (const_params.at(0).shape.size() != 2),
      OpsFusion::dd_format(
          "Unsupported const spec for MHAWINDOW\n"
          "(Details : #const params == 1 ({}), Const param1 dim == 2 ({})",
          const_params.size(), const_params.at(0).shape.size()));
  const int qdq_idx = 0;

  auto qdq_param = (int32_t *)const_params.at(qdq_idx).data;

  int size_qdqparam = QDQparam_size * num_qdq_nodes * sizeof(int32_t);

  qdq_param[(16 * 2) + qdq_Mv_idx] = mha_win_st_pad;
  qdq_param[(16 * 2) + qdq_Nv_idx] = mha_win_st_pad;

  qdq_param[(16 * 3) + qdq_Mv_idx] = mha_win_st_pad;
  qdq_param[(16 * 3) + qdq_Nv_idx] = mha_win_val_subv_cols;

  io.write(0, (void *)qdq_param, size_qdqparam);

  RYZENAI_LOG_TRACE("MHAWINDOW initialize_const_params(ptr) ... DONE");
}

// For MHAWINDOW
template <typename InT, typename WtT, typename OutT>
void mhawindow<InT, WtT, OutT>::initialize_const_params(
    const std::vector<Tensor> &const_params,
    const std::map<std::string, std::any> &attr) {
  // Check the number of inputs
  DD_ASSERT((const_params.size() == 1),
            OpsFusion::dd_format("MHAWINDOW expects one constant. Got {}",
                                 const_params.size()));

  int size_qdqparam = QDQparam_size * num_qdq_nodes * sizeof(int32_t);

  // Create input/output BOs
  const size_t A_BO_SIZE =
      (kernel_x_shape_[0] * kernel_x_shape_[1] * kernel_x_shape_[2] *
       a_dtype_size_); // TODO:: add batch dimension also
  const size_t B_BO_SIZE = size_qdqparam;
  const size_t C_BO_SIZE =
      (kernel_x_shape_[0] * kernel_x_shape_[1] * kernel_x_shape_[2] *
       c_dtype_size_ / 3); // TODO: add batch dimension also

  RYZENAI_LOG_TRACE("MHAWINDOW: A_BO_SIZE:" + std::to_string(A_BO_SIZE) +
                    " B_BO_SIZE:" + std::to_string(B_BO_SIZE) +
                    " C_BO_SIZE size:" + std::to_string(C_BO_SIZE));

  a_bo_ = xrt::bo(xrt_ctx_->get_device(), A_BO_SIZE, XRT_BO_FLAGS_HOST_ONLY,
                  xrt_ctx_->get_kernel().group_id(8));
  b_bo_ = xrt::bo(xrt_ctx_->get_device(), B_BO_SIZE, XRT_BO_FLAGS_HOST_ONLY,
                  xrt_ctx_->get_kernel().group_id(8));
  c_bo_ = xrt::bo(xrt_ctx_->get_device(), C_BO_SIZE, XRT_BO_FLAGS_HOST_ONLY,
                  xrt_ctx_->get_kernel().group_id(8));

  // copy b_bo
  b_copy_time_ = 0;
  b_format_time_ = 0;
  b_sync_time_ = 0;
  auto b_copy_start = GET_ELAPSED_TIME_NS();
  auto b_format_start = GET_ELAPSED_TIME_NS();
  WtT *b_bo_map = b_bo_.map<WtT *>();
  auto bo_const = BoConst(b_bo_map);
  initialize_const_params(bo_const, const_params);
  auto b_format_stop = GET_ELAPSED_TIME_NS();
  auto b_copy_stop = GET_ELAPSED_TIME_NS();
  b_format_time_ += static_cast<int64_t>(b_format_stop - b_format_start);
  b_copy_time_ = static_cast<int64_t>(b_copy_stop - b_copy_start);

  // sync b_bo
  auto b_sync_start = GET_ELAPSED_TIME_NS();
  b_bo_.sync(XCL_BO_SYNC_BO_TO_DEVICE);
  auto b_sync_stop = GET_ELAPSED_TIME_NS();
  b_sync_time_ = static_cast<int64_t>(b_sync_stop - b_sync_start);
}

// Q+K+V
template <typename InT, typename WtT, typename OutT>
void mhawindow<InT, WtT, OutT>::execute(std::vector<Tensor> &input,
                                        std::vector<Tensor> &output) {
  // Check the number of inputs
  if (input.size() != 1) {
    throw std::runtime_error("MHAWINDOW IPU Wrapper expect to have one input.");
  }
  const int qkv_idx = 0;
  InT *a = (InT *)input.at(qkv_idx).data;

  a_copy_time_ = 0;
  a_sync_time_ = 0;
  b_copy_time_ = 0;
  b_format_time_ = 0;
  b_sync_time_ = 0;
  c_copy_time_ = 0;
  c_sync_time_ = 0;
  run_aie_time_ = 0;
  cpu_acc_time_ = 0;
  auto exec_start = GET_ELAPSED_TIME_NS();
  size_t M, K, N;

  M = input.at(qkv_idx).shape.at(0);
  K = input.at(qkv_idx).shape.at(1);
  N = input.at(qkv_idx).shape.at(2);

  c_shape_[0] = M * K * N / 3;

  kernel_x_rows = N;
  // a_bo copy
  auto a_copy_start = GET_ELAPSED_TIME_NS();
  InT *a_bo_map = a_bo_.map<InT *>();
  size_t a_size = M * K * N * sizeof(InT);
  memcpy((void *)a_bo_map, (void *)a, a_size);

  auto a_copy_stop = GET_ELAPSED_TIME_NS();

  // a_bo sync
  auto a_sync_start = GET_ELAPSED_TIME_NS();
  a_bo_.sync(XCL_BO_SYNC_BO_TO_DEVICE);
  auto a_sync_stop = GET_ELAPSED_TIME_NS();

  a_copy_time_ = static_cast<int64_t>(a_copy_stop - a_copy_start);
  a_sync_time_ = static_cast<int64_t>(a_sync_stop - a_sync_start);

  w_shape_[0] = M;
  w_shape_[1] = K;
  // prepare inst_bo and param_bo
  std::vector<size_t> param_shape = {M, K, N};
  auto instr_bo_key = get_instr_key(txn_fname_prefix_, param_shape);
  auto param_bo_key =
      get_instr_key(param_fname_prefix_, param_shape) + "_param";

  auto instr_bo = xrt_ctx_->get_registry().get_instr_bo(instr_bo_key);
  const xrt::bo &param_bo =
      xrt_ctx_->get_registry().get_param_bo(param_bo_key).second;
  uint32_t instr_bo_words = uint32_t(instr_bo.size() / sizeof(int));
  auto kernel_ = xrt_ctx_->get_kernel();
  // launch the kernel
  xrt::run run;
  auto run_aie_start = GET_ELAPSED_TIME_NS();
  c_bo_.sync(XCL_BO_SYNC_BO_TO_DEVICE);

  run = kernel_(2, instr_bo, instr_bo_words,
                c_bo_.address() + DDR_AIE_ADDR_OFFSET,
                a_bo_.address() + DDR_AIE_ADDR_OFFSET,
                b_bo_.address() + DDR_AIE_ADDR_OFFSET,
                param_bo.address() + DDR_AIE_ADDR_OFFSET, 0);

  run.wait2();

  auto run_aie_stop = GET_ELAPSED_TIME_NS();
  run_aie_time_ += static_cast<int64_t>(run_aie_stop - run_aie_start);
  num_run_aie_++;
  // sync output activation to host memory
  auto c_sync_start = GET_ELAPSED_TIME_NS();
  c_bo_.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
  auto c_sync_stop = GET_ELAPSED_TIME_NS();
  c_sync_time_ += static_cast<int64_t>(c_sync_stop - c_sync_start);
  // copy c_bo to host memory
  auto aie_out = (OutT *)output.at(0).data;
  auto c_copy_start = GET_ELAPSED_TIME_NS();
  OutT *c_bo_map = c_bo_.map<OutT *>();
  memcpy((void *)aie_out, (void *)c_bo_map, c_shape_[0] * sizeof(OutT));
  auto c_copy_stop = GET_ELAPSED_TIME_NS();
  c_copy_time_ = static_cast<int64_t>(c_copy_stop - c_copy_start);
  auto exec_end = GET_ELAPSED_TIME_NS();
  RYZENAI_LOG_INFO(
      std::to_string(mhawindow_id_) + " " + std::to_string(a_shape_[0]) + " " +
      std::to_string(a_shape_[1]) + " " + std::to_string(w_shape_[1]) + " " +
      std::to_string(kernel_x_rows) + " " + std::to_string(kernel_x_shape_[1]) +
      " " + std::to_string(kernel_y_shape_[1]) + " " +
      std::to_string(exec_end - exec_start) + " " +
      std::to_string(num_run_aie_) + " " + std::to_string(run_aie_time_) + " " +
      std::to_string(a_copy_time_) + " " + std::to_string(a_sync_time_) + " " +
      std::to_string(c_copy_time_) + " " + std::to_string(c_sync_time_) + " " +
      std::to_string((double)run_aie_time_ / num_run_aie_) + "\n");
}

template <typename InT, typename WtT, typename OutT>
const std::vector<uint8_t> mhawindow<InT, WtT, OutT>::get_transaction_bin(
    std::vector<Tensor> &input, std::vector<Tensor> &output,
    const std::map<std::string, std::any> &attr) const {
  auto QKV_shape = extract_shape(input.at(0));

  std::vector<size_t> param_shape = {QKV_shape[0], QKV_shape[1], QKV_shape[2]};
  std::string txn_key = get_instr_key(txn_fname_prefix_, param_shape);
  Transaction &txn = Transaction::getInstance();
  return txn.get_txn_bvec(txn_key);
}

template <typename InT, typename WtT, typename OutT>
const std::vector<uint8_t> mhawindow<InT, WtT, OutT>::get_super_kernel_params(
    std::vector<Tensor> &input, std::vector<Tensor> &output,
    const std::map<std::string, std::any> &attr) const {
  auto QKV_shape = extract_shape(input.at(0));

  std::vector<size_t> param_shape = {QKV_shape[0], QKV_shape[1], QKV_shape[2]};
  std::string param_key =
      get_instr_key(param_fname_prefix_, param_shape) + "_param";
  // std::cout << "Super kernel params name : " << fname << std::endl;

  Transaction &txn = Transaction::getInstance();
  return txn.get_txn_bvec(param_key);
}

template <typename InT, typename WtT, typename OutT>
std::vector<OpArgMap> mhawindow<InT, WtT, OutT>::get_buffer_reqs(
    std::vector<Tensor> &input, std::vector<Tensor> &output,
    const std::map<std::string, std::any> &attr) const {
  // [QKV, qdq_params]
  if (input.size() != 3) {
    throw std::runtime_error("MHAWINDOW: Incorrect number of tensors received");
  }

  auto QKV_shape = extract_shape(input.at(0));

  size_t size_qdqparam = QDQparam_size * num_qdq_nodes * sizeof(int32_t);

  size_t QKV_size = (std::accumulate(QKV_shape.begin(), QKV_shape.end(),
                                     size_t{1}, std::multiplies{}) *
                     sizeof(InT));

  size_t out_size = (std::accumulate(QKV_shape.begin(), QKV_shape.end(),
                                     size_t{1}, std::multiplies{}) *
                     sizeof(OutT) / 3);

  size_t super_kernel_size = get_super_kernel_params(input, output).size();

  std::vector<OpArgMap> arg_map{
      {OpArgMap::OpArgType::INPUT, 1, 0, 0, QKV_size},
      {OpArgMap::OpArgType::CONST_INPUT, 2, 1, 0, size_qdqparam},
      {OpArgMap::OpArgType::OUTPUT, 0, 2, 0, out_size},
      {OpArgMap::OpArgType::CONST_KERNEL_PARAM_INPUT, 3, 0, 0,
       super_kernel_size}};

  return arg_map;
}

template class mhawindow<uint16_t, uint8_t, uint16_t>;
} // namespace ryzenai
