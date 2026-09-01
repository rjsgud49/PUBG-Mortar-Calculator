import os
import xml.etree.ElementTree as ET
from pathlib import Path

FOLDER_PATH = Path(r"tests\fixtures")


def convert_bbox(size, box):
    dw = 1.0 / size[0]
    dh = 1.0 / size[1]
    x = (box[0] + box[1]) / 2.0
    y = (box[2] + box[3]) / 2.0
    w = box[1] - box[0]
    h = box[3] - box[2]
    return (x * dw, y * dh, w * dw, h * dh)


def convert_annotation(xml_path, classes):
    tree = ET.parse(xml_path)
    root = tree.getroot()
    size = root.find("size")
    w = float(size.find("width").text)
    h = float(size.find("height").text)

    txt_filename = os.path.splitext(xml_path)[0] + ".txt"

    lines = []
    for obj in root.findall("object"):
        cls_name = obj.find("name").text
        if cls_name not in classes:
            continue
        cls_id = classes.index(cls_name)
        xmlbox = obj.find("bndbox")
        b = (
            float(xmlbox.find("xmin").text),
            float(xmlbox.find("xmax").text),
            float(xmlbox.find("ymin").text),
            float(xmlbox.find("ymax").text),
        )
        bb = convert_bbox((w, h), b)
        lines.append(f"{cls_id} " + " ".join([f"{a:.6f}" for a in bb]))

    with open(txt_filename, "w") as out_file:
        out_file.write("\n".join(lines))


def main():
    classes_file = os.path.join(FOLDER_PATH, "classes.txt")
    if os.path.exists(classes_file):
        with open(classes_file, "r") as f:
            classes = [line.strip() for line in f if line.strip()]
    else:
        classes = []

    for file in os.listdir(FOLDER_PATH):
        if file.endswith(".xml"):
            xml_path = os.path.join(FOLDER_PATH, file)
            convert_annotation(xml_path, classes)


if __name__ == "__main__":
    main()
