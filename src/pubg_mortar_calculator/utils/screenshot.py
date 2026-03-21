import cv2
import numpy as np
from mss import mss
from screeninfo import get_monitors


def take_screenshot(monitor_id: int = 0) -> np.ndarray:
    monitors = get_monitors()[monitor_id]
    region = {
        "top": 0,
        "left": 0,
        "width": monitors.width,
        "height": monitors.height,
    }

    with mss() as sct:
        screenshot = sct.grab(region)

    return cv2.cvtColor(np.array(screenshot), cv2.COLOR_BGRA2BGR)
