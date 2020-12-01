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

// This transformation pass applies quantization propagation on TFLite dialect.
#include <cmath>
#include <iterator>
#include <string>

#include "absl/memory/memory.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "mlir/Dialect/Quant/FakeQuantSupport.h"  // from @llvm-project
#include "mlir/Dialect/Quant/QuantOps.h"  // from @llvm-project
#include "mlir/Dialect/Quant/QuantTypes.h"  // from @llvm-project
#include "mlir/IR/Function.h"  // from @llvm-project
#include "mlir/IR/MLIRContext.h"  // from @llvm-project
#include "mlir/IR/Operation.h"  // from @llvm-project
#include "mlir/IR/PatternMatch.h"  // from @llvm-project
#include "mlir/IR/StandardTypes.h"  // from @llvm-project
#include "mlir/IR/Value.h"  // from @llvm-project
#include "mlir/Pass/Pass.h"  // from @llvm-project
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"  // from @llvm-project
#include "tensorflow/compiler/mlir/lite/ir/tfl_ops.h"
#include "tensorflow/compiler/mlir/lite/quantization/lite/tfl_to_std.h"
#include "tensorflow/compiler/mlir/lite/quantization/quantization_config.h"
#include "tensorflow/compiler/mlir/lite/quantization/quantization_traits.h"
#include "tensorflow/compiler/mlir/lite/quantization/quantization_utils.h"
#include "tensorflow/compiler/mlir/lite/transforms/passes.h"
#include "tensorflow/core/framework/types.pb.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/tools/optimize/operator_property.h"

// NOLINTNEXTLINE
static llvm::cl::list<std::string> quantize_allowlist(
    "tfl-test-quantize-allowlist", llvm::cl::value_desc("list"),
    llvm::cl::desc("comma separated list of allowlisted functions to be "
                   "quantized. Only used in tests"),
    llvm::cl::CommaSeparated);

// NOLINTNEXTLINE
static llvm::cl::opt<bool> quantize_signed(
    "tfl-test-quantize-signed", llvm::cl::value_desc("bool"),
    llvm::cl::desc("signed inference type. Only used in tests"),
    llvm::cl::init(false));

// NOLINTNEXTLINE
static llvm::cl::opt<bool> disable_per_channel(
    "tfl-disable-per-channel", llvm::cl::value_desc("bool"),
    llvm::cl::desc("Whether disable per-channel quantized weights."),
    llvm::cl::init(false));

//===----------------------------------------------------------------------===//
// The prepare-quantize Pass.
//
namespace mlir {
namespace TFL {

namespace {

// Applies prepare quantization on the model in TFL dialect. This pass runs
// before the quantization pass and propagate the quantization parameters
// across ops. This step is necessary for post-training quantization and also
// making the quantization rule for some operations in the quantization-aware
// training quantization simpler.
class PrepareQuantizePass
    : public PassWrapper<PrepareQuantizePass, FunctionPass> {
  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<TFL::TensorFlowLiteDialect,
                    ::mlir::quant::QuantizationDialect>();
  }

 public:
  // Constructor used by the PassRegistration and enforce uint8 quantization.
  // This is only used by test.
  explicit PrepareQuantizePass() {
    if (quantize_signed)
      quant_specs_.inference_type = tensorflow::DT_QINT8;
    else
      quant_specs_.inference_type = tensorflow::DT_QUINT8;
  }

  // Constructor used by manually creating the pass.
  explicit PrepareQuantizePass(const QuantizationSpecs& quant_specs)
      : quant_specs_(quant_specs) {}

  void runOnFunction() override;

 private:
  // Set the quantization parameters of the input nodes. These parameters are
  // converted from the user specified input value ranges. The input nodes with
  // non-float tensor types will be skipped because they are not quantizable.
  // Return true if number of input nodes doesn't equal to that of the input
  // ranges.
  bool SetInputNodesQuantizationParams(FuncOp func);

  // The function might contain more stats ops than required, and it will
  // introduce requantize if the calibration stats have conflicts. This method
  // tries to remove all the redundant stats ops.
  bool RemoveRedundantStats(FuncOp func);

