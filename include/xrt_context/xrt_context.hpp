/*
 * Copyright © 2023 Advanced Micro Devices, Inc. All rights reserved.
 */
#pragma once
#ifndef XRT_CONTEXT_HPP__
#define XRT_CONTEXT_HPP__

#ifdef _WIN32
#ifdef DYNAMIC_DISPATCH_BUILD_SHARED
#ifdef DYNAMIC_DISPATCH_EXPORT
#define DYNAMIC_DISPATCH_API __declspec(dllexport)
#else
#define DYNAMIC_DISPATCH_API __declspec(dllimport)
#endif
#endif
#endif

#ifdef __GNUC__
#ifdef DYNAMIC_DISPATCH_BUILD_SHARED
#ifdef DYNAMIC_DISPATCH_EXPORT
#define DYNAMIC_DISPATCH_API __attribute__((visibility("default")))
#else
#define DYNAMIC_DISPATCH_API
#endif
#endif
#endif

#ifndef DYNAMIC_DISPATCH_API
#define DYNAMIC_DISPATCH_API
#endif

#include <memory>
#include <mutex>
#include <unordered_map>

// XRT headers
#ifdef XRT_RUNLIST_EN
#include "experimental/xrt_kernel.h"
#endif
#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"

#include "utils/instruction_registry.hpp"
#include <utils/logging.hpp>
#include <utils/tfuncs.hpp>

namespace {
constexpr unsigned int DEVICE_INDEX = 0;
constexpr auto NPU_KERNEL_NAME = "DPU";

std::string get_first_kernel_name(const xrt::xclbin &xclbin_) {
  std::string kernel_name = NPU_KERNEL_NAME;
  for (const auto &kernel : xclbin_.get_kernels()) {
    const auto candidate_kernel_name = kernel.get_name();
    if ((candidate_kernel_name.rfind("XDP_KERNEL", 0) != 0) &&
        (candidate_kernel_name.rfind("vadd", 0) != 0)) {
      kernel_name = candidate_kernel_name;
      break;
    }
  }
  return kernel_name;
}

std::vector<std::string> get_all_kernel_names(const xrt::xclbin &xclbin_) {
  std::vector<std::string> kernel_names;
  for (const auto &kernel : xclbin_.get_kernels()) {
    const auto candidate_kernel_name = kernel.get_name();
    if ((candidate_kernel_name.rfind("XDP_KERNEL", 0) != 0) &&
        (candidate_kernel_name.rfind("vadd", 0) != 0)) {
      kernel_names.push_back(candidate_kernel_name);
    }
  }
  return kernel_names;
}

bool create_xrt_context() {
  RYZENAI_LOG_TRACE("Creating new context);");
  return true;
}

} // namespace

namespace ryzenai {
namespace dynamic_dispatch {

using context_id_t = std::uint32_t;
using xrt_key_id_t = std::string;
constexpr std::uint32_t MAX_NUM_XRT_CONTEXTS = 15;
class xrt_context {
private:
  static DYNAMIC_DISPATCH_API
      std::unordered_map<xrt_key_id_t, std::shared_ptr<xrt_context>>
          ctx_map_;
  static DYNAMIC_DISPATCH_API std::mutex xrt_ctx_mutex_;
  bool init_;
  xrt::device device_;
  xrt::xclbin xclbin_;
  xrt::uuid uuid_;
  xrt::hw_context context_;
  std::unordered_map<std::string, xrt::kernel> kernels_;
  xrt::kernel kernel_;
  std::map<std::string, std::uint32_t> qos_;
  instruction_registry instr_reg_;

public:
  xrt_context() = default;

