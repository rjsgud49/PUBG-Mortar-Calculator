import logging
import sys

from pubg_mortar_calculator.core.settings_loader import SettingsLoader
from pubg_mortar_calculator.utils import paths

comtypes_logger = logging.getLogger("comtypes")

comtypes_logger.setLevel(logging.WARNING)


def get_logger():
    logger = logging.getLogger("PUBG-Mortar-Calculator")

    if logger.hasHandlers():
        return logger

    mode = SettingsLoader().get("general_settings_debug_mode_checkbox")

    level = logging.DEBUG if mode is not None and mode else logging.INFO
    logger.setLevel(level)

    console_formatter = logging.Formatter(
        fmt="[%(asctime)s.%(msecs)03d] - %(message)s", datefmt="%H:%M:%S"
    )

    file_formatter = logging.Formatter(
        fmt="[%(asctime)s.%(msecs)03d] - %(levelname)s - %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    file_handler = logging.FileHandler(
        paths.project() / "debug.log", mode="a", encoding="utf-8"
    )
    file_handler.setFormatter(file_formatter)
    logger.addHandler(file_handler)

    if sys.stdout is not None:
        console_handler = logging.StreamHandler(sys.stdout)
        console_handler.setFormatter(console_formatter)
        logger.addHandler(console_handler)

    return logger
