def main():
    from .logger import get_logger
    from .ui.app import App
    from .utils.screenshot import take_screenshot

    LOGGER = get_logger()

    LOGGER.info(f"{'=' * 15} PUBG-Mortar-Calculator {'=' * 15}")

    LOGGER.info("설정을 불러오는 중...")

    app = App()

    LOGGER.debug("프로그램을 시작합니다...")
    app.mainloop()


if __name__ == "__main__":
    import multiprocessing

    multiprocessing.freeze_support()
    main()
