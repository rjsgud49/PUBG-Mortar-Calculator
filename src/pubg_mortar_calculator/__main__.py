def main():
    import threading
    import time

    import customtkinter
    import keyboard

    from app_overlay import Clear

    from .logger import get_logger
    from .settings_loader import SettingsLoader
    from .ui.app import App
    from .utils.screenshot import take_screenshot

    LOGGER = get_logger()

    def setup_key_listeners(app: App):
        calc_key = app.get_calculation_key()
        elev_key = app.get_elevation_key()
        all_in_one_key = app.get_all_in_one_key()

        def handle_calc():
            if app.overlay is not None:
                app.overlay.add_command(Clear())
            time.sleep(0.1)
            app.set_map_image(take_screenshot())

        def handle_elev():
            app.set_elevation_image(take_screenshot())

        def handle_all_in_one():
            if app.overlay is not None:
                app.overlay.add_command(Clear())
            time.sleep(0.1)
            screenshot = take_screenshot()
            app.set_map_image(screenshot, False)
            app.set_elevation_image(screenshot)

        keyboard.add_hotkey(calc_key, handle_calc)
        keyboard.add_hotkey(elev_key, handle_elev)
        keyboard.add_hotkey(all_in_one_key, handle_all_in_one)

    def on_closing():
        settings_loader.save()
        LOGGER.info("Goodbye!")
        exit()

    LOGGER.info(f"{'=' * 15} PUBG-Mortar-Calculator {'=' * 15}")

    LOGGER.info("Loading settings...")
    settings_loader = SettingsLoader()

    customtkinter.set_appearance_mode("System")
    customtkinter.set_default_color_theme("blue")
    app = App()
    app.protocol("WM_DELETE_WINDOW", on_closing)

    LOGGER.debug("Starting keyboard listeners...")
    t = threading.Thread(target=setup_key_listeners, args=(app,), daemon=True)
    t.start()

    LOGGER.debug("Starting program...")
    app.mainloop()


if __name__ == "__main__":
    main()
