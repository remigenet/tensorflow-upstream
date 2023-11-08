// RUN: stablehlo-quant-opt %s -split-input-file -stablehlo-lift-quantizable-spots-as-functions | FileCheck %s

// CHECK-LABEL: @conv_fn(
// CHECK-SAME:          %[[ARG_0:.*]]: tensor<1x3x3x4xf32>
func.func @conv_fn(%arg0: tensor<1x3x3x4xf32>) -> tensor<1x3x3x4xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<3x3x4x4xf32>
  %1 = stablehlo.convolution(%arg0, %0) dim_numbers = [b, 0, 1, f]x[0, 1, i, o]->[b, 0, 1, f], window = {pad = [[1, 1], [1, 1]]} {batch_group_count = 1 : i64, feature_group_count = 1 : i64} : (tensor<1x3x3x4xf32>, tensor<3x3x4x4xf32>) -> tensor<1x3x3x4xf32>
  func.return %1: tensor<1x3x3x4xf32>
}
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<1x3x3x4xf32>
// CHECK: }

// CHECK-LABEL: private @composite_conv_fn_1
// CHECK: %[[CONV:.*]] = stablehlo.convolution(%arg0, %arg1)
// CHECK: return %[[CONV]] : tensor<1x3x3x4xf32>
// CHECK: }

// -----

// CHECK-LABEL: @dot_general_fn(
// CHECK-SAME:                 %[[ARG_0:.*]]: tensor<1x1x167xf32>
func.func @dot_general_fn(%arg0: tensor<1x1x167xf32>) -> tensor<1x1x64xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<167x64xf32>
  %1 = stablehlo.dot_general %arg0, %0, contracting_dims = [2] x [0], precision = [DEFAULT, DEFAULT] : (tensor<1x1x167xf32>, tensor<167x64xf32>) -> tensor<1x1x64xf32>
  return %1 : tensor<1x1x64xf32>
}
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<1x1x64xf32>
// CHECK: }

// CHECK-LABEL: private @composite_dot_general_fn_1
// CHECK: %[[DOT_GENERAL:.*]] = stablehlo.dot_general %arg0, %arg1
// CHECK: return %[[DOT_GENERAL:.*]] : tensor<1x1x64xf32>
// CHECK: }

// -----

