from pathlib import Path

import cv2
import matplotlib.pyplot as plt
import numpy as np
import onnxruntime as ort
import torchvision.transforms as transforms
from dataset import SquarePad
from PIL import Image


def test_single_image(image_path: Path):
    onnx_path = Path("tools/minimap_model/best_int8.onnx")
    session = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    input_name = session.get_inputs()[0].name
    output_name = session.get_outputs()[0].name

    transform = transforms.Compose(
        [
            SquarePad(),
            transforms.Resize((224, 224)),
            transforms.ToTensor(),
            transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225]),
        ]
    )

    image_pil = Image.open(image_path).convert("RGB")
    image_tensor = transform(image_pil)
    input_numpy = image_tensor.unsqueeze(0).numpy()

    outputs = session.run([output_name], {input_name: input_numpy})
    logits = outputs[0][0]

    exp_logits = np.exp(logits - np.max(logits))
    probs = exp_logits / np.sum(exp_logits)

    pred_class_id = int(np.argmax(probs))
    confidence = probs[pred_class_id] * 100

    classes = ["No Minimap", "Small Minimap", "Large Minimap"]

    mean = np.array([0.485, 0.456, 0.406])
    std = np.array([0.229, 0.224, 0.225])
    img_np = image_tensor.permute(1, 2, 0).numpy()
    img_display = np.clip(img_np * std + mean, 0, 1)

    plt.figure(figsize=(5, 5))
    plt.imshow(img_display)
    plt.title(
        f"Pred: {classes[pred_class_id]} ({confidence:.1f}%)",
        fontsize=12,
        fontweight="bold",
        color="green",
    )
    plt.axis("off")
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    test_image_path = Path(
        r"C:\Users\patri\Desktop\dataset\images\train\swamp_map_blue_962_358_958_574.jpg"
    )
    test_single_image(test_image_path)