  // Verify the quantization specification is expected for quantizing the
  // current function.
  bool IsLegalQuantSpecs(FuncOp func) {
    if (func.getName() == quant_specs_.target_func) {
      return func.getNumArguments() == quant_specs_.input_ranges.size();
    }
    return true;
  }

  // Get the min and max values from the quantization specification for the
  // current function and argument index. Uses default values if the function
  // is specified in the `quantize_allowlist`.
  std::pair<llvm::Optional<double>, llvm::Optional<double>>
  GetMinMaxValuesForArgument(llvm::StringRef func_name, int index) {
    if (func_name == quant_specs_.target_func) {
      return quant_specs_.input_ranges[index];
    } else {
      return {0.0, 255.0};
    }
  }

  // Apply some sanity check and report some warnings for those who don't follow
  // the best quantization practice. This also fixes some simple violations.
  void SanityCheckAndAdjustment(FuncOp func);

  // Whether the func contains Quantize ops. This is used to determine whether
  // to use the quantization parameters from the fixed output range property.
  bool ContainsQuantizeOps(FuncOp func);

  QuantizationSpecs quant_specs_;
};

bool PrepareQuantizePass::SetInputNodesQuantizationParams(FuncOp func) {
  StringRef func_name = func.getName();
  auto& target_func = quant_specs_.target_func;

  // Skip this function because it isn't the target function from the spec or
  // in the function while list.
  if (target_func != func_name &&
      !llvm::is_contained(quantize_allowlist, func_name)) {
    return false;
  }

  // If the validation fails, the pass should stop immediately.
  if (!IsLegalQuantSpecs(func)) {
    return true;
  }

  OpBuilder builder(func);
  bool is_signed = quant_specs_.IsSignedInferenceType();
  IntegerAttr num_bits =
      builder.getI32IntegerAttr(quant_specs_.GetQuantizationTypeWidth());
  BoolAttr narrow_range = builder.getBoolAttr(false);

  auto add_quantize_op = [&](Location loc, Type input_type, Block* block,
                             Block::iterator insertion_point, Value arg,
                             int i) {
    if (auto shaped = input_type.dyn_cast<ShapedType>()) {
      if (shaped.getElementType().isa<FloatType>()) {
        // If there are existing quantize ops, they are from training and we
        // should respect them.
        if (arg.hasOneUse() &&
            llvm::isa<quant::QuantizeCastOp>(*arg.user_begin())) {
          return;
        }

        auto min_max = GetMinMaxValuesForArgument(func_name, i);
        // The input min/max or mean/std are not specified, then skip.
        if (!min_max.first.hasValue() || !min_max.second.hasValue()) return;

        TypeAttr params = quant::GetQuantizedTypeAttr(
            builder, input_type,
            builder.getF64FloatAttr(min_max.first.getValue()),
            builder.getF64FloatAttr(min_max.second.getValue()),
            /*quant_dim=*/-1, num_bits, narrow_range, is_signed);
        builder.setInsertionPoint(block, insertion_point);
        auto q_op =
            builder.create<quant::QuantizeCastOp>(loc, params.getValue(), arg);
        auto dq_op = builder.create<quant::DequantizeCastOp>(loc, input_type,
                                                             q_op.getResult());
        arg.replaceAllUsesWith(dq_op.getResult());
        q_op.setOperand(arg);
      }
    }
  };

  for (int i = 0, e = func.getNumArguments(); i != e; ++i) {
    BlockArgument arg = func.getArgument(i);
    auto* arg_block = arg.getOwner();
    add_quantize_op(arg.getLoc(), arg.getType(), arg_block,
                    std::next(arg_block->begin(), i), arg, i);
  }

  return false;
}

#include "tensorflow/compiler/mlir/lite/utils/generated_op_quant_spec_getters.inc"

bool PrepareQuantizePass::RemoveRedundantStats(FuncOp func) {
  return RemoveRedundantStatsOps(func, GetOpQuantSpec);
}

static Value Quantized(Operation* user) {
  if (auto q = llvm::dyn_cast_or_null<quant::QuantizeCastOp>(user)) {
    if (auto dq = llvm::dyn_cast_or_null<quant::DequantizeCastOp>(
            *q.getResult().user_begin())) {
      return dq.getResult();
    }
  }
  return {};
}

void PrepareQuantizePass::SanityCheckAndAdjustment(FuncOp func) {
  // If an op output has two users: one of them is a quantize op and another
  // one is returned directly, we decide to return the quantized result instead,
  // so this op can be quantized. This is only applied on the returned result
  // because the error will not be accumulated.

  func.walk([&](ReturnOp ret) {
    int i = 0;
    for (Value returned : ret.operands()) {
      llvm::SmallVector<Value, 4> quantized;
      for (auto user : returned.getUsers()) {
        if (auto q = Quantized(user)) {
          quantized.push_back(q);
        }
      }
      if (quantized.size() == 1) {
        ret.setOperand(i, quantized.front());
      }
      i++;
    }
  });

  // We prefer to placing quantization emulation ops on the results of the
  // concat ops.
  func.walk([&](ConcatenationOp concat) {
    if (concat.output().hasOneUse() &&
        Quantized(*concat.output().user_begin())) {
      return;
    }
    concat.emitWarning(
        "Missing quantization parameter on the output might introduce "
        "quantization error!");
  });

  // Check for  (Quant (Dequant $in), $qA) "qdq" pairs that couldn't be
  // eliminated at this point.  This only occurs for the pattern
  //      (Quant (Dequant (Quant $in, $qB)), $qA)   $qB != $qA
  // where the  qdq pair denotes a non-trivial requantization of an
  // already quantized value. Since this makes little sense (directly quantizing
  // (Quant $in, $qA) would introduce less quantization noise) the likely cause
  // is an minor error in constructing the original network model that
  // introduced back-to-back Fake Quantization operations. Hence: emit a
  // warning. N.b. at this point we're (teporarility) in the quantization
  // dialect (presumably enable re-use in xla etc) quant::*QuantizeCastOp
  // we're matching here.
  //
  func.walk([&](quant::QuantizeCastOp q_op) {
    // If up with end up with
    auto dq_op = dyn_cast_or_null<quant::DequantizeCastOp>(
        q_op.getOperand().getDefiningOp());
    if (!dq_op) {
      return;
    }
    auto dq_arg = dq_op.getOperand();

    if (!dq_arg.hasOneUse()) {
      // The initial quantization is used someplace else ... so it might be
      // reasonable for it to requantized for another purpose.
      // Ideally would want to still check whether requantization narrows
      // rather than widens the representation.
      return;
    }

    // Invariant:
    // isa<quant::QuantizeCastOp>(dq_arg.getDefiningOp()) -->
    // getdq_arg.getType() != q_op.getResult().getType()
    //
    // as otherwise qdq pair would have been optimized away.
    auto qd_arg_def_q_op =
        dyn_cast_or_null<quant::QuantizeCastOp>(dq_arg.getDefiningOp());
    if (!qd_arg_def_q_op) {
      return;
    }

    qd_arg_def_q_op.emitWarning()
        << " quantizer's output has another quantizer (" << q_op.getLoc()
        << ") as consumer - intentional?";
  });
}

bool PrepareQuantizePass::ContainsQuantizeOps(FuncOp func) {
  for (const auto& op : func.getOps()) {
    if (llvm::isa<quant::DequantizeCastOp>(op)) return true;
  }
  return false;
}

using PrepareQuantStats =
    quant::ConvertStatsToQDQs<quant::QuantizeCastOp, quant::DequantizeCastOp>;

// Calculates the minimum power of two that is not less than the value.
double power_of_two_bound(double value) {
  return std::pow(2, std::ceil(std::log2(value)));
}

// Quantize recurrent input of LSTM with 16 bits.
template <typename SourceOp, typename Q, typename DQ>
struct ConvertLstmStatsToQDQs : public OpRewritePattern<SourceOp> {
 public:
  explicit ConvertLstmStatsToQDQs(MLIRContext* context)
      : OpRewritePattern<SourceOp>(context, /*benefit=*/2) {}
  LogicalResult matchAndRewrite(SourceOp op,
                                PatternRewriter& rewriter) const override {
    tflite::optimize::operator_property::OpVariant lstm_variant;
    if (llvm::isa<TFL::LSTMOp>(op.getOperation())) {
      lstm_variant.op_code = tflite::BuiltinOperator_LSTM;
    } else if (llvm::isa<TFL::UnidirectionalSequenceLSTMOp>(
                   op.getOperation())) {
      lstm_variant.op_code =
          tflite::BuiltinOperator_UNIDIRECTIONAL_SEQUENCE_LSTM;
    } else {
      op.emitError("ConvertLstmStatsToQDQs pass only supports LSTMs.");
      return failure();
    }
    lstm_variant.use_projection =
        !op.projection_weights().getType().template isa<NoneType>();
    lstm_variant.use_peephole =
        !op.cell_to_output_weights().getType().template isa<NoneType>();
    lstm_variant.use_peephole =
        !op.cell_to_output_weights().getType().template isa<NoneType>();
    lstm_variant.use_layer_norm =
        !op.forget_layer_norm_coefficients().getType().template isa<NoneType>();

    auto lstm_property =
        tflite::optimize::operator_property::GetOperatorProperty(lstm_variant);

    // Same with the ordering of //tensorflow/compiler/mlir/lite/ir/tfl_ops.td
    const std::vector<std::string> intermediate_attributes = {
        "input_to_input_intermediate", "input_to_forget_intermediate",
        "input_to_cell_intermediate", "input_to_output_intermediate",
        "effective_hidden_scale_intermediate"};

    for (auto& enumerated_intermediates : lstm_property.intermediates) {
      int index = enumerated_intermediates.first;
      auto& tensor_property = enumerated_intermediates.second;
      // intermediate tensors 0, 1, 2, 3 are only used with layer normalization.
      if (!lstm_variant.use_layer_norm && index != 4) {
        continue;
      }
      // intermediate tensor 4 is only used with projection.
      if (!lstm_variant.use_projection && index == 4) {
        continue;
      }
      TypeAttr attr =
          op.template getAttrOfType<TypeAttr>(intermediate_attributes[index]);

      if (!attr) {
        op.emitError()
            << op.getOperationName()
            << " requires quantization values for intermediate tensor "
            << intermediate_attributes[index];
        return failure();
      }
      auto quantized_type =
          QuantizedType::getQuantizedElementType(attr.getValue());
      if (!quantized_type) {
        op.emitError() << intermediate_attributes[index]
                       << " is not quantized.";
        return failure();
      }
      auto calibrated_type =
          quantized_type.dyn_cast<quant::CalibratedQuantizedType>();
      if (!calibrated_type) {
        int num_storage_bits = quantized_type.getStorageTypeIntegralWidth();
        if (tensor_property.number_of_bits != num_storage_bits) {
          op.emitError() << intermediate_attributes[index]
                         << " is expected to be quantized with "
                         << tensor_property.number_of_bits << " bits, but got "
                         << num_storage_bits << " bits instead.";
          return failure();
        }
        continue;  // skip if it is already quantized.
      }
      quant::UniformQuantizedType qtype;
      if (tensor_property.number_of_bits == 8) {
        qtype = quant::fakeQuantAttrsToType(
            op.getLoc(), tensor_property.number_of_bits,
            calibrated_type.getMin(), calibrated_type.getMax(),
            /*narrowRange=*/false, calibrated_type.getExpressedType(),
            /*isSigned=*/false);
      } else if (tensor_property.number_of_bits == 16) {
        double max = std::max(std::abs(calibrated_type.getMin()),
                              std::abs(calibrated_type.getMax()));
        qtype = quant::fakeQuantAttrsToType(
            op.getLoc(), tensor_property.number_of_bits, -max, max,
            /*narrowRange=*/true, calibrated_type.getExpressedType(),
            /*isSigned=*/true);
      } else {
        op.emitError() << "Unsupported quantization bits: "
                       << tensor_property.number_of_bits;
        return failure();
      }

      op.setAttr(intermediate_attributes[index],
                 TypeAttr::get(qtype.castFromExpressedType(
                     qtype.castToExpressedType(attr.getValue()))));
    }

    quant::StatisticsOp stats_op = llvm::dyn_cast_or_null<quant::StatisticsOp>(
        op.input_cell_state().getDefiningOp());
    // Recurrent input is be used within an LSTM, and thus should have one use.
    if (!stats_op || !stats_op.getResult().hasOneUse()) {
      return failure();
    }
    auto stats = stats_op.layerStats().dyn_cast<DenseFPElementsAttr>();
    if (!stats) {
      return failure();
    }

    double max = std::max(
        std::abs(FloatAttr::getValueAsDouble(stats.getValue<APFloat>({0}))),
        std::abs(FloatAttr::getValueAsDouble(stats.getValue<APFloat>({1}))));
    double bound = power_of_two_bound(max);
    Type expressed = stats_op.getType().cast<ShapedType>().getElementType();
    // Set flags to 1 for signed type.
    quant::QuantizedType quant_type = UniformQuantizedType::getChecked(
        quant::QuantizationFlags::Signed,
        IntegerType::get(16, expressed.getContext()), expressed,
        /*scale=*/bound / 32768.0, /*zeroPoint=*/0, llvm::minIntN(16),
        llvm::maxIntN(16), op.getLoc());

    rewriter.setInsertionPointAfter(stats_op);
    Type result_type = quant_type.castFromExpressedType(stats_op.getType());
    auto q = rewriter.create<Q>(stats_op.getLoc(), result_type, stats_op.arg());
    rewriter.replaceOpWithNewOp<DQ>(stats_op, stats_op.getType(), q);
    return success();
  }
};

using PrepareLstmQuantStats =
    ConvertLstmStatsToQDQs<TFL::UnidirectionalSequenceLSTMOp,
                           quant::QuantizeCastOp, quant::DequantizeCastOp>;

void PrepareQuantizePass::runOnFunction() {
  FuncOp func = getFunction();
  MLIRContext* ctx = func.getContext();
  ConvertTFLQuantOpsToMlirQuantOps(func);

  if (quant_specs_.post_training_quantization) {
    RemoveRedundantStats(func);
  } else {
    // Set the quantization parameters for the quantizable input nodes. If this
    // failed, return the function immediately. This is only required for
    // quantization aware training model conversion.
    if (SetInputNodesQuantizationParams(func)) {
      return;
    }
  }

  // During the legalization, unsigned quantized type is used, so we have to
  // convert all of them to signed.
  OwningRewritePatternList patterns;
  bool is_signed = quant_specs_.IsSignedInferenceType();
  int bit_width = quant_specs_.GetQuantizationTypeWidth();
  // When this is true, the quantizer will try its best to extract the
  // quantization parameters from the op quantization property and constant
  // content. This is also set to true when the `quantize_allowlist` and
  // `quantize_signed` test flags are enabled.
  bool eager_quantize = ContainsQuantizeOps(func) ||
                        (!quantize_allowlist.empty() || quantize_signed);
  // Infer the tensor range for the activation ops and weight constants unless
  // it is disabled explicitly.
  bool infer_tensor_range =
      (quant_specs_.post_training_quantization || eager_quantize) &&
      !quant_specs_.disable_infer_tensor_range;
  if (is_signed) {
    patterns.insert<quant::ConvertUnsignedToSigned<quant::QuantizeCastOp>>(ctx);
    // Convert quant stats to int8 quantization parameters.
    // Currently, only activation stats are imported, so narrow_range = false.
    patterns.insert<PrepareQuantStats>(bit_width, false, true, ctx);
  } else {
    // Convert quant stats to uint8 quantization parameters.
    // Currently, only activation stats are imported, so narrow_range = false.
    patterns.insert<PrepareQuantStats>(bit_width, false, false, ctx);
  }
  patterns.insert<PrepareLstmQuantStats>(ctx);
  applyPatternsAndFoldGreedily(func, std::move(patterns));

  SanityCheckAndAdjustment(func);

  // Finally, the quantization parameters can be propagated to the rest of the
  // values (tensors).
  ApplyQuantizationParamsPropagation(
      func, is_signed, disable_per_channel || quant_specs_.disable_per_channel,
      GetOpQuantSpec, infer_tensor_range);

  ConvertMlirQuantOpsToTFLQuantOps(func);
}

}  // namespace

// Creates an instance of the TensorFlow Lite dialect PrepareQuantize pass.
std::unique_ptr<OperationPass<FuncOp>> CreatePrepareQuantizePass(
    const QuantizationSpecs& quant_specs) {
  return std::make_unique<PrepareQuantizePass>(quant_specs);
}

static PassRegistration<PrepareQuantizePass> pass(
    "tfl-prepare-quantize", "Prepare TFL dialect for quantization");

}  // namespace TFL
}  // namespace mlir
