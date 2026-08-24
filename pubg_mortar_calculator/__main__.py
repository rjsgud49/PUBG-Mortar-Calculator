def main():
    from .logger import get_logger
    from .ui.app import App
    from .utils.screenshot import take_screenshot

    LOGGER = get_logger()

    LOGGER.info(f"{'=' * 15} PUBG-Mortar-Calculator {'=' * 15}")

    LOGGER.info("Loading settings...")

    app = App()

    LOGGER.debug("Starting program...")
    app.mainloop()


if __name__ == "__main__":
    main()
