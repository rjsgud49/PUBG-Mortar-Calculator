import re
from pathlib import Path

import cv2
import pytest

from src.pubg_mortar_calculator.detectors import MinimapDetector, MinimapType

FIXTURE_DIR = Path("tests/fixtures/maps")

def load_minimap_images():
    """Dynamically loads test cases and 4 coordinates from filenames."""
    test_cases = []

    if not FIXTURE_DIR.exists():
        return test_cases

    for img_path in FIXTURE_DIR.iterdir():
        if img_path.is_file() and img_path.suffix.lower() in [".png", ".jpg", ".jpeg"]:
            match = re.search(
                r"^(.*)_(.*)_(.*)_(\d+)_(\d+)_(\d+)_(\d+)\.(png|jpg|jpeg)$",
                img_path.name,
                re.IGNORECASE,
            )

            if match:
                minimap_type = match.group(1)
                scenario = match.group(3)
                test_cases.append((minimap_type, str(img_path), scenario))

    return test_cases


@pytest.mark.parametrize(
    "minimap_type, image_path, scenario",
    load_minimap_images(),
)
def test_minimap_type(
    minimap_type, image_path, scenario
):
    detector = MinimapDetector()
    image = cv2.imread(image_path)

    assert image is not None, f"Failed to load image for {scenario} at: {image_path}"

    predicted_minimap_type = detector.detect(image)

    assert predicted_minimap_type != MinimapType.NO_MINIMAP, (
        f"Scenario: {scenario} | No minimap detected!"
    )
   
    match minimap_type:
        case "small":
            truth_minimap_type = MinimapType.SMALL_MINIMAP
        case "large":
            truth_minimap_type = MinimapType.LARGE_MINIMAP
        case _:
            truth_minimap_type = MinimapType.NO_MINIMAP

    assert predicted_minimap_type == truth_minimap_type, (
        f"Type mismatch in {scenario} ({predicted_minimap_type}/{truth_minimap_type})"
    )