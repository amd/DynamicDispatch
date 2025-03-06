// Copyright (c) 2025 Advanced Micro Devices, Inc
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

#include <op_fuser/fuse_types.hpp>
#include <ops/op_interface.hpp>
#include <utils/tfuncs.hpp>

namespace OpsFusion {

class MetaUtils {
public:
  /// @brief Get a summary of the metdata like number of each ops, total memory
  /// consumption etc.
  static std::string get_summary(const Metadata &meta);

  /// @brief Get the input tensors of the metadata
  static std::vector<Tensor> get_input_tensors(const Metadata &meta);

  /// @brief Get the output tensors of the metadata
  static std::vector<Tensor> get_output_tensors(const Metadata &meta);

  /// @brief Get all the const tensors of the metadata
  static std::vector<Tensor> get_const_tensors(const Metadata &meta);

  /// @brief Get number of input tensors of the metadata
  static size_t get_num_inputs(const Metadata &meta);

  /// @brief Get number of output tensors of the metadata
  static size_t get_num_outputs(const Metadata &meta);

  /// @brief Load all the constant data from binary files associated with an op
  static std::map<std::string, std::vector<char>>
  load_op_const_buffers(const Metadata &meta, const Metadata::OpInfo &op_info);

  /// @brief Load all the constant data from binary files in the whole meta
  static std::map<std::string, std::vector<char>>
  load_const_buffers(const Metadata &meta);

  static std::map<std::string, std::vector<char>>
  load_const_buffers_from_big_file(const Metadata &meta);

  /// @brief Get a list of args of an Op as Tensors. If const_buffer_ptrs are
  /// passed, Tensor::data will point to a valid buffer in const_buffer_ptrs,
  /// otherwise, Tensor::data will be nullptr by default.
  /// MetaUtils::load_op_const_buffers() can be used to get const buffers for an
  /// op.
  static std::vector<Tensor> collect_op_tensors(
      const Metadata &meta, const Metadata::OpInfo &op_info,
      const std::map<std::string, void *> &const_buffer_ptrs = {});

private:
  static std::vector<Tensor> get_tensors(const Metadata &meta,
                                         OpArgMap::OpArgType arg_type);
  static size_t get_num_tensors(const Metadata &meta,
                                OpArgMap::OpArgType arg_type);
};
} // namespace OpsFusion
