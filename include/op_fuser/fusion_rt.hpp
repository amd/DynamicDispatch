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

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <op_fuser/fuse_ops.hpp>
#include <ops/op_interface.hpp>

namespace OpsFusion {
struct Metadata;

struct SimpleSpan {
  char *loc;
  size_t size_in_bytes;
};

struct DDConfig {
  uint32_t profile =
      0; // pass profile level. 0 - None, 1 - subgraph, 2 - subgraph+PDI
         // partition, 3 - subgraph + PDI partition + ops
  bool pm_swap = false;
  bool optimize_scratch = true;
  // use fused transaction, but run each op serially
  bool eager_mode = false;
  // Cache dir containinig dd artifacts
  std::string cache_dir;
  // Key for accessing meta info
  std::string model_name = "";
  std::vector<char> *xclbin_content;
};

class FusionRuntime {
public:
  FusionRuntime();
  FusionRuntime(const std::string &xclbin_filename);
  FusionRuntime(const std::string &xclbin_filename,
                const std::vector<char> &xclbin_content,
                const std::string &kernel_name_prefix = "DPU",
                const std::map<std::string, std::uint32_t> &qos = {});
  FusionRuntime(xrt::hw_context *ctx,
                const std::string &kernel_name_prefix = "DPU");
  ~FusionRuntime();
  // Do not allow copying or assignment
  // to prevent instruction BO for hw_context to grow
  FusionRuntime(const FusionRuntime &) = delete;
  FusionRuntime &operator=(const FusionRuntime &) = delete;
  void execute(const std::vector<Tensor> &inputs,
               const std::vector<Tensor> &outputs);
  void compile(const Metadata &meta, const std::string &base_dir = "",
               const DDConfig &cfg = {},
               std::map<std::string, SimpleSpan> const_map = {});

  /// @brief initialize the FusionRT for execution
  /// @param meta MetaJson Object
  /// @param base_dir Path to root directory of DD (used to load txn_bin etc)
  /// @param cache_dir Path to cache directory containing meta.json & *.const
  void init(const Metadata &meta, const std::string &base_dir = "",
            const DDConfig &cfg = {});

  std::vector<std::vector<uint8_t>> get_txns();
  const Metadata &get_meta() const;

  // Unpack internal buffers of RT
  // This is useful for debugging after the execution.
  // Its result is meaningful only after execution.
  // TODO : Testing
  std::map<std::string, std::vector<uint8_t>>
  unpack_internal_buffers(const std::string &dir = "");

  // Save MetaState
  void save_state(const std::string &state_name,
                  save_function save_func = nullptr);
  void load_state(const std::string &state_name,
                  load_function load_func = nullptr);

private:
  void load_const(const Metadata &meta,
                  std::map<std::string, SimpleSpan> &const_map);
  void fill_super_instr(const Metadata &meta,
                        std::map<std::string, SimpleSpan> &const_map);
  void fill_ctrl_pkts(const Metadata &meta);
  void setup_xrt_run(const Metadata &meta);
  void split_outputs(const std::vector<Tensor> &outputs, const Metadata &meta);
  void merge_inputs(const std::vector<Tensor> &inputs, const Metadata &meta);
  std::vector<std::vector<uint8_t>> generate_fused_txns(const Metadata &meta);
  bool check_context_instr_size(
      const std::vector<std::vector<uint8_t>> &fused_instr_vec,
      const size_t limit);
  bool check_partition_instr_size(
      const std::vector<std::vector<uint8_t>> &fused_instr_vec,
      const size_t partition_limit);
  bool
  allocate_instr_bos(const std::vector<std::vector<uint8_t>> &fused_instr_vec);
  void
  populate_instr_bos(const std::vector<std::vector<uint8_t>> &fused_instr_vec);
  void reallocate_xrt_bos(const Metadata &meta);
  void initialize_inputs(const Metadata &meta);
  void save_buffer_objects();
  void save_bo(xrt::bo &bo, const std::string filename);
  void save_patched_txns();
  void prepare_formatting_ops();
  void patch_ctrl_pkt(const Metadata &meta);

