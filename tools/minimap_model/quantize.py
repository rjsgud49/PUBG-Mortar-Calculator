import torch, torchvision
import torch.ao.quantization as quantization
from torchvision import transforms
from torch.utils.data import Dataset, DataLoader
from pathlib import Path
from dataset import SquarePad
from model import MinimapModel
from dataset import MinimapDataset
import torch
from onnxruntime.quantization import quantize_dynamic, QuantType
from model import MinimapModel

model = MinimapModel(num_classes=3, pretrained=False)

model.load_state_dict(torch.load(Path("tools\\minimap_model\\best.pth"), map_location="cpu"))

model.eval()

prepared_model = quantization.prepare(model, inplace=False)

# transform = torchvision.transforms.Compose([
#     SquarePad(),
#     transforms.Resize((224, 224)),
#     transforms.ToTensor(),
#     transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225])
# ])

# val_dataset = MinimapDataset(Path(r"C:\Users\patri\Desktop\dataset"), False, transform=transform)

fp32_onnx_path = Path("tools\\minimap_model\\best_fp32.onnx")
torch.onnx.export(
    model,
    torch.randn(1, 3, 224, 224),
    fp32_onnx_path,
    export_params=True,
    opset_version=13,
    do_constant_folding=True,
    input_names=["input"],
    output_names=["output"],
    dynamic_axes={"input": {0: "batch_size"}, "output": {0: "batch_size"}},
    dynamo=False
)

int8_onnx_path = Path("tools\\minimap_model\\best_int8.onnx")
quantize_dynamic(
    model_input=fp32_onnx_path,
    model_output=int8_onnx_path,
    weight_type=QuantType.QUInt8
)