// CHECK-LABEL: @conv_with_bias_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<1x3x3x4xf32>
func.func @conv_with_bias_fn(%arg0: tensor<1x3x3x4xf32>) -> tensor<1x3x3x4xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<3x3x4x4xf32>
  %1 = stablehlo.constant dense<2.000000e+00> : tensor<1x3x3x4xf32>
  %2 = stablehlo.convolution(%arg0, %0) dim_numbers = [b, 0, 1, f]x[0, 1, i, o]->[b, 0, 1, f], window = {pad = [[1, 1], [1, 1]]} {batch_group_count = 1 : i64, feature_group_count = 1 : i64} : (tensor<1x3x3x4xf32>, tensor<3x3x4x4xf32>) -> tensor<1x3x3x4xf32>
  %3 = stablehlo.add %2, %1 : tensor<1x3x3x4xf32>
  func.return %3: tensor<1x3x3x4xf32>
}
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST_0]], %[[CONST_1]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<1x3x3x4xf32>
// CHECK: }

// CHECK-LABEL: private @composite_conv_with_bias_fn_1
// CHECK: %[[CONV:.*]] = stablehlo.convolution(%arg0, %arg1)
// CHECK: %[[ADD:.*]] = stablehlo.add %[[CONV]], %arg2
// CHECK: return %[[ADD]] : tensor<1x3x3x4xf32>
// CHECK: }

// -----

// CHECK-LABEL: @dot_general_with_bias_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<1x1x167xf32>
func.func @dot_general_with_bias_fn(%arg0: tensor<1x1x167xf32>) -> tensor<1x1x64xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<167x64xf32>
  %1 = stablehlo.constant dense<2.000000e+00> : tensor<1x1x64xf32>
  %2 = stablehlo.dot_general %arg0, %0, contracting_dims = [2] x [0], precision = [DEFAULT, DEFAULT] : (tensor<1x1x167xf32>, tensor<167x64xf32>) -> tensor<1x1x64xf32>
  %3 = stablehlo.add %2, %1 : tensor<1x1x64xf32>
  func.return %3: tensor<1x1x64xf32>
}
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST_0]], %[[CONST_1]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<1x1x64xf32>
// CHECK: }

// CHECK-LABEL: private @composite_dot_general_with_bias_fn_1
// CHECK: %[[DOT_GENERAL:.*]] = stablehlo.dot_general %arg0, %arg1
// CHECK: %[[ADD:.*]] = stablehlo.add %[[DOT_GENERAL]], %arg2
// CHECK: return %[[ADD]] : tensor<1x1x64xf32>
// CHECK: }

// -----

// CHECK-LABEL: @conv_with_bias_dynamic_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<?x28x28x1xf32>
func.func @conv_with_bias_dynamic_fn(%arg0: tensor<?x28x28x1xf32>) -> tensor<?x28x28x16xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<3x3x1x16xf32>
  %1 = stablehlo.constant dense<2.000000e+00> : tensor<16xf32>
  %2 = stablehlo.convolution(%arg0, %0) dim_numbers = [b, 0, 1, f]x[0, 1, i, o]->[b, 0, 1, f], window = {stride = [1, 1], pad = [[1, 1], [1, 1]], rhs_dilate = [1, 1]} {batch_group_count = 1 : i64, feature_group_count = 1 : i64, precision_config = [#stablehlo<precision DEFAULT>, #stablehlo<precision DEFAULT>]} : (tensor<?x28x28x1xf32>, tensor<3x3x1x16xf32>) -> tensor<?x28x28x16xf32>
  %3 = shape.shape_of %2 : tensor<?x28x28x16xf32> -> tensor<4xindex>
  %4 = stablehlo.dynamic_broadcast_in_dim %1, %3, dims = [3] : (tensor<16xf32>, tensor<4xindex>) -> tensor<?x28x28x16xf32>
  %5 = stablehlo.add %2, %4 : tensor<?x28x28x16xf32>
  func.return %5: tensor<?x28x28x16xf32>
}
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST_0]], %[[CONST_1]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<?x28x28x16xf32>
// CHECK: }

// CHECK-LABEL: private @composite_conv_with_bias_dynamic_fn_1
// CHECK: %[[CONV:.*]] = stablehlo.convolution(%arg0, %arg1)
// CHECK: %[[SHAPE_OF:.*]] = shape.shape_of %[[CONV]]
// CHECK: %[[DYNAMIC_BROADCAST_IN_DIM:.*]] = stablehlo.dynamic_broadcast_in_dim %arg2, %[[SHAPE_OF]]
// CHECK: %[[ADD:.*]] = stablehlo.add %[[CONV]], %[[DYNAMIC_BROADCAST_IN_DIM]]
// CHECK: return %[[ADD]] : tensor<?x28x28x16xf32>
// CHECK: }

// -----

// CHECK-LABEL: @dot_general_with_bias_dynamic_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<?x12544xf32>
func.func @dot_general_with_bias_dynamic_fn(%arg0: tensor<?x12544xf32>) -> tensor<?x10xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<12544x10xf32>
  %1 = stablehlo.constant dense<2.000000e+00> : tensor<10xf32>
  %2 = stablehlo.dot_general %arg0, %0, contracting_dims = [1] x [0], precision = [DEFAULT, DEFAULT] : (tensor<?x12544xf32>, tensor<12544x10xf32>) -> tensor<?x10xf32>
  %3 = shape.shape_of %2 : tensor<?x10xf32> -> tensor<2xindex>
  %4 = stablehlo.dynamic_broadcast_in_dim %1, %3, dims = [1] : (tensor<10xf32>, tensor<2xindex>) -> tensor<?x10xf32>
  %5 = stablehlo.add %2, %4 : tensor<?x10xf32>
  func.return %5: tensor<?x10xf32>
}
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST_0]], %[[CONST_1]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<?x10xf32>
// CHECK: }

// CHECK-LABEL: private @composite_dot_general_with_bias_dynamic_fn_1
// CHECK: %[[DOT_GENERAL:.*]] = stablehlo.dot_general %arg0, %arg1
// CHECK: %[[SHAPE_OF_0:.*]] = shape.shape_of %[[DOT_GENERAL]]
// CHECK: %[[DYNAMIC_BROADCAST_IN_DIM_0:.*]] = stablehlo.dynamic_broadcast_in_dim %arg2, %[[SHAPE_OF_0]]
// CHECK: %[[ADD:.*]] = stablehlo.add %[[DOT_GENERAL]], %[[DYNAMIC_BROADCAST_IN_DIM_0]]
// CHECK: return %[[ADD]] : tensor<?x10xf32>
// CHECK: }

// -----

// CHECK-LABEL: @conv_with_relu_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<1x3x3x4xf32>
func.func @conv_with_relu_fn(%arg0: tensor<1x3x3x4xf32>) -> tensor<1x3x3x4xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<3x3x4x4xf32>
  %1 = stablehlo.constant dense<0.000000e+00> : tensor<1x3x3x4xf32>
  %2 = stablehlo.convolution(%arg0, %0) dim_numbers = [b, 0, 1, f]x[0, 1, i, o]->[b, 0, 1, f], window = {pad = [[1, 1], [1, 1]]} {batch_group_count = 1 : i64, feature_group_count = 1 : i64} : (tensor<1x3x3x4xf32>, tensor<3x3x4x4xf32>) -> tensor<1x3x3x4xf32>
  %3 = stablehlo.maximum %2, %1 : tensor<1x3x3x4xf32>
  func.return %3: tensor<1x3x3x4xf32>
}
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<1x3x3x4xf32>
// CHECK: }

