import customtkinter as ct

from pubg_mortar_calculator.customtkinter_widgets import Checkbox, Combobox, Slider


class MarkDetectorBlock(ct.CTkFrame):
    def __init__(self, master, on_update, on_map_preview, *args, **kwargs):
        super().__init__(master, fg_color="transparent", *args, **kwargs)

        self.columnconfigure([0, 1], weight=1)
        self.rowconfigure([0, 1, 2, 3], weight=1)

        self.draw_checkbox = Checkbox(
            self,
            text="마커 표시",
            command=on_update,
            saving_id="mark_detection_draw_checkbox",
            default=True,
        ).grid(row=0, column=0, padx=5, pady=5)

        self.show_processed_image_checkbox = Checkbox(
            self,
            text="처리 결과 표시",
            command=on_update,
            saving_id="mark_detection_show_processed_image_checkbox",
        ).grid(row=0, column=1, padx=5, pady=5)

        self.zoom_to_points_checkbox = Checkbox(
            self,
            text="지점 확대",
            command=on_update,
            saving_id="mark_detection_zoom_to_points_checkbox",
            default=True,
        ).grid(row=1, column=0, padx=5)

        self.yolo_checkbox = Checkbox(
            self,
            text="YOLO 사용",
            command=on_update,
            saving_id="mark_detection_yolo_checkbox",
            default=False,
        ).grid(row=1, column=1, padx=5)

        ct.CTkLabel(self, text="마커 색상: ").grid(row=2, column=0, padx=5, pady=5)

        self.color_combobox = Combobox(
            self,
            values=["orange", "yellow", "blue", "green"],
            labels={
                "orange": "주황",
                "yellow": "노랑",
                "blue": "파랑",
                "green": "초록",
            },
            command=on_update,
            return_value=False,
            saving_id="mark_detection_color_combobox",
        )
        self.color_combobox.grid(row=2, column=1, padx=5, pady=5)

        self.min_radius_slider = Slider(
            self,
            "마커 최소 반지름",
            "mark_detection_min_radius_slider",
            0,
            50,
            default=5,
            command=lambda: on_update(),
        )
        self.min_radius_slider.grid(row=3, column=0, columnspan=2)

        self.max_radius_slider = Slider(
            self,
            "마커 최대 반지름",
            "mark_detection_max_radius_slider",
            5,
            50,
            default=30,
            command=lambda: on_update(),
        )
        self.max_radius_slider.grid(row=4, column=0, columnspan=2)

        self.debug_load_map_preview_button = ct.CTkButton(
            self, text="지도 미리보기 불러오기", command=on_map_preview
        )
        self.debug_load_map_preview_button.grid(row=5, columnspan=2)
