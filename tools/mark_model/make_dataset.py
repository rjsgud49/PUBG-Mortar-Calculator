import shutil
from dataclasses import dataclass
from pathlib import Path

import cv2

DATASET_PATH = Path(r"A:\Datasets\PUBG\original")
OUTPUT_PATH = Path(__file__).resolve().parents[0] / "mark_dataset"
OUTPUT_PATH.mkdir(exist_ok=True)
MARGINS = [3, 5, 7]
CLASSE_IDS = [1, 2, 3, 4, 5, 6, 7, 8]


@dataclass
class Annotation:
    id: int
    x: float
    y: float
    w: float
    h: float


def get_annotations_from_file(file_path: Path) -> list[Annotation]:
    data = []
    with open(file_path, "r") as file:
        for line in file.readlines():
            slices = line.split(" ")
            if len(slices) < 5:
                continue

            class_id = int(slices[0])
            class_x = float(slices[1])
            class_y = float(slices[2])
            class_w = float(slices[3])
            class_h = float(slices[4])
            data.append(Annotation(class_id, class_x, class_y, class_w, class_h))
    return data


def write_annotations(path: Path, annotations: list[Annotation]):
    with open(path, "w") as file:
        for annotation in annotations:
            file.write(
                f"{annotation.id} {annotation.x} {annotation.y} {annotation.w} {annotation.h}\n"
            )


if __name__ == "__main__":
    for file in DATASET_PATH.iterdir():
        if file.name.endswith("txt"):
            if "classes" in file.name:
                shutil.copy(file, OUTPUT_PATH / file.name)
            continue
        image = cv2.imread(file)
        if image is None:
            exit(f"Could not read {file} as image")

        image_height, image_width = image.shape[:2]

        annotation_path = file.with_suffix(".txt")
        if annotation_path.exists():
            annotations = get_annotations_from_file(annotation_path)
        else:
            annotations = []

        print(file)
        for annotation in annotations:
            if not annotation.id in CLASSE_IDS:
                continue

            x = int(annotation.x * image_width)
            y = int(annotation.y * image_height)
            w = int(annotation.w * image_width)
            h = int(annotation.h * image_height)

            for margin in MARGINS:
                margin = int(max(w, h) * margin)
                image_x0 = max(0, x - margin)
                image_y0 = max(0, y - margin)
                image_x1 = min(image_width, image_x0 + margin * 2)
                image_y1 = min(image_height, image_y0 + margin * 2)

                cutted_image = image[image_y0:image_y1, image_x0:image_x1].copy()
                cutted_h, cutted_w = cutted_image.shape[:2]
                new_annotations = []

                for other_annotation in annotations:
                    if other_annotation.id not in CLASSE_IDS:
                        continue
                    other_x = int(other_annotation.x * image_width) - image_x0
                    other_y = int(other_annotation.y * image_height) - image_y0
                    other_w = int(other_annotation.w * image_width)
                    other_h = int(other_annotation.h * image_height)

                    if other_x >= other_w // 2 and other_x + other_w // 2 <= cutted_w:
                        if (
                            other_y >= other_h // 2
                            and other_y + other_h // 2 <= cutted_h
                        ):
                            # cv2.rectangle(cutted_image, (other_x-other_w//2, other_y-other_h//2), (other_x+other_w//2, other_y+other_h//2), (255, 0, 255), 2)

                            norm_x = max(0.0, min(1.0, other_x / cutted_w))
                            norm_y = max(0.0, min(1.0, other_y / cutted_h))
                            norm_w = max(0.0, min(1.0, other_w / cutted_w))
                            norm_h = max(0.0, min(1.0, other_h / cutted_h))

                            new_annotations.append(
                                Annotation(
                                    other_annotation.id, norm_x, norm_y, norm_w, norm_h
                                )
                            )

                new_annotation_name = Path(
                    f"{file.with_suffix('').name}^{margin}_{x}_{y}"
                )
                new_annotation_path = (
                    OUTPUT_PATH / f"{file.with_suffix('').name}^{margin}_{x}_{y}.txt"
                )
                new_image_path = new_annotation_path.with_suffix(".png")
                # cv2.imshow("A", cutted_image)
                # cv2.waitKey(0)
                write_annotations(new_annotation_path, new_annotations)
                cv2.imwrite(new_image_path, cutted_image)
