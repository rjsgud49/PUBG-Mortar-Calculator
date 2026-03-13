import numpy as np

from src.yolo11_onnx_detector import Yolo11OnnxDetector

from ..utils import paths


class MinimapDetector:
    def __init__(self) -> None:
        self.detector = Yolo11OnnxDetector(
            paths.map_detection_model(), ["map"], 0.2, 0.2
        )

    def detect(self, bgr_image: np.ndarray) -> list[int] | None:
        detections = self.detector.detect(bgr_image)

        if len(detections) > 0:
            detection = max(detections, key=lambda i: i.confidence)
            return detection.box
        return None

    def change_confidence(self, confidence: float):
        self.detector.confidence = confidence
        self.detector.iou_threshold = confidence
