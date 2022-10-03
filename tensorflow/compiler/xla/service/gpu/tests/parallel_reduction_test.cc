/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

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

#include "tensorflow/compiler/xla/service/gpu/tests/gpu_codegen_test.h"
#include "tensorflow/compiler/xla/service/hlo_parser.h"
#include "tensorflow/compiler/xla/tests/filecheck.h"

namespace xla {
namespace gpu {

namespace {

class ParallelReductionTest : public GpuCodegenTest {
  DebugOptions GetDebugOptionsForTest() override {
    DebugOptions debug_options = GpuCodegenTest::GetDebugOptionsForTest();
    // The test contains a MOF fusion and the XLA optimizer passes
    // don't like this.
    debug_options.set_xla_disable_all_hlo_passes(true);
    return debug_options;
  }
};

TEST_F(ParallelReductionTest, TwoParallelReductions) {
  const char* hlo_text = R"(
HloModule TwoParallelReductions

%add_f32 {
  %x = f32[] parameter(0)
  %y = f32[] parameter(1)
  ROOT %add = f32[] add(%x, %y)
}

%fused_computation {
  %param0 = f32[1024] parameter(0)
  %param1 = f32[1024] parameter(1)
  %constant0 = f32[] constant(0)
  %reduce1 = f32[] reduce(%param0, %constant0), dimensions={0}, to_apply=%add_f32
  %reduce2 = f32[] reduce(%param1, %constant0), dimensions={0}, to_apply=%add_f32
  ROOT %tuple = (f32[], f32[]) tuple(%reduce1, %reduce2)
}

ENTRY %cluster {
  %param0 = f32[1024] parameter(0)
  %param1 = f32[1024] parameter(1)
  ROOT %fusion = (f32[], f32[])
      fusion(%param0, %param1), kind=kInput, calls=%fused_computation
}
)";

  TF_ASSERT_OK_AND_ASSIGN(std::unique_ptr<VerifiedHloModule> hlo_module,
                          ParseAndReturnVerifiedModule(hlo_text));
  CompileAndVerifyIr(std::move(hlo_module),
                     R"(
CHECK: reduce-group-0
CHECK: reduce-group-1
CHECK-NOT: reduce-group-2
)",
                     /*match_optimized_ir=*/false);
  EXPECT_TRUE(RunAndCompare(hlo_text, ErrorSpec{1e-5, 1e-5}));
}

TEST_F(ParallelReductionTest, TwoParallelReductionsWithBroadcastOutput) {
  const char* hlo_text = R"(
HloModule TwoParallelReductions

%add_f32 {
  %x = f32[] parameter(0)
  %y = f32[] parameter(1)
  ROOT %add = f32[] add(%x, %y)
}

%fused_computation {
  %param0 = f32[] parameter(0)
  %param1 = f32[] parameter(1)
  %param2 = f32[] parameter(2)
  %bcast0 = f32[1024] broadcast(f32[] %param0)
  %reduce1 = f32[] reduce(%bcast0, %param1), dimensions={0}, to_apply=%add_f32
  %reduce2 = f32[] reduce(%bcast0, %param2), dimensions={0}, to_apply=%add_f32
  ROOT %tuple = (f32[], f32[], f32[1024]) tuple(%reduce1, %reduce2, %bcast0)
}

ENTRY %cluster {
  %param0 = f32[] parameter(0)
  %param1 = f32[] parameter(1)
  %param2 = f32[] parameter(2)
  ROOT %fusion = (f32[], f32[], f32[1024])
      fusion(%param0, %param1, %param2), kind=kInput, calls=%fused_computation
}
)";

  TF_ASSERT_OK_AND_ASSIGN(std::unique_ptr<VerifiedHloModule> hlo_module,
                          ParseAndReturnVerifiedModule(hlo_text));
  CompileAndVerifyIr(std::move(hlo_module),
                     R"(
CHECK: reduce-group-0
CHECK: reduce-group-1
CHECK-NOT: reduce-group-2
)",
                     /*match_optimized_ir=*/false);
  EXPECT_TRUE(RunAndCompare(hlo_text, ErrorSpec{1e-5, 1e-5}));
}

