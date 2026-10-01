import sys
from pathlib import Path
from tkinter import filedialog


def _frozen() -> bool:
    return bool(getattr(sys, "frozen", False))


def project() -> Path:
    if _frozen():
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parents[2]


def _bundle_root() -> Path:
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        return Path(meipass)
    return project()


def assets() -> Path:
    return _bundle_root() / "assets"


def _asset_file(name: str) -> Path:
    beside_program = project() / "assets" / name
    if beside_program.exists():
        return beside_program
    bundled = assets() / name
    if bundled.exists():
        return bundled
    return beside_program


def app_icon() -> Path:
    return _asset_file("icon.JPG")


def mortar_distances() -> Path:
    return _asset_file("mortar_distances.txt")


def settings() -> Path:
    return project() / "settings.json"


def temp() -> Path:
    path = project() / "temp"
    path.mkdir(parents=True, exist_ok=True)
    return path


def map_preview() -> Path:
    return temp() / "map_preview.png"


def elevation_preview() -> Path:
    return temp() / "elevation_preview.png"


def debug_files() -> Path:
    path = project() / "debug_files"
    path.mkdir(parents=True, exist_ok=True)
    return path


def map_detection_model() -> Path:
    return _asset_file("map_model.onnx")


def airdrop_detection_model() -> Path:
    return _asset_file("airdrop_model.onnx")


def mark_detection_model() -> Path:
    return _asset_file("mark_model.onnx")


def get_image() -> Path | None:
    image_path = filedialog.askopenfilename(
        title="이미지 선택",
        filetypes=[
            ("이미지 파일", "*.png;*.jpg"),
            ("PNG 파일", "*.png"),
            ("JPG 파일", "*.jpg"),
        ],
    )
    return Path(image_path) if image_path else None
