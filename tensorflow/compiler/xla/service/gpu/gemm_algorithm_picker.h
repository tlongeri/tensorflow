/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#ifndef TENSORFLOW_COMPILER_XLA_SERVICE_GPU_GEMM_ALGORITHM_PICKER_H_
#define TENSORFLOW_COMPILER_XLA_SERVICE_GPU_GEMM_ALGORITHM_PICKER_H_

#include <optional>
#include <string>
#include <variant>

#include "absl/strings/string_view.h"
#include "tensorflow/compiler/xla/autotune_results.pb.h"
#include "tensorflow/compiler/xla/hlo/ir/hlo_instructions.h"
#include "tensorflow/compiler/xla/hlo/ir/hlo_module.h"
#include "tensorflow/compiler/xla/service/gpu/gpu_conv_runner.h"
#include "tensorflow/compiler/xla/service/hlo_pass_interface.h"
#include "tensorflow/compiler/xla/stream_executor/device_description.h"
#include "tensorflow/compiler/xla/stream_executor/device_memory_allocator.h"
#include "tensorflow/compiler/xla/stream_executor/stream_executor.h"
#include "tensorflow/tsl/protobuf/autotuning.pb.h"

namespace xla {
namespace gpu {

class GemmAlgorithmPicker : public HloModulePass {
 public:
  static void ClearAutotuneResults();
  static Status WriteAutotuneResults(AutotuneResults* results);
  static Status LoadAutotuneResults(const AutotuneResults& results);

  struct DeviceConfig {
    se::StreamExecutor* stream_exec;
    se::DeviceMemoryAllocator* allocator;
  };

  struct DevicelessConfig {
    std::string device_description_str;
    se::CudaComputeCapability cuda_compute_capability{0, 0};
  };

  explicit GemmAlgorithmPicker(DeviceConfig config) : config_(config) {}

  explicit GemmAlgorithmPicker(DevicelessConfig config) : config_(config) {}

  absl::string_view name() const override { return "gemm-algorithm-picker"; }

  using HloPassInterface::Run;
  StatusOr<bool> Run(
      HloModule* module,
      const absl::flat_hash_set<absl::string_view>& execution_threads) override;

 private:
  std::variant<DeviceConfig, DevicelessConfig> config_;
};

}  // namespace gpu
}  // namespace xla

#endif  // TENSORFLOW_COMPILER_XLA_SERVICE_GPU_GEMM_ALGORITHM_PICKER_H_
