/* Copyright 2022 The TensorFlow Authors. All Rights Reserved.

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

#include "tensorflow/compiler/xla/service/gpu/gemm_algorithm_picker.h"

#include <string>

#include "tensorflow/compiler/xla/service/gpu/gemm_rewriter.h"
#include "tensorflow/compiler/xla/service/pattern_matcher.h"
#include "tensorflow/compiler/xla/service/pattern_matcher_gmock.h"
#include "tensorflow/compiler/xla/stream_executor/device_description.h"
#include "tensorflow/compiler/xla/tests/hlo_test_base.h"
#include "tensorflow/tsl/lib/core/status_test_util.h"
#include "tensorflow/tsl/platform/statusor.h"
#include "tensorflow/tsl/platform/test.h"
#include "tensorflow/tsl/protobuf/dnn.pb.h"

namespace xla::gpu {
namespace {

namespace m = ::xla::match;

class GemmAlgorithmPickerTest : public HloTestBase {
 public:
  GemmAlgorithmPickerTest() { GemmAlgorithmPicker::ClearAutotuneResults(); }
};

TEST_F(GemmAlgorithmPickerTest, SetAlgorithm) {
  constexpr absl::string_view kHlo = R"(
HloModule module

ENTRY main {
  %arg0 = f32[100,100]{1,0} parameter(0)
  %arg1 = f32[100,100]{1,0} parameter(1)
  ROOT %dot = f32[100,100]{1,0} dot(arg0, arg1), lhs_contracting_dims={1}, rhs_contracting_dims={0}
})";
  TF_ASSERT_OK_AND_ASSIGN(auto m, ParseAndReturnVerifiedModule(kHlo));

  se::Platform* platform = PlatformUtil::GetDefaultPlatform().value();
  TF_ASSERT_OK_AND_ASSIGN(std::vector<se::StreamExecutor*> executors,
                          PlatformUtil::GetStreamExecutors(platform));
  ASSERT_GT(executors.size(), 0);
  se::StreamExecutor* stream_exec = executors[0];
  GemmAlgorithmPicker::DeviceConfig device_config{stream_exec, nullptr};

  bool changed = false;
  TF_ASSERT_OK_AND_ASSIGN(
      changed, RunHloPass(GemmRewriter(stream_exec->GetDeviceDescription()
                                           .cuda_compute_capability()),
                          m.get()));
  changed = false;
  TF_ASSERT_OK_AND_ASSIGN(
      changed, RunHloPass(GemmAlgorithmPicker(device_config), m.get()));
  ASSERT_TRUE(changed);

  AutotuneResults results;
  TF_ASSERT_OK(GemmAlgorithmPicker::WriteAutotuneResults(&results));
  ASSERT_EQ(results.dots_size(), 1);
  auto& result = *results.mutable_dots(0)->mutable_result();
  int64_t old_algo_id = result.algorithm().algo_id();
  int64_t new_algo_id = old_algo_id + 1;
  result.mutable_gemm()->set_algorithm(new_algo_id);

  GemmAlgorithmPicker::ClearAutotuneResults();
  TF_ASSERT_OK(GemmAlgorithmPicker::LoadAutotuneResults(results));

  // Now send the same module through GemmAlgorithmPicker again.  The dot should
  // have the new algorithm.
  TF_ASSERT_OK_AND_ASSIGN(m, ParseAndReturnVerifiedModule(kHlo));
  changed = false;
  TF_ASSERT_OK_AND_ASSIGN(
      changed, RunHloPass(GemmRewriter(stream_exec->GetDeviceDescription()
                                           .cuda_compute_capability()),
                          m.get()));
  changed = false;
  TF_ASSERT_OK_AND_ASSIGN(
      changed, RunHloPass(GemmAlgorithmPicker(device_config), m.get()));
  ASSERT_TRUE(changed);

  SCOPED_TRACE(m->ToString());
  HloInstruction* dot;
  ASSERT_THAT(m->entry_computation()->root_instruction(),
              GmockMatch(m::CustomCall(&dot)));

  TF_ASSERT_OK_AND_ASSIGN(GemmBackendConfig config,
                          dot->backend_config<GemmBackendConfig>());
  EXPECT_EQ(config.selected_algorithm(), new_algo_id);
}

TEST_F(GemmAlgorithmPickerTest, GetAlgorithmWithoutDevice) {
  constexpr absl::string_view kHlo = R"(
HloModule module

ENTRY main {
  %arg0 = f32[100,100]{1,0} parameter(0)
  %arg1 = f32[100,100]{1,0} parameter(1)
  ROOT %dot = f32[100,100]{1,0} dot(arg0, arg1), lhs_contracting_dims={1}, rhs_contracting_dims={0}
})";
  TF_ASSERT_OK_AND_ASSIGN(auto m, ParseAndReturnVerifiedModule(kHlo));

  se::Platform* platform = PlatformUtil::GetDefaultPlatform().value();
  TF_ASSERT_OK_AND_ASSIGN(std::vector<se::StreamExecutor*> executors,
                          PlatformUtil::GetStreamExecutors(platform));
  ASSERT_GT(executors.size(), 0);
  se::StreamExecutor* stream_exec = executors[0];

  bool changed = false;
  TF_ASSERT_OK_AND_ASSIGN(
      changed, RunHloPass(GemmRewriter(stream_exec->GetDeviceDescription()
                                           .cuda_compute_capability()),
                          m.get()));
  changed = false;
  GemmAlgorithmPicker::DeviceConfig device_config{stream_exec, nullptr};
  TF_ASSERT_OK_AND_ASSIGN(
      changed, RunHloPass(GemmAlgorithmPicker(device_config), m.get()));
  ASSERT_TRUE(changed);

  AutotuneResults results;
  TF_ASSERT_OK(GemmAlgorithmPicker::WriteAutotuneResults(&results));
  ASSERT_EQ(results.dots_size(), 1);
  auto& result = *results.mutable_dots(0)->mutable_result();
  int64_t old_algo_id = result.algorithm().algo_id();
  int64_t new_algo_id = old_algo_id + 1;
  result.mutable_gemm()->set_algorithm(new_algo_id);

  GemmAlgorithmPicker::ClearAutotuneResults();
  TF_ASSERT_OK(GemmAlgorithmPicker::LoadAutotuneResults(results));

  // Now send the same module through GemmAlgorithmPicker again.  The dot should
  // have the new algorithm.
  TF_ASSERT_OK_AND_ASSIGN(m, ParseAndReturnVerifiedModule(kHlo));
  changed = false;
  TF_ASSERT_OK_AND_ASSIGN(
      changed, RunHloPass(GemmRewriter(stream_exec->GetDeviceDescription()
                                           .cuda_compute_capability()),
                          m.get()));
  changed = false;
  absl::string_view device_description_str =
      stream_exec->device_description_str();
  se::CudaComputeCapability cuda_compute_capability =
      stream_exec->GetDeviceDescription().cuda_compute_capability();
  GemmAlgorithmPicker::DevicelessConfig deviceless_config{
      std::string(device_description_str), cuda_compute_capability};
  TF_ASSERT_OK_AND_ASSIGN(
      changed, RunHloPass(GemmAlgorithmPicker(deviceless_config), m.get()))
  ASSERT_TRUE(changed);

  SCOPED_TRACE(m->ToString());
  HloInstruction* dot;
  ASSERT_THAT(m->entry_computation()->root_instruction(),
              GmockMatch(m::CustomCall(&dot)));

  TF_ASSERT_OK_AND_ASSIGN(GemmBackendConfig config,
                          dot->backend_config<GemmBackendConfig>());
  EXPECT_EQ(config.selected_algorithm(), new_algo_id);
}

}  // namespace
}  // namespace xla::gpu
