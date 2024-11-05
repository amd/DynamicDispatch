# Copyright (c) 2024 Advanced Micro Devices, Inc
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
# the Software, and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import os
import numpy as np
import onnx
from onnx.helper import (
    make_model,
    make_node,
    make_graph,
    make_opsetid,
    make_tensor_value_info,
    make_tensor,
)
from onnx.checker import check_model
import onnxruntime
from ryzenai_dynamic_dispatch import onnx_graph as ogm
from ryzenai_dynamic_dispatch import fuse
import argparse

np.random.seed(42)

QDQ_PARAMS_SIZE = 16


def shape2tuple(shape):
    return tuple(getattr(d, "dim_value", 0) for d in shape.dim)


def create_single_mladfmatmul_model(M, K, N, InT, WtT, OutT):
    X = make_tensor_value_info("X", InT, [1, M, K])
    Y = make_tensor_value_info("Y", OutT, [1, M, N])

    np_wts = np.random.randint(low=-5, high=5, size=(K, N)).astype(np.uint8)
    wts_tsor = make_tensor(f"weights", onnx.TensorProto.UINT8, np_wts.shape, np_wts)

    np_bias = np.random.randint(low=-5, high=5, size=(1, N)).astype(np.float32)
    bias_tsor = make_tensor(f"bias", onnx.TensorProto.FLOAT, np_bias.shape, np_bias)

    np_scales = np.random.randint(low=-5, high=5, size=(1, int(N*K/128))).astype(np.float32)
    scales_tsor = make_tensor(f"scales", onnx.TensorProto.FLOAT, np_scales.shape, np_scales)

    np_zeros = np.random.randint(low=-5, high=5, size=(1, int(N*K/128))).astype(np.uint8)
    zeros_tsor = make_tensor(f"zeros", onnx.TensorProto.UINT8, np_zeros.shape, np_zeros)

    mul1 = make_node(
        name="mul1",
        op_type="MladfMatMul",
        inputs=["X", "weights", "bias", "scales", "zeros", ],
        outputs=["Y"],
    )
    attr = onnx.helper.make_attribute("group_size", 128)
    mul1.attribute.append(attr)

    graph = make_graph(
        [mul1],
        "lr",
        [X],
        [Y],
        initializer=[wts_tsor, bias_tsor, scales_tsor, zeros_tsor],
    )
    onnx_model = make_model(graph, opset_imports=[make_opsetid("", 19)])
    return onnx_model, [np_wts]


if __name__ == "__main__":

    dir_name = "test_mladfmatmul_tile"
    model_name = dir_name + "/model_mladfmatmul.onnx"
    json_name = dir_name + "/model_mladfmatmul_meta.json"
    M, K, N = (4096, 4096, 11008)
    onnx_model, wts = create_single_mladfmatmul_model(
    M, K, N, onnx.TensorProto.BFLOAT16, onnx.TensorProto.UINT8, onnx.TensorProto.BFLOAT16)

    os.makedirs(dir_name, exist_ok=True)

    onnx.save(onnx_model, f"{model_name}")
    metainfo = fuse.prepare_metadata(ogm.ONNXGraph(onnx_model), dir_name)
    json_str = fuse.save_tensors_to_json(
        f"{json_name}", *metainfo
    )
    print("JSON Metadata saved to", f"{json_name}")
