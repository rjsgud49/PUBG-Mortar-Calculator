import customtkinter as ct

from customtkinter_widgets import Checkbox, Slider


class MinimapDetectorBlock(ct.CTkFrame):
    def __init__(self, master, on_update, *args, **kwargs):
        super().__init__(master, fg_color="transparent", *args, **kwargs)

        self.columnconfigure(0, weight=1)
        self.rowconfigure([0, 1], weight=1)

        self.enabled_checkbox = Checkbox(
            self,
            text="Enabled",
            command=on_update,
            saving_id="minimap_detector_enabled_checkbox",
            default=True,
        ).grid(row=0, column=0, padx=5, pady=5)

        self.confidence_slider = Slider(
            self,
            "Confidence",
            "minimap_detector_confidence_slider",
            0,
            100,
            30,
            command=on_update,
        )
        self.confidence_slider.grid(row=1, column=0)
