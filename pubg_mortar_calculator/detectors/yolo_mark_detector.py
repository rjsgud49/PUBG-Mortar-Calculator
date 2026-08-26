import numpy as np

from pubg_mortar_calculator.detectors.yolo11_onnx_detector import (
    Detection,
    Yolo11OnnxDetector,
)

from ..utils import paths


class YoloMarkDetector:
    def __init__(self) -> None:
        self.detector = Yolo11OnnxDetector(
            paths.mark_detection_model(),
            [
                "map",
                "yellow_mark",
                "yellow_player_mark",
                "orange_mark",
                "orange_player_mark",
                "blue_mark",
                "blue_player_mark",
                "green_mark",
                "green_player_mark",
                "yellow_ingame_mark",
                "orange_ingame_mark",
                "blue_ingame_mark",
                "green_ingame_mark",
            ],
            0.1,
            0.1,
        )

    def get_player_and_mark_pos(
        self, bgr_image: np.ndarray, color: str
    ) -> tuple[tuple[int, int] | None, tuple[int, int] | None]:
        detections = self._detect(bgr_image)

        players: list[Detection] = []
        marks: list[Detection] = []
        for detection in detections:
            if (
                detection.class_name is not None
                and "player_mark" in detection.class_name
            ):
                players.append(detection)
            elif detection.class_name is not None and "mark" in detection.class_name:
                marks.append(detection)

        player = None
        for detection in players:
            if detection.class_name is not None and color in detection.class_name:
                player = detection
                break

        mark = None
        for detection in marks:
            if detection.class_name is not None and color in detection.class_name:
                mark = detection
                break

        player_pos = None
        if player is not None:
            player_pos = (
                round((player.box[0] + player.box[2]) / 2),
                round((player.box[1] + player.box[3]) / 2),
            )

        mark_pos = None
        if mark is not None:
            mark_pos = (
                round((mark.box[0] + mark.box[2]) / 2),
                mark.box[3],
            )
        return (player_pos, mark_pos)

    def change_confidence(self, confidence: float):
        self.detector.confidence = confidence
        self.detector.iou_threshold = confidence

    def _detect(self, image: np.ndarray) -> list[Detection]:
        detections = self.detector.detect(image)
        return detections
