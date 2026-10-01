from pathlib import Path

from PIL import Image


def write_icon(source: Path, dest: Path) -> None:
    image = Image.open(source).convert("RGBA")
    side = min(image.size)
    left = (image.width - side) // 2
    top = (image.height - side) // 2
    image = image.crop((left, top, left + side, top + side))
    dest.parent.mkdir(parents=True, exist_ok=True)
    image.save(dest, format="ICO", sizes=[(size, size) for size in (16, 24, 32, 48, 64, 256)])


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    write_icon(root / "assets" / "icon.JPG", root / "cpp" / "build" / "app.ico")
