##
## Copyright (c) 2025 Advanced Micro Devices, Inc
##
## Permission is hereby granted, free of charge, to any person obtaining a copy
## of this software and associated documentation files (the "Software"), to deal
## in the Software without restriction, including without limitation the rights
## to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
## copies of the Software, and to permit persons to whom the Software is
## furnished to do so, subject to the following conditions:
##
## The above copyright notice and this permission notice shall be included in
## all copies or substantial portions of the Software.
##
## THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
## IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
## FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
## AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
## LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
## OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
## SOFTWARE.

from . import onnx_graph as ogm
import numpy as np
import onnx

__HW_MIN_SIZE = 128


def is_multiple(x, M):
    return x % M == 0

def tune_input_nodes(onnx_graph: ogm.ONNXGraph):
    onnx_graph.aux_info["original_inputs"] = {}
    in_nodes = onnx_graph.getPrimaryInputs()
    for in_node in in_nodes:
        prop = ogm.parseValueInfoProto(in_node.op)
        # Do the transformations here
        onnx_graph.aux_info["original_inputs"][in_node.name] = prop


def tune_output_nodes(onnx_graph: ogm.ONNXGraph):
    onnx_graph.aux_info["original_outputs"] = {}
    out_nodes = onnx_graph.getPrimaryOutputs()
    for out in out_nodes:
        prop = ogm.parseValueInfoProto(out.op)
        if prop["shape"][-1] < __HW_MIN_SIZE:
            parent_nodes = out.inputs
            for node in parent_nodes:
                if node.op.op_type in {"MatMul"}:
                    matmul_wts_name = node.op.input[1]
                    matmul_wts = onnx_graph.nodev[matmul_wts_name]
                    # if the weights are also inputs, then it won't be a TensorProto
                    if not isinstance(matmul_wts.op, onnx.onnx_ml_pb2.TensorProto):
                        continue
                    wts_prop = ogm.parseTensorProto(matmul_wts.op)
                    wts = np.frombuffer(
                        matmul_wts.op.raw_data, dtype=wts_prop["dtype"]
                    ).reshape(wts_prop["shape"])
                    padded_wts = np.pad(
                        wts, ((0, 0), (0, __HW_MIN_SIZE - wts_prop["shape"][1]))
                    )
                    new_op = onnx.numpy_helper.from_array(padded_wts, matmul_wts_name)
                    matmul_wts.op = new_op
                elif node.op.op_type in {"MatMulAdd"}:
                    matmul_wts_name = node.op.input[1]
                    matmul_wts = onnx_graph.nodev[matmul_wts_name]
                    wts_prop = ogm.parseTensorProto(matmul_wts.op)
                    wts = np.frombuffer(
                        matmul_wts.op.raw_data, dtype=wts_prop["dtype"]
                    ).reshape(wts_prop["shape"])
                    padded_wts = np.pad(
                        wts, ((0, 0), (0, __HW_MIN_SIZE - wts_prop["shape"][1]))
                    )
                    new_op = onnx.numpy_helper.from_array(padded_wts, matmul_wts_name)
                    matmul_wts.op = new_op

                    matmul_bias_name = node.op.input[2]
                    matmul_bias = onnx_graph.nodev[matmul_bias_name]
                    bias_prop = ogm.parseTensorProto(matmul_bias.op)
                    bias = np.frombuffer(
                        matmul_bias.op.raw_data, dtype=bias_prop["dtype"]
                    ).reshape(bias_prop["shape"])
                    padded_bias = np.pad(
                        bias, ((0, __HW_MIN_SIZE - bias_prop["shape"][-1]))
                    )
                    new_op = onnx.numpy_helper.from_array(padded_bias, matmul_bias_name)
                    matmul_bias.op = new_op
                else:
                    raise RuntimeError(
                        f"No padding handler specified for {node.op.op_type}"
                    )

            out.op.type.tensor_type.shape.dim[-1].dim_value = __HW_MIN_SIZE

        onnx_graph.aux_info["original_outputs"][out.name] = prop
        # pass


def tune_graph(onnx_graph: ogm.ONNXGraph):
    tune_input_nodes(onnx_graph)
    tune_output_nodes(onnx_graph)
    pass
