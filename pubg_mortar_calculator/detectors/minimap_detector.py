from enum import Enum

import cv2
import numpy as np
import onnxruntime as ort

from ..utils import paths


class MinimapType(Enum):
    NO_MINIMAP = 0
    SMALL_MINIMAP = 1
    LARGE_MINIMAP = 2


class MinimapDetector:
    def __init__(self) -> None:
        self.session = ort.InferenceSession(
            paths.map_detection_model(), providers=["CPUExecutionProvider"]
        )

    def detect(self, image: np.ndarray) -> MinimapType:
        image = self._process_input(image)
        input_name = self.session.get_inputs()[0].name
        output_name = self.session.get_outputs()[0].name

        outputs = self.session.run([output_name], {input_name: [image]})
        logits = outputs[0][0]  # type: ignore

        exp_logits = np.exp(logits - np.max(logits))
        probs = exp_logits / np.sum(exp_logits)

        pred_class_id = int(np.argmax(probs))
        confidence = probs[pred_class_id] * 100
        return MinimapType(pred_class_id)

    def _process_input(self, image: np.ndarray):
        image = self._letterbox(image)
        image = cv2.resize(image, (224, 224), interpolation=cv2.INTER_AREA)
        image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
        image = image.astype(np.float32) / 255.0
        mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
        std = np.array([0.229, 0.224, 0.225], dtype=np.float32)
        image = (image - mean) / std
        image = image.transpose((2, 0, 1))
        return image

    def _letterbox(self, image: np.ndarray) -> np.ndarray:
        h, w = image.shape[:2]
        max_dim = max(w, h)

        pad_left = (max_dim - w) // 2
        pad_top = (max_dim - h) // 2
        pad_right = max_dim - w - pad_left
        pad_bottom = max_dim - h - pad_top

        return cv2.copyMakeBorder(
            image,
            pad_top,
            pad_bottom,
            pad_left,
            pad_right,
            borderType=cv2.BORDER_CONSTANT,
            value=(0, 0, 0),
        )


if __name__ == "__main__":
    from matplotlib import pyplot as plt

    detector = MinimapDetector()

    image = cv2.imread(
        r"C:\Users\patri\Desktop\dataset\images\train\swamp_map_blue_962_358_958_574.jpg"
    )

    if image is None:
        exit("Cannot read image")

    detected = detector.detect(image)

    image = detector._process_input(image)

    mean = np.array([0.485, 0.456, 0.406])
    std = np.array([0.229, 0.224, 0.225])
    img_np = image.transpose(1, 2, 0)
    img_display = np.clip(img_np * std + mean, 0, 1)

    plt.figure(figsize=(5, 5))
    plt.imshow(img_display)
    plt.title(
        f"Pred: {detected}",
        fontsize=12,
        fontweight="bold",
        color="green",
    )
    plt.axis("off")
    plt.tight_layout()
    plt.show()