// CHECK-LABEL: private @composite_conv_with_relu_fn_1
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<0.000000e+00>
// CHECK: %[[CONV:.*]] = stablehlo.convolution(%arg0, %arg1)
// CHECK: %[[MAX:.*]] = stablehlo.maximum %[[CONV]], %[[CONST]]
// CHECK: return %[[MAX]] : tensor<1x3x3x4xf32>
// CHECK: }

// -----

// CHECK-LABEL: @dot_general_with_relu_fn(
// CHECK-SAME:                 %[[ARG_0:.*]]: tensor<1x1x167xf32>,
func.func @dot_general_with_relu_fn(%arg0: tensor<1x1x167xf32>, %arg1: tensor<167x64xf32>) -> tensor<1x1x64xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<167x64xf32>
  %1 = stablehlo.constant dense<0.000000e+00> : tensor<1x1x64xf32>
  %2 = stablehlo.dot_general %arg0, %0, contracting_dims = [2] x [0], precision = [DEFAULT, DEFAULT] : (tensor<1x1x167xf32>, tensor<167x64xf32>) -> tensor<1x1x64xf32>
  %3 = stablehlo.maximum %2, %1 : tensor<1x1x64xf32>
  return %3 : tensor<1x1x64xf32>
}
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<1x1x64xf32>
// CHECK: }

// CHECK-LABEL: private @composite_dot_general_with_relu_fn_1
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<0.000000e+00>
// CHECK: %[[DOT_GENERAL:.*]] = stablehlo.dot_general %arg0, %arg1
// CHECK: %[[MAX:.*]] = stablehlo.maximum %[[DOT_GENERAL]], %[[CONST]]
// CHECK: return %[[MAX:.*]] : tensor<1x1x64xf32>
// CHECK: }

// -----

