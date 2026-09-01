import re
from pathlib import Path

import cv2

ROOT_DIR = Path(__file__).parent.parent
FIXTURE_DIR = ROOT_DIR / "tests" / "fixtures"

MAX_DIMENSION = 19200
JPEG_QUALITY = 70  # 0 to 100 (lower = smaller file size, but more blurry)


def compress_and_rename():
    if not FIXTURE_DIR.exists():
        print(f"Directory not found: {FIXTURE_DIR}")
        return

    files = []
    files.extend(FIXTURE_DIR.glob("*.png"))

    for img_path in files:
        match = re.search(r"^(.*)_(\d+)\.(png)$", img_path.name, re.IGNORECASE)

        if not match:
            print(f"Skipping '{img_path.name}' -> Doesn't match format 'name_GAP.ext'")
            continue

        prefix = match.group(1)
        old_gap = float(match.group(2))

        img = cv2.imread(str(img_path))
        if img is None:
            print(f"Skipping '{img_path.name}' -> Could not read file")
            continue

        h, w = img.shape[:2]
        scale = 1.0

        if w > MAX_DIMENSION or h > MAX_DIMENSION:
            scale = MAX_DIMENSION / max(w, h)
            new_w, new_h = int(w * scale), int(h * scale)
            img = cv2.resize(img, (new_w, new_h), interpolation=cv2.INTER_AREA)

        new_gap = int(round(old_gap * scale))

        new_filename = f"{prefix}_{new_gap}.jpg"
        new_filepath = FIXTURE_DIR / new_filename

        cv2.imwrite(str(new_filepath), img, [cv2.IMWRITE_JPEG_QUALITY, JPEG_QUALITY])

        if img_path != new_filepath:
            img_path.unlink()

        print(f"Processed: '{img_path.name}' -> '{new_filename}' (Scale: {scale:.2f})")


if __name__ == "__main__":
    compress_and_rename()