  bool parse_xclbin_metadata(OpPDIMap &op_pdi_map,
                             const std::string &model_name,
                             const xrt::xclbin &xclbin);

  // Parse xclbin metadata from hw context (useful at run time)
  bool parse_xclbin_metadata(OpPDIMap &op_pdi_map,
                             const std::string &model_name);

  // Parse xclbin metadata from xclbin file directly (useful at compile time)
  bool parse_xclbin_metadata(OpPDIMap &op_pdi_map,
                             const std::string &model_name,
                             const std::vector<char> *xclbin_content);
  void host_to_dev_memcpy();
  void initialize_runtime();

  // Device Independent APIs.
  void allocate_host_bos(const Metadata &meta);

private:
  static std::once_flag logger_flag_;

  bool cpu_only_runtime_ = false;
  bool use_external_ctx_ = false;

  std::string xclbin_filename_;
  std::vector<char> xclbin_content_;
  std::map<std::string, std::uint32_t> qos_;
  std::shared_ptr<ryzenai::dynamic_dispatch::xrt_context> xrt_ctx_;
  // External Context
  xrt::hw_context ctx_;
  std::vector<xrt::kernel> kernels_;
  std::vector<xrt::run> runs_;

  Metadata meta_;
  std::vector<xrt::bo> instr_bos_;
  xrt::bo input_bo_;
  xrt::bo output_bo_;
  xrt::bo scratch_bo_;
  xrt::bo const_bo_;
  xrt::bo super_instr_bo_;

  // Host buffers.
  FILE *input_vec_file_ptr_;
  FILE *const_vec_file_ptr_;
  std::vector<uint8_t> output_vec_;
  std::vector<uint8_t> scratch_vec_;
  FILE *super_instr_vec_file_ptr_;
  FILE *ctrl_pkt_vec_file_ptr_;

  // Config
  DDConfig cfg_;

  // TODO : calling .size() on an empty bo throws exception
  size_t instr_bo_sz_{0};
  size_t input_bo_sz_{0};
  size_t output_bo_sz_{0};
  size_t scratch_bo_sz_{0};
  size_t const_bo_sz_{0};
  size_t super_instr_bo_sz_{0};
  // TODO: can we only keep fused_instr_vec_ ??
  std::vector<std::vector<uint8_t>> txns_;
  std::vector<std::vector<uint8_t>> fused_instr_vec_;

  // Timers
  int64_t input_copy_time_{0};
  int64_t input_sync_time_{0};
  int64_t output_copy_time_{0};
  int64_t output_sync_time_{0};
  int64_t xrt_exec_time_{0};

  // make copying to input BO, from output BO thread safe
  std::mutex execute_mutex_;
  std::mutex load_save_state_mutex_;
  // Fallback to dynamically updating instr bo
  bool use_instr_sw_cache_;

  // procucer_ops_ = [{op_info1, op1}, {op_info2, op2}, ...]
  std::vector<
      std::pair<OpsFusion::Metadata::OpInfo, std::unique_ptr<OpInterface>>>
      producer_ops_;