// CHECK-LABEL: @conv_with_relu_dynamic_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<?x28x28x1xf32>
func.func @conv_with_relu_dynamic_fn(%arg0: tensor<?x28x28x1xf32>) -> tensor<?x28x28x16xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<3x3x1x16xf32>
  %1 = stablehlo.constant dense<0.000000e+00> : tensor<f32>
  %2 = stablehlo.convolution(%arg0, %0) dim_numbers = [b, 0, 1, f]x[0, 1, i, o]->[b, 0, 1, f], window = {stride = [1, 1], pad = [[1, 1], [1, 1]], rhs_dilate = [1, 1]} {batch_group_count = 1 : i64, feature_group_count = 1 : i64, precision_config = [#stablehlo<precision DEFAULT>, #stablehlo<precision DEFAULT>]} : (tensor<?x28x28x1xf32>, tensor<3x3x1x16xf32>) -> tensor<?x28x28x16xf32>
  %3 = shape.shape_of %2 : tensor<?x28x28x16xf32> -> tensor<4xindex>
  %4 = stablehlo.dynamic_broadcast_in_dim %1, %3, dims = [] : (tensor<f32>, tensor<4xindex>) -> tensor<?x28x28x16xf32>
  %5 = stablehlo.maximum %2, %4 : tensor<?x28x28x16xf32>
  func.return %5: tensor<?x28x28x16xf32>
}
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<?x28x28x16xf32>
// CHECK: }

// CHECK-LABEL: private @composite_conv_with_relu_dynamic_fn_1
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<0.000000e+00>
// CHECK: %[[CONV:.*]] = stablehlo.convolution(%arg0, %arg1)
// CHECK: %[[SHAPE_OF:.*]] = shape.shape_of %[[CONV]]
// CHECK: %[[DYNAMIC_BROADCAST_IN_DIM:.*]] = stablehlo.dynamic_broadcast_in_dim %[[CONST]], %[[SHAPE_OF]]
// CHECK: %[[MAX:.*]] = stablehlo.maximum %[[CONV]], %[[DYNAMIC_BROADCAST_IN_DIM]]
// CHECK: return %[[MAX]] : tensor<?x28x28x16xf32>
// CHECK: }

// -----

// CHECK-LABEL: @dot_general_with_relu_dynamic_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<?x12544xf32>
func.func @dot_general_with_relu_dynamic_fn(%arg0: tensor<?x12544xf32>) -> tensor<?x10xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<12544x10xf32>
  %1 = stablehlo.constant dense<0.000000e+00> : tensor<f32>
  %2 = stablehlo.dot_general %arg0, %0, contracting_dims = [1] x [0], precision = [DEFAULT, DEFAULT] : (tensor<?x12544xf32>, tensor<12544x10xf32>) -> tensor<?x10xf32>
  %3 = shape.shape_of %2 : tensor<?x10xf32> -> tensor<2xindex>
  %4 = stablehlo.dynamic_broadcast_in_dim %1, %3, dims = [] : (tensor<f32>, tensor<2xindex>) -> tensor<?x10xf32>
  %5 = stablehlo.maximum %2, %4 : tensor<?x10xf32>
  func.return %5: tensor<?x10xf32>
}
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<?x10xf32>
// CHECK: }

// CHECK-LABEL: private @composite_dot_general_with_relu_dynamic_fn_1
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<0.000000e+00>
// CHECK: %[[DOT_GENERAL:.*]] = stablehlo.dot_general %arg0, %arg1
// CHECK: %[[SHAPE_OF:.*]] = shape.shape_of %[[DOT_GENERAL]]
// CHECK: %[[DYNAMIC_BROADCAST_IN_DIM:.*]] = stablehlo.dynamic_broadcast_in_dim %[[CONST]], %[[SHAPE_OF]]
// CHECK: %[[MAX:.*]] = stablehlo.maximum %[[DOT_GENERAL]], %[[DYNAMIC_BROADCAST_IN_DIM]]
// CHECK: return %[[MAX]] : tensor<?x10xf32>
// CHECK: }

// -----

// The pattern should not match when the const value for relu is not 0.

// CHECK-LABEL: @conv_with_relu_wrong_const_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<1x3x3x4xf32>
func.func @conv_with_relu_wrong_const_fn(%arg0: tensor<1x3x3x4xf32>, %arg1: tensor<3x3x4x4xf32>) -> tensor<1x3x3x4xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<3x3x4x4xf32>
  %1 = stablehlo.constant dense<2.000000e+00> : tensor<1x3x3x4xf32>
  %2 = stablehlo.convolution(%arg0, %0) dim_numbers = [b, 0, 1, f]x[0, 1, i, o]->[b, 0, 1, f], window = {pad = [[1, 1], [1, 1]]} {batch_group_count = 1 : i64, feature_group_count = 1 : i64} : (tensor<1x3x3x4xf32>, tensor<3x3x4x4xf32>) -> tensor<1x3x3x4xf32>
  %3 = stablehlo.maximum %2, %1 : tensor<1x3x3x4xf32>
  func.return %3: tensor<1x3x3x4xf32>
}
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST_0]])
// CHECK: %[[MAX:.*]] = stablehlo.maximum %[[XLA_CALL_MODULE]], %[[CONST_1]]
// CHECK: return %[[MAX]] : tensor<1x3x3x4xf32>
// CHECK: }

