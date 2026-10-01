import multiprocessing
import sys
import traceback
from pathlib import Path


def _write_startup_error(exc: BaseException) -> None:
    if getattr(sys, "frozen", False):
        log_path = Path(sys.executable).with_name("startup_error.txt")
    else:
        log_path = Path("startup_error.txt")
    log_path.write_text("".join(traceback.format_exception(exc)), encoding="utf-8")


if __name__ == "__main__":
    multiprocessing.freeze_support()
    try:
        from pubg_mortar_calculator.__main__ import main

        main()
    except Exception as exc:
        _write_startup_error(exc)
        raise