  std::map<std::string, std::uint32_t> kernel_name_to_pdi_idx_;
  bool use_xclbin_parse_data_;
  OpPDIMap op_pdi_map_;
  const OpPDIMap DEFAULT_OP_TO_PDI_MAP_ = {
      {{{"Add", 0},
        {"BMM1", 0},
        {"BMM2", 0},
        {"DQAdd", 0},
        {"LayerNorm", 0},
        {"QLayerNorm", 0},
        {"QGroupNorm", 0},
        {"MatMul", 0},
        {"QMatMul", 0},
        {"MatMulAdd", 0},
        {"QMatMulAdd", 0},
        {"MatMulAddGelu", 0},
        {"QMatMulAddGelu", 0},
        {"MladfMatMul", 0},
        {"MHAGRPB", 0},
        {"QMHAGRPB", 0},
        {"QEltWiseAdd", 0},
        {"QMHAWINDOW", 0},
        {"QMHACHANNEL", 0},
        {"QELWEMUL_qdq", 0},
        {"QELWEMUL_mxgan", 2},
        {"SILU", 0},
        {"GELU", 0},
        {"QuantOP", 0},
        {"DeQuantOP", 0},
        {"ELWMUL", 0},
        {"MLADFADD", 0},
        {"MLADFRMSNORM", 0},
        {"MASKEDSOFTMAX", 0},
        {"MLADFMHAROPE", 0},
        {"QConv", 1},
        {"QL2norm", 2},
        {"QConcateOPs", 0},
        {"IConv", 0},
        {"QReshapeTranspose", 0},
        {"square", 0},
        {"cube", 0},
        {"QMHA", 0},
        {"QGlobalAvgPool", 1},
        {"xcom-conv2d", 0},
        {"QConv2MatMul", 0},
        {"QMatMulDynamic", 0},
        {"QMatMulDynamicSoftmax", 0},
        {"QMulSoftmax", 0},
        {"mzdk5MHA", 0},
        {"QSilu", 0},
        {"QSlice", 0},
        {"QConcat", 0},
        {"QResize", 0},
        {"DPS", 2},
        {"QBroadcastAdd", 0},
        {"QGelu", 0},
        {"Mladfsoftmax", 0},
        {"MLADFMATMULA16A16", 0},
        {"MLADFMATMULA16W8", 0},
        {"QLstm", 0},
        {"QSigmoid", 2},
        {"Mladfelwadd", 0},
        {"QL2norm", 2},
        {"AttentionMaskPrePro", 2},
        {"Qtanh_lpnorm", 2},
        {"Qbias_add", 2},
        {"QActConstAdd", 0},
        {"QExpand", 2},
        {"QEltWiseDiv", 2},
        {"QReduceSum", 2},
        {"Mladfelwmul", 0},
        {"SDConv", 0}}},
      {{{0, "DPU"}, {1, "DPU_1"}, {2, "DPU_2"}}}};
};

class FusionSubgraphRuntime {
public:
  FusionSubgraphRuntime(const std::string &xclbin,
                        const std::string &kernel_name = "DPU",
                        std::uint32_t num_parallel_contexts = 2);
  FusionSubgraphRuntime(std::vector<xrt::hw_context> &ctxs,
                        const std::string &kernel_name = "DPU");
  ~FusionSubgraphRuntime();
  // Do not allow copying or assignment
  // to prevent instruction BO for hw_context to grow
  FusionSubgraphRuntime(const FusionSubgraphRuntime &) = delete;
  FusionSubgraphRuntime &operator=(const FusionSubgraphRuntime &) = delete;

  void init(std::vector<Tensor> &tensors,
            const std::map<std::string, std::any> attr);

  void execute_subgraph(const std::vector<Tensor> &inputs,
                        const std::vector<Tensor> &outputs);

private:
  void allocate_data_bos(const std::vector<OpArgMap> &arg_map);
  void init_const_inputs(const std::vector<Tensor> &tensors,
                         const std::map<std::string, std::any> &attr);
  void setup_instr_bo(std::vector<uint8_t> &txn);
  void setup_xrt_runlist(const std::map<std::string, std::any> &attr);
  void copy_input(const std::vector<Tensor> &inputs);

  size_t num_contexts_;
  // External Context
  std::vector<xrt::hw_context> ctxs_;
  std::vector<xrt::kernel> kernels_;
  std::vector<xrt::run> runs_;
#ifdef XRT_RUNLIST_EN
  std::vector<xrt::runlist> runlists_;
#endif

  std::vector<size_t> sched_num_tiles_per_context_;
  std::vector<size_t> prefix_sum_sched_num_tiles_;

  std::vector<xrt::bo> instr_bos_;
  xrt::bo input_bo_;
  xrt::bo output_bo_;
  std::vector<xrt::bo> scratch_bos_;
  xrt::bo const_bo_;

  std::map<std::string, std::any> attr_;

  // make copying to input BO, from output BO thread safe
  std::mutex execute_mutex_;
};

} // namespace OpsFusion