  xrt_context(const std::vector<char> *xclbin,
              const std::map<std::string, std::uint32_t> &qos)
      : init_(create_xrt_context()), device_(DEVICE_INDEX), xclbin_(*xclbin),
        uuid_(device_.register_xclbin(xclbin_)),
        context_(device_, xclbin_.get_uuid(), qos),
        kernel_(context_, get_first_kernel_name(xclbin_)),
        instr_reg_(context_, kernel_) {

    std::vector<std::string> kernel_names = get_all_kernel_names(xclbin_);
    for (const auto &kernel_name : kernel_names) {
      kernels_[kernel_name] = xrt::kernel(context_, kernel_name);
    }

    RYZENAI_LOG_TRACE("Created new context");
  }
  static void destroy_ctx_map() { ctx_map_.clear(); }

  static std::shared_ptr<xrt_context>
  get_instance(const std::string &xclbin_fname, context_id_t context_id = 0,
               const std::map<std::string, std::uint32_t> &qos = {},
               const std::vector<char> *xclbin_content = nullptr) {
    RYZENAI_LOG_TRACE("Getting context with xclbin: " + xclbin_fname +
                      ", context_id = " + std::to_string(context_id));
    auto xrt_key =
        xclbin_fname + std::string("_context_id_") + std::to_string(context_id);

    auto clean_stale_contexts = [&]() {
      bool removed_contexts = false;

      std::vector<std::string> stale_xclbins;

      for (auto it = ctx_map_.begin(); it != ctx_map_.end(); ++it) {
        // check if only copy is the one in cache
        if (it->second.use_count() == 1) {
          stale_xclbins.push_back(it->first);
        }
      }

      if (stale_xclbins.size() != 0) {
        for (auto &stale_xclbin : stale_xclbins) {
          ctx_map_.erase(stale_xclbin);
        }

        removed_contexts = true;
      } else {
        RYZENAI_LOG_TRACE(
            "[Warning] Could not find xrt context to remove from cache");
        // TODO: should we throw here ?? Not sure what xrt behaviour will be
      }

      return removed_contexts;
    };
    std::lock_guard<std::mutex> guard(xrt_ctx_mutex_);
    if (ctx_map_.find(xrt_key) != ctx_map_.end()) {
      RYZENAI_LOG_TRACE("Context found in map");
      return ctx_map_[xrt_key];
    } else {

      if (ctx_map_.size() == MAX_NUM_XRT_CONTEXTS) {
        RYZENAI_LOG_TRACE("[Warning] Maximum number of xrt contexts hit");
        (void)clean_stale_contexts();
      }
      RYZENAI_LOG_TRACE("Context not found in map, creating new one");
      RYZENAI_LOG_TRACE("Current num contexts " +
                        std::to_string(ctx_map_.size()));

      bool retry = false;
      std::uint32_t num_retries = 0;
      constexpr std::uint32_t MAX_NUM_RETRIES = 1;
      do {
        try {
          std::vector<char> buffer;
          if (!xclbin_content) {
            buffer = OpsFusion::read_bin_file<char>(xclbin_fname);
            xclbin_content = &buffer;
          }
          ctx_map_[xrt_key] =
              std::make_shared<xrt_context>(xclbin_content, qos);
          retry = false;
        } catch (...) {
          RYZENAI_LOG_TRACE(
              "[Warning] Retrying xrt context creation and cleanup");
          retry = clean_stale_contexts();
          retry = retry && (num_retries < MAX_NUM_RETRIES);
          num_retries++;
        }
      } while (retry);

      return ctx_map_.at(xrt_key);
    }
  }

  xrt::kernel &get_kernel(const std::string &kernel_name) {
    return kernels_.at(kernel_name);
  }

  xrt::device &get_device() { return device_; }
  xrt::hw_context &get_context() { return context_; }
  xrt::kernel &get_kernel() { return kernel_; }
  xrt::xclbin &get_xclbin() { return xclbin_; }
  instruction_registry &get_registry() { return instr_reg_; }

  void update_qos(const std::map<std::string, std::uint32_t> &qos) {
    // NOTE: changing priority is currently not allowed
    //       if no priority is specified, it will default as if user is
    //       setting to normal priority
    //       In case context was created with different priority, this will
    //       throw
    qos_ = qos;
    context_.update_qos(qos_);
  }
};

} // namespace dynamic_dispatch

} // namespace ryzenai

#endif