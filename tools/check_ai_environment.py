from __future__ import annotations

import json

import matplotlib
import numpy as np
import onnx
import onnxruntime as ort
import pandas as pd
import pybind11
import sklearn
import torch
import yaml
import zstandard


def main() -> int:
    matrix = torch.tensor([[1.0, 2.0], [3.0, 4.0]])
    product = matrix @ matrix
    summary = {
        "python_stack": "ok",
        "torch": torch.__version__,
        "torch_cuda_available": torch.cuda.is_available(),
        "torch_matrix_product": product.tolist(),
        "onnx": onnx.__version__,
        "onnxruntime": ort.__version__,
        "onnxruntime_providers": ort.get_available_providers(),
        "numpy": np.__version__,
        "pandas": pd.__version__,
        "scikit_learn": sklearn.__version__,
        "matplotlib": matplotlib.__version__,
        "pyyaml": yaml.__version__,
        "zstandard": zstandard.__version__,
        "pybind11": pybind11.__version__,
    }
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
