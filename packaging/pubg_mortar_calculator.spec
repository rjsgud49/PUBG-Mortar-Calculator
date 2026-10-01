# -*- mode: python ; coding: utf-8 -*-
import sys
from pathlib import Path

from PyInstaller.utils.hooks import collect_all

ROOT = Path(SPECPATH).resolve().parent
sys.path.insert(0, str(ROOT / "packaging"))
from make_icon import write_icon

ICON_JPG = ROOT / "assets" / "icon.JPG"
ICON_ICO = ROOT / "cpp" / "build" / "app.ico"
if ICON_JPG.exists():
    write_icon(ICON_JPG, ICON_ICO)
datas = []
binaries = []
hiddenimports = []

for package in ("customtkinter", "onnxruntime", "pyttsx3"):
    package_datas, package_binaries, package_hidden = collect_all(package)
    datas += package_datas
    binaries += package_binaries
    hiddenimports += package_hidden

for name in ("mortar_distances.txt", "mark_model.onnx", "map_model.onnx", "icon.JPG"):
    source = ROOT / "assets" / name
    if source.exists():
        datas.append((str(source), "assets"))

a = Analysis(
    [str(ROOT / "packaging" / "launcher.py")],
    pathex=[str(ROOT)],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        "torch",
        "torchvision",
        "matplotlib",
        "pytest",
        "onnxscript",
    ],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="PUBG-Mortar-Calculator",
    icon=str(ICON_ICO) if ICON_ICO.exists() else None,
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
)
