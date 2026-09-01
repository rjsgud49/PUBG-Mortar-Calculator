import customtkinter as ct

from pubg_mortar_calculator.customtkinter_widgets import Checkbox, Slider


class OverlaySettingsBlock(ct.CTkFrame):
    def __init__(self, master, on_overlay_change, *args, **kwargs):
        super().__init__(master, fg_color="transparent", *args, **kwargs)
        self.columnconfigure([0, 1], weight=1)
        self.rowconfigure([0, 1], weight=1)

        self.enabled_checkbox = Checkbox(
            self,
            text="Enabled",
            saving_id="overlay_settings_enabled_checkbox",
            command=on_overlay_change,
        ).grid(row=0, column=0, padx=5, pady=5)

        self.draw_borders_checkbox = Checkbox(
            self,
            text="Draw Borders",
            saving_id="overlay_settings_draw_borders_checkbox",
            command=on_overlay_change,
        ).grid(row=0, column=1, padx=5, pady=5)

        self.draw_map_marks_checkbox = Checkbox(
            self,
            text="Draw Map Marks",
            saving_id="overlay_settings_draw_map_marks_checkbox",
            command=on_overlay_change,
        ).grid(row=1, column=0, padx=5, pady=5)

        self.draw_elevation_marks_checkbox = Checkbox(
            self,
            text="Draw Elevation Marks",
            saving_id="overlay_settings_draw_elevation_marks_checkbox",
            command=on_overlay_change,
        ).grid(row=1, column=1, padx=5, pady=5)

        self.draw_minimap_box_checkbox = Checkbox(
            self,
            text="Draw Minimap Box",
            saving_id="overlay_settings_draw_minimap_box_checkbox",
            command=on_overlay_change,
        ).grid(row=2, column=0, columnspan=2, padx=5, pady=5)

        self.scale_slider = Slider(
            self,
            "Scale",
            "overlay_settings_scale_slider",
            50,
            250,
            100,
            command=on_overlay_change,
        )
        self.scale_slider.grid(row=3, columnspan=2, padx=5, pady=5)