TEST_F(ParallelReductionTest, ManyParallelReductions) {
  std::unique_ptr<VerifiedHloModule> module = CreateNewVerifiedModule();
  // Simply use a number not too large to avoid long compilation time
  // and not too small for meaningful test.
  const size_t num_reduces = 32;

  HloComputation* reduce_computation;
  {
    auto embedded_builder = HloComputation::Builder("add");
    HloInstruction* lhs =
        embedded_builder.AddInstruction(HloInstruction::CreateParameter(
            0, ShapeUtil::MakeShape(F32, {}), "lhs"));
    HloInstruction* rhs =
        embedded_builder.AddInstruction(HloInstruction::CreateParameter(
            1, ShapeUtil::MakeShape(F32, {}), "rhs"));
    embedded_builder.AddInstruction(
        HloInstruction::CreateBinary(lhs->shape(), HloOpcode::kAdd, lhs, rhs));
    reduce_computation =
        module->AddEmbeddedComputation(embedded_builder.Build());
  }

  Shape input_shape = ShapeUtil::MakeShape(F32, {1024});
  Shape output_shape = ShapeUtil::MakeShape(F32, {});
  HloComputation* fusion_computation;
  {
    auto fusion_builder = HloComputation::Builder("fusion_computation");
    std::vector<HloInstruction*> outputs;
    HloInstruction* constant = fusion_builder.AddInstruction(
        HloInstruction::CreateConstant(LiteralUtil::CreateR0<float>(0)));
    for (size_t i = 0; i < num_reduces; ++i) {
      HloInstruction* param = fusion_builder.AddInstruction(
          HloInstruction::CreateParameter(i, input_shape, "param"));
      HloInstruction* output =
          fusion_builder.AddInstruction(HloInstruction::CreateReduce(
              output_shape, param, constant, {0}, reduce_computation));
      outputs.push_back(output);
    }
    fusion_builder.AddInstruction(HloInstruction::CreateTuple(outputs));
    fusion_computation = module->AddEmbeddedComputation(fusion_builder.Build());
  }

  HloComputation::Builder b(TestName());
  std::vector<HloInstruction*> entry_params;
  std::vector<Shape> output_shapes;
  entry_params.reserve(num_reduces);
  output_shapes.reserve(num_reduces);
  for (size_t i = 0; i < num_reduces; ++i) {
    HloInstruction* param = b.AddInstruction(
        HloInstruction::CreateParameter(i, input_shape, "param"));
    entry_params.push_back(param);
    output_shapes.push_back(output_shape);
  }
  b.AddInstruction(HloInstruction::CreateFusion(
      ShapeUtil::MakeTupleShape(output_shapes),
      HloInstruction::FusionKind::kInput, entry_params, fusion_computation));
  module->AddEntryComputation(b.Build());

  EXPECT_TRUE(RunAndCompare(std::move(module), ErrorSpec{1e-5, 1e-5}));
}