// CHECK-LABEL: private @composite_conv_fn_1
// CHECK-NOT: private @composite_conv_with_relu_fn_1

// -----

// CHECK-LABEL: @conv_with_relu6_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<1x3x3x4xf32>
func.func @conv_with_relu6_fn(%arg0: tensor<1x3x3x4xf32>) -> tensor<1x3x3x4xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<3x3x4x4xf32>
  %1 = stablehlo.constant dense<0.000000e+00> : tensor<1x3x3x4xf32>
  %2 = stablehlo.constant dense<6.000000e+00> : tensor<1x3x3x4xf32>
  %3 = stablehlo.convolution(%arg0, %0) dim_numbers = [b, 0, 1, f]x[0, 1, i, o]->[b, 0, 1, f], window = {pad = [[1, 1], [1, 1]]} {batch_group_count = 1 : i64, feature_group_count = 1 : i64} : (tensor<1x3x3x4xf32>, tensor<3x3x4x4xf32>) -> tensor<1x3x3x4xf32>
  %4 = stablehlo.clamp %1, %3, %2 : tensor<1x3x3x4xf32>
  func.return %4: tensor<1x3x3x4xf32>
}
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<1x3x3x4xf32>
// CHECK: }

// CHECK-LABEL: private @composite_conv_with_relu6_fn_1
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<0.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<6.000000e+00>
// CHECK: %[[CONV:.*]] = stablehlo.convolution(%arg0, %arg1)
// CHECK: %[[CLAMP:.*]] = stablehlo.clamp %[[CONST_0]], %[[CONV]], %[[CONST_1]]
// CHECK: return %[[CLAMP]] : tensor<1x3x3x4xf32>
// CHECK: }

// -----

// CHECK-LABEL: @dot_general_with_relu6_fn(
// CHECK-SAME:                 %[[ARG_0:.*]]: tensor<1x1x167xf32>
func.func @dot_general_with_relu6_fn(%arg0: tensor<1x1x167xf32>) -> tensor<1x1x64xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<167x64xf32>
  %1 = stablehlo.constant dense<0.000000e+00> : tensor<1x1x64xf32>
  %2 = stablehlo.constant dense<6.000000e+00> : tensor<1x1x64xf32>
  %3 = stablehlo.dot_general %arg0, %0, contracting_dims = [2] x [0], precision = [DEFAULT, DEFAULT] : (tensor<1x1x167xf32>, tensor<167x64xf32>) -> tensor<1x1x64xf32>
  %4 = stablehlo.clamp %1, %3, %2 : tensor<1x1x64xf32>
  return %4 : tensor<1x1x64xf32>
}
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<1x1x64xf32>
// CHECK: }

// CHECK-LABEL: private @composite_dot_general_with_relu6_fn_1
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<0.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<6.000000e+00>
// CHECK: %[[DOT_GENERAL:.*]] = stablehlo.dot_general %arg0, %arg1
// CHECK: %[[CLAMP:.*]] = stablehlo.clamp %[[CONST_0]], %[[DOT_GENERAL]], %[[CONST_1]]
// CHECK: return %[[CLAMP]] : tensor<1x1x64xf32>
// CHECK: }

// -----

