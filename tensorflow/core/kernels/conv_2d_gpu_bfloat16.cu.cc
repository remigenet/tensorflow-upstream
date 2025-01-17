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

#if GOOGLE_CUDA || TENSORFLOW_USE_ROCM

#define EIGEN_USE_GPU

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#include "tensorflow/core/kernels/conv_2d.h"
#include "tensorflow/core/kernels/conv_2d_gpu.h"

namespace tensorflow {

namespace functor {

template struct SwapDimension1And2InTensor3<Eigen::GpuDevice, Eigen::bfloat16>;

// For 2d ops.
template struct TransformFilter<Eigen::GpuDevice, Eigen::bfloat16, int, 4>;
template struct ReverseTransformFilter<Eigen::GpuDevice, Eigen::bfloat16, 4>;
template struct NHWCToNCHW<Eigen::GpuDevice, Eigen::bfloat16, 4>;
template struct NCHWToNHWC<Eigen::GpuDevice, Eigen::bfloat16, 4>;
template struct PadInput<Eigen::GpuDevice, Eigen::bfloat16, int, 4>;

// For 3d ops.
template struct TransformFilter<Eigen::GpuDevice, Eigen::bfloat16, int, 5>;
template struct ReverseTransformFilter<Eigen::GpuDevice, Eigen::bfloat16, 5>;
template struct NHWCToNCHW<Eigen::GpuDevice, Eigen::bfloat16, 5>;
template struct NCHWToNHWC<Eigen::GpuDevice, Eigen::bfloat16, 5>;
template struct PadInput<Eigen::GpuDevice, Eigen::bfloat16, int, 5>;

}  // namespace functor
}  // namespace tensorflow

#endif  // GOOGLE_CUDA  