TEST_F(ParallelReductionTest, CouldBeThreeReductionGroups) {
  const char* hlo_text = R"(
HloModule ThreeReductionGroups

%add_f32 {
  %x = f32[] parameter(0)
  %y = f32[] parameter(1)
  ROOT %add = f32[] add(%x, %y)
}

%fused_computation {
  %param0 = f32[1024,128] parameter(0)
  %param1 = f32[1024,128] parameter(1)
  %param2 = f32[1024,128] parameter(2)
  %constant0 = f32[] constant(0)
  // %mul0, %reduce0, and %reduce1 should go into a group.
  %broadcast0 = f32[1024,128] broadcast(%constant0), dimensions={}
  %mul0 = f32[1024,128] multiply(param0, broadcast0)
  %reduce0 = f32[128] reduce(%mul0, %constant0), dimensions={0}, to_apply=%add_f32
  %reduce1 = f32[128] reduce(%param0, %constant0), dimensions={0}, to_apply=%add_f32
  // %reduce2 and %reduce3 should go into another group.
  %reduce2 = f32[128] reduce(%param1, %constant0), dimensions={0}, to_apply=%add_f32
  %reduce3 = f32[128] reduce(%param1, %constant0), dimensions={0}, to_apply=%add_f32
  // %reduce4 and %mul2 should go into the other group, although broadcast0 is
  // reused.
  %mul1 = f32[1024,128] multiply(param2, broadcast0)
  %reduce4 = f32[128] reduce(%mul1, %constant0), dimensions={0}, to_apply=%add_f32
  %mul2 = f32[1024,128] multiply(param2, param2)
  ROOT %tuple =
      (f32[1024, 128], f32[128], f32[128], f32[128], f32[128], f32[128], f32[1024, 128])
      tuple(%mul2, %reduce0, %reduce4, %reduce3, %reduce2, %reduce1, %mul0)
}

ENTRY %cluster {
  %param0 = f32[1024,128] parameter(0)
  %param1 = f32[1024,128] parameter(1)
  %param2 = f32[1024,128] parameter(2)
  ROOT %fusion =
      (f32[1024, 128], f32[128], f32[128], f32[128], f32[128], f32[128], f32[1024, 128])
      fusion(%param0, %param1, %param2), kind=kInput, calls=%fused_computation
}
)";

  // Because of b/249976438 mul0 and mul2 will make first and last groups merge.

  TF_ASSERT_OK_AND_ASSIGN(std::unique_ptr<VerifiedHloModule> hlo_module,
                          ParseAndReturnVerifiedModule(hlo_text));
  CompileAndVerifyIr(std::move(hlo_module),
                     R"(
CHECK: reduce-group-0
CHECK: reduce-group-1
CHECK-NOT: reduce-group-2
)",
                     /*match_optimized_ir=*/false);
  EXPECT_TRUE(RunAndCompare(hlo_text, ErrorSpec{1e-5, 1e-5}));
}

class ParallelReductionTestBase : public HloTestBase {};

TEST_F(ParallelReductionTestBase, ParallelReductionsWithAliasing) {
  const char* hlo_text = R"(
HloModule m, input_output_alias={{1}: (2, {}, must-alias)}

a {
  x = f32[] parameter(0)
  y = f32[] parameter(1)
  ROOT r = f32[] add(x, y)
}

f {
  p0 = f32[128] parameter(0)
  p1 = f32[128] parameter(1)
  p2 = f32[128] parameter(2)
  p3 = f32[128] parameter(3)
  c0 = f32[] constant(0)
  m0 = f32[128]{0} multiply(p0, p0)
  reduce0 = f32[] reduce(m0, c0), dimensions={0}, to_apply=a
  add0 = f32[128] add(p0, p1)
  m1 = f32[128]{0} multiply(p2, p2)
  reduce1 = f32[] reduce(m1, c0), dimensions={0}, to_apply=a
  add1 = f32[128] add(p2, p3)
  ROOT r = (f32[], f32[128], f32[], f32[128])
    tuple(reduce0, add0, reduce1, add1)
}

ENTRY e {
  p0 = f32[128] parameter(0)
  p1 = f32[128] parameter(1)
  p2 = f32[128] parameter(2)
  p3 = f32[128] parameter(3)
  ROOT r = (f32[], f32[128], f32[], f32[128]) fusion(p0, p1, p2, p3), kind=kInput, calls=f
}
)";

  TF_ASSERT_OK_AND_ASSIGN(std::unique_ptr<VerifiedHloModule> hlo_module,
                          ParseAndReturnVerifiedModule(hlo_text));
  EXPECT_TRUE(
      hlo_module->input_output_alias_config().ParameterMustAlias(2, {}));
  EXPECT_TRUE(RunAndCompareNoHloPasses(hlo_text, ErrorSpec{1e-5, 1e-5}));
}
}  // namespace
}  // namespace gpu
}  // namespace xla