// CHECK-LABEL: @conv_with_bias_and_relu_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<1x3x3x4xf32>
func.func @conv_with_bias_and_relu_fn(%arg0: tensor<1x3x3x4xf32>) -> tensor<1x3x3x4xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<3x3x4x4xf32>
  %1 = stablehlo.constant dense<2.000000e+00> : tensor<1x3x3x4xf32>
  %2 = stablehlo.constant dense<0.000000e+00> : tensor<1x3x3x4xf32>
  %3 = stablehlo.convolution(%arg0, %0) dim_numbers = [b, 0, 1, f]x[0, 1, i, o]->[b, 0, 1, f], window = {pad = [[1, 1], [1, 1]]} {batch_group_count = 1 : i64, feature_group_count = 1 : i64} : (tensor<1x3x3x4xf32>, tensor<3x3x4x4xf32>) -> tensor<1x3x3x4xf32>
  %4 = stablehlo.add %3, %1 : tensor<1x3x3x4xf32>
  %5 = stablehlo.maximum %4, %2 : tensor<1x3x3x4xf32>
  func.return %5: tensor<1x3x3x4xf32>
}
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST_0]], %[[CONST_1]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<1x3x3x4xf32>
// CHECK: }

// CHECK-LABEL: private @composite_conv_with_bias_and_relu_fn_1
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<0.000000e+00>
// CHECK: %[[CONV:.*]] = stablehlo.convolution(%arg0, %arg1)
// CHECK: %[[ADD:.*]] = stablehlo.add %[[CONV]], %arg2
// CHECK: %[[MAX:.*]] = stablehlo.maximum %[[ADD]], %[[CONST]]
// CHECK: return %[[MAX]] : tensor<1x3x3x4xf32>
// CHECK: }

// -----

// CHECK-LABEL: @dot_general_with_bias_and_relu_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<1x1x167xf32>
func.func @dot_general_with_bias_and_relu_fn(%arg0: tensor<1x1x167xf32>) -> tensor<1x1x64xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<167x64xf32>
  %1 = stablehlo.constant dense<2.000000e+00> : tensor<1x1x64xf32>
  %2 = stablehlo.constant dense<0.000000e+00> : tensor<1x1x64xf32>
  %3 = stablehlo.dot_general %arg0, %0, contracting_dims = [2] x [0], precision = [DEFAULT, DEFAULT] : (tensor<1x1x167xf32>, tensor<167x64xf32>) -> tensor<1x1x64xf32>
  %4 = stablehlo.add %3, %1 : tensor<1x1x64xf32>
  %5 = stablehlo.maximum %4, %2 : tensor<1x1x64xf32>
  func.return %5: tensor<1x1x64xf32>
}
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST_0]], %[[CONST_1]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<1x1x64xf32>
// CHECK: }

// CHECK-LABEL: private @composite_dot_general_with_bias_and_relu_fn_1
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<0.000000e+00>
// CHECK: %[[DOT_GENERAL:.*]] = stablehlo.dot_general %arg0, %arg1
// CHECK: %[[ADD:.*]] = stablehlo.add %[[DOT_GENERAL]], %arg2
// CHECK: %[[MAX:.*]] = stablehlo.maximum %[[ADD]], %[[CONST]]
// CHECK: return %[[MAX]] : tensor<1x1x64xf32>
// CHECK: }

// -----

// CHECK-LABEL: @conv_with_bias_and_relu_dynamic_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<?x28x28x1xf32>
func.func @conv_with_bias_and_relu_dynamic_fn(%arg0: tensor<?x28x28x1xf32>) -> tensor<?x28x28x16xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<3x3x1x16xf32>
  %1 = stablehlo.constant dense<2.000000e+00> : tensor<16xf32>
  %2 = stablehlo.constant dense<0.000000e+00> : tensor<f32>
  %3 = stablehlo.convolution(%arg0, %0) dim_numbers = [b, 0, 1, f]x[0, 1, i, o]->[b, 0, 1, f], window = {stride = [1, 1], pad = [[1, 1], [1, 1]], rhs_dilate = [1, 1]} {batch_group_count = 1 : i64, feature_group_count = 1 : i64, precision_config = [#stablehlo<precision DEFAULT>, #stablehlo<precision DEFAULT>]} : (tensor<?x28x28x1xf32>, tensor<3x3x1x16xf32>) -> tensor<?x28x28x16xf32>
  %4 = shape.shape_of %3 : tensor<?x28x28x16xf32> -> tensor<4xindex>
  %5 = stablehlo.dynamic_broadcast_in_dim %1, %4, dims = [3] : (tensor<16xf32>, tensor<4xindex>) -> tensor<?x28x28x16xf32>
  %6 = stablehlo.add %3, %5 : tensor<?x28x28x16xf32>
  %7 = shape.shape_of %6 : tensor<?x28x28x16xf32> -> tensor<4xindex>
  %8 = stablehlo.dynamic_broadcast_in_dim %2, %7, dims = [] : (tensor<f32>, tensor<4xindex>) -> tensor<?x28x28x16xf32>
  %9 = stablehlo.maximum %6, %8 : tensor<?x28x28x16xf32>
  func.return %9: tensor<?x28x28x16xf32>
}
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST_0]], %[[CONST_1]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<?x28x28x16xf32>
// CHECK: }

// CHECK-LABEL: private @composite_conv_with_bias_and_relu_dynamic_fn_1
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<0.000000e+00>
// CHECK: %[[CONV:.*]] = stablehlo.convolution(%arg0, %arg1)
// CHECK: %[[SHAPE_OF_0:.*]] = shape.shape_of %[[CONV]]
// CHECK: %[[DYNAMIC_BROADCAST_IN_DIM_0:.*]] = stablehlo.dynamic_broadcast_in_dim %arg2, %[[SHAPE_OF_0]]
// CHECK: %[[ADD:.*]] = stablehlo.add %[[CONV]], %[[DYNAMIC_BROADCAST_IN_DIM_0]]
// CHECK: %[[SHAPE_OF_1:.*]] = shape.shape_of %[[ADD]]
// CHECK: %[[DYNAMIC_BROADCAST_IN_DIM_1:.*]] = stablehlo.dynamic_broadcast_in_dim %[[CONST]], %[[SHAPE_OF_1]]
// CHECK: %[[MAX:.*]] = stablehlo.maximum %[[ADD]], %[[DYNAMIC_BROADCAST_IN_DIM_1]]
// CHECK: return %[[MAX]] : tensor<?x28x28x16xf32>
// CHECK: }

// -----

// CHECK-LABEL: @dot_general_with_bias_and_relu_dynamic_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<?x12544xf32>
func.func @dot_general_with_bias_and_relu_dynamic_fn(%arg0: tensor<?x12544xf32>) -> tensor<?x10xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<12544x10xf32>
  %1 = stablehlo.constant dense<2.000000e+00> : tensor<10xf32>
  %2 = stablehlo.constant dense<0.000000e+00> : tensor<f32>
  %3 = stablehlo.dot_general %arg0, %0, contracting_dims = [1] x [0], precision = [DEFAULT, DEFAULT] : (tensor<?x12544xf32>, tensor<12544x10xf32>) -> tensor<?x10xf32>
  %4 = shape.shape_of %3 : tensor<?x10xf32> -> tensor<2xindex>
  %5 = stablehlo.dynamic_broadcast_in_dim %1, %4, dims = [1] : (tensor<10xf32>, tensor<2xindex>) -> tensor<?x10xf32>
  %6 = stablehlo.add %3, %5 : tensor<?x10xf32>
  %7 = shape.shape_of %6 : tensor<?x10xf32> -> tensor<2xindex>
  %8 = stablehlo.dynamic_broadcast_in_dim %2, %7, dims = [] : (tensor<f32>, tensor<2xindex>) -> tensor<?x10xf32>
  %9 = stablehlo.maximum %6, %8 : tensor<?x10xf32>
  func.return %9: tensor<?x10xf32>
}
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST_0]], %[[CONST_1]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<?x10xf32>
// CHECK: }

// CHECK-LABEL: private @composite_dot_general_with_bias_and_relu_dynamic_fn_1
// CHECK: %[[CONST:.*]] = stablehlo.constant dense<0.000000e+00>
// CHECK: %[[DOT_GENERAL:.*]] = stablehlo.dot_general %arg0, %arg1
// CHECK: %[[SHAPE_OF_0:.*]] = shape.shape_of %[[DOT_GENERAL]]
// CHECK: %[[DYNAMIC_BROADCAST_IN_DIM_0:.*]] = stablehlo.dynamic_broadcast_in_dim %arg2, %[[SHAPE_OF_0]]
// CHECK: %[[ADD:.*]] = stablehlo.add %[[DOT_GENERAL]], %[[DYNAMIC_BROADCAST_IN_DIM_0]]
// CHECK: %[[SHAPE_OF_1:.*]] = shape.shape_of %[[ADD]]
// CHECK: %[[DYNAMIC_BROADCAST_IN_DIM_1:.*]] = stablehlo.dynamic_broadcast_in_dim %[[CONST]], %[[SHAPE_OF_1]]
// CHECK: %[[MAX:.*]] = stablehlo.maximum %[[ADD]], %[[DYNAMIC_BROADCAST_IN_DIM_1]]
// CHECK: return %[[MAX]] : tensor<?x10xf32>
// CHECK: }

// -----

// CHECK-LABEL: @conv_with_bias_and_relu6_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<1x3x3x4xf32>
func.func @conv_with_bias_and_relu6_fn(%arg0: tensor<1x3x3x4xf32>) -> tensor<1x3x3x4xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<3x3x4x4xf32>
  %1 = stablehlo.constant dense<2.000000e+00> : tensor<1x3x3x4xf32>
  %2 = stablehlo.constant dense<0.000000e+00> : tensor<1x3x3x4xf32>
  %3 = stablehlo.constant dense<6.000000e+00> : tensor<1x3x3x4xf32>
  %4 = stablehlo.convolution(%arg0, %0) dim_numbers = [b, 0, 1, f]x[0, 1, i, o]->[b, 0, 1, f], window = {pad = [[1, 1], [1, 1]]} {batch_group_count = 1 : i64, feature_group_count = 1 : i64} : (tensor<1x3x3x4xf32>, tensor<3x3x4x4xf32>) -> tensor<1x3x3x4xf32>
  %5 = stablehlo.add %4, %1 : tensor<1x3x3x4xf32>
  %6 = stablehlo.clamp %2, %5, %3 : tensor<1x3x3x4xf32>
  func.return %6: tensor<1x3x3x4xf32>
}
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST_0]], %[[CONST_1]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<1x3x3x4xf32>
// CHECK: }

// CHECK-LABEL: private @composite_conv_with_bias_and_relu6_fn_1
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<0.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<6.000000e+00>
// CHECK: %[[CONV:.*]] = stablehlo.convolution(%arg0, %arg1)
// CHECK: %[[ADD:.*]] = stablehlo.add %[[CONV]], %arg2
// CHECK: %[[CLAMP:.*]] = stablehlo.clamp %[[CONST_0]], %[[ADD]], %[[CONST_1]]
// CHECK: return %[[CLAMP]] : tensor<1x3x3x4xf32>
// CHECK: }

// -----

// CHECK-LABEL: @dot_general_with_bias_and_relu6_fn(
// CHECK-SAME:                    %[[ARG_0:.*]]: tensor<1x1x167xf32>
func.func @dot_general_with_bias_and_relu6_fn(%arg0: tensor<1x1x167xf32>) -> tensor<1x1x64xf32> {
  %0 = stablehlo.constant dense<2.000000e+00> : tensor<167x64xf32>
  %1 = stablehlo.constant dense<2.000000e+00> : tensor<1x1x64xf32>
  %2 = stablehlo.constant dense<0.000000e+00> : tensor<1x1x64xf32>
  %3 = stablehlo.constant dense<6.000000e+00> : tensor<1x1x64xf32>
  %4 = stablehlo.dot_general %arg0, %0, contracting_dims = [2] x [0], precision = [DEFAULT, DEFAULT] : (tensor<1x1x167xf32>, tensor<167x64xf32>) -> tensor<1x1x64xf32>
  %5 = stablehlo.add %4, %1 : tensor<1x1x64xf32>
  %6 = stablehlo.clamp %2, %5, %3 : tensor<1x1x64xf32>
  func.return %6: tensor<1x1x64xf32>
}
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<2.000000e+00>
// CHECK: %[[XLA_CALL_MODULE:.*]] = "tf.XlaCallModule"(%arg0, %[[CONST_0]], %[[CONST_1]])
// CHECK: return %[[XLA_CALL_MODULE:.*]] : tensor<1x1x64xf32>
// CHECK: }

// CHECK-LABEL: private @composite_dot_general_with_bias_and_relu6_fn_1
// CHECK: %[[CONST_0:.*]] = stablehlo.constant dense<0.000000e+00>
// CHECK: %[[CONST_1:.*]] = stablehlo.constant dense<6.000000e+00>
// CHECK: %[[DOT_GENERAL:.*]] = stablehlo.dot_general %arg0, %arg1
// CHECK: %[[ADD:.*]] = stablehlo.add %[[DOT_GENERAL]], %arg2
// CHECK: %[[CLAMP:.*]] = stablehlo.clamp %[[CONST_0]], %[[ADD]], %[[CONST_1]]
// CHECK: return %[[CLAMP]] : tensor<1x1x64xf32>
// CHECK: }
