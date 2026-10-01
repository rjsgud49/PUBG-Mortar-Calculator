import time
import tkinter

import customtkinter as ct

from pubg_mortar_calculator.customtkinter_widgets import Image
from pubg_mortar_calculator.logger import get_logger
from pubg_mortar_calculator.utils import paths, take_screenshot

from ..core.app_logic import AppLogic
from ..core.app_overlay import *
from ..core.hotkey_service import HotkeyService
from ..core.models import (
    DictorSettings,
    ElevationSettings,
    GeneralSettings,
    GridSettings,
    MarkSettings,
    MinimapSettings,
    OverlaySettings,
)
from .blocks import (
    CalculationDataBlock,
    DictorSettingsBlock,
    ElevationDetectorBlock,
    GeneralSettingsBlock,
    GridDetectorBlock,
    MarkDetectorBlock,
    MinimapDetectorBlock,
    OverlaySettingsBlock,
)

LOGGER = get_logger()


def _write_window_icon():
    icon_path = paths.app_icon()
    if not icon_path.exists():
        return None
    from PIL import Image

    image = Image.open(icon_path).convert("RGBA")
    side = min(image.size)
    left = (image.width - side) // 2
    top = (image.height - side) // 2
    image = image.crop((left, top, left + side, top + side))
    dest = paths.temp() / "app.ico"
    image.save(dest, format="ICO", sizes=[(size, size) for size in (16, 24, 32, 48, 64, 256)])
    return dest

ct.ThemeManager.theme["CTkFont"]["family"] = "Malgun Gothic"


class App(ct.CTk):
    def _set_app_icon(self):
        try:
            icon_path = _write_window_icon()
        except Exception:
            LOGGER.exception("앱 아이콘을 열지 못했습니다.")
            return
        if icon_path is None:
            return
        self.iconbitmap(str(icon_path))

    def __init__(self):
        super().__init__()
        self.title("PUBG 박격포 계산기")
        self.resizable(False, False)
        self._set_app_icon()

        self.logic = AppLogic()

        # Left Frame
        self.left_frame = ct.CTkFrame(self)
        self.left_frame.grid(row=0, column=0, padx=5, pady=5)

        self.map_image_preview = Image(
            self.left_frame, (500, 290), save_aspect_ratio=True
        )
        self.map_image_preview.grid(row=0, column=0, columnspan=2, padx=5, pady=5)

        self.elevation_image_preview = Image(
            self.left_frame, (60, 450), save_aspect_ratio=True
        )
        self.elevation_image_preview.grid(row=0, column=3, rowspan=2, padx=5, pady=5)

        self.map_data_block = CalculationDataBlock(
            self.left_frame,
            "지도 정보",
            ["격자 간격", "마커 위치", "플레이어 위치", "거리", "미니맵"],
        )
        self.map_data_block.grid(row=1, column=0)

        self.elevation_data_block = CalculationDataBlock(
            self.left_frame,
            "고도 정보",
            ["마커 위치", "고도", "고도 반영 거리", "박격포 사거리"],
        )
        self.elevation_data_block.grid(row=1, column=1)

        # Right Frame
        self.right_frame = ct.CTkFrame(self, fg_color="transparent")
        self.right_frame.grid(row=0, column=1)

        self.tabview = ct.CTkTabview(self.right_frame)
        self.tabview.add("일반")
        self.tabview.add("격자")
        self.tabview.add("마커")
        self.tabview.add("고도")
        self.tabview.add("미니맵")
        self.tabview.add("음성")
        self.tabview.add("오버레이")
        self.tabview.pack(fill="both", expand=True, padx=10, pady=10)

        self.general_settings_block = GeneralSettingsBlock(
            self.tabview.tab("일반"),
            self._initialize_overlay,
            self._update_all_hotkeys,
        )
        self.general_settings_block.pack(fill="both", expand=True, padx=5, pady=5)

        self.dictor_settings_block = DictorSettingsBlock(self.tabview.tab("음성"))
        self.dictor_settings_block.pack(fill="both", expand=True, padx=5, pady=5)

        self.elevation_detector_block = ElevationDetectorBlock(
            self.tabview.tab("고도"),
            self.process_elevation_image,
            self.load_elevation_preview,
        )
        self.elevation_detector_block.pack(fill="both", expand=True, padx=5, pady=5)

        self.minimap_detector_block = MinimapDetectorBlock(
            self.tabview.tab("미니맵"), self.process_map_image
        )
        self.minimap_detector_block.pack(fill="both", expand=True, padx=5, pady=5)

        self.grid_detector_block = GridDetectorBlock(
            self.tabview.tab("격자"), self.process_map_image
        )
        self.grid_detector_block.pack(fill="both", expand=True, padx=5, pady=5)

        self.mark_detector_block = MarkDetectorBlock(
            self.tabview.tab("마커"), self.process_map_image, self.load_map_preview
        )
        self.mark_detector_block.pack(fill="both", expand=True, padx=5, pady=5)

        self.overlay_settings_block = OverlaySettingsBlock(
            self.tabview.tab("오버레이"), self._initialize_overlay
        )
        self.overlay_settings_block.pack(fill="both", expand=True, padx=5, pady=5)

        self.overlay: None | AppOverlay = None

        self._load_initial_previews()
        self._initialize_overlay()

        self.hotkey_service = HotkeyService()
        self._update_all_hotkeys()

        if not paths.map_detection_model().exists():
            LOGGER.warning(
                "미니맵 감지 모델을 찾을 수 없습니다: "
                + paths.map_detection_model().as_posix()
            )
            self.minimap_detector_block.enabled_checkbox.checkbox.configure(
                state=tkinter.DISABLED
            )
            self.minimap_detector_block.enabled_checkbox.set(False)

        if not paths.mark_detection_model().exists():
            LOGGER.warning(
                "마커 감지 모델을 찾을 수 없습니다: "
                + paths.mark_detection_model().as_posix()
            )
            self.mark_detector_block.yolo_checkbox.checkbox.configure(
                state=tkinter.DISABLED
            )
            self.mark_detector_block.yolo_checkbox.set(False)

    def _load_initial_previews(self):
        if self.logic.map_image is not None:
            self.process_map_image(combat=False)

    def _update_all_hotkeys(self):
        settings = self._get_general_settings()
        self.update_map_hotkey(settings.calculation_hotkey)
        self.update_elevation_hotkey(settings.elevation_hotkey)
        self.update_all_in_one_hotkey(settings.all_in_one_hotkey)

    def process_map_image(self, combat: bool = False, dictor: bool = True):
        if combat:
            if self.overlay is not None:
                self.overlay.add_command(Clear())
                time.sleep(0.05)
            self.logic.map_image = take_screenshot()

        if self.logic.map_image is None:
            return

        dictor_settings = self._get_dictor_settings()
        dictor_settings.enabled = dictor_settings.enabled and dictor

        processed_img, map_data = self.logic.set_map_image(
            self.logic.map_image,
            grid_settings=self._get_grid_settings(),
            mark_settings=self._get_mark_settings(),
            minimap_settings=self._get_minimap_settings(),
            dictor_settings=dictor_settings,
            general_settings=self._get_general_settings(),
            combat=combat,
        )

        self.map_image_preview.set_cv2(processed_img)
        self._update_map_data_ui(map_data)

        self.process_elevation_image(False)

        if self.overlay is not None:
            self.logic.draw_to_overlay(self.overlay, self._get_overlay_settings())

    def update_map_hotkey(self, key_str: str):
        self.hotkey_service.bind(
            action_name="map_calculation",
            key_combo=key_str,
            callback=lambda: self.after(0, lambda: self.process_map_image(True)),
        )

    def update_elevation_hotkey(self, key_str: str):
        self.hotkey_service.bind(
            action_name="elevation_calculation",
            key_combo=key_str,
            callback=lambda: self.after(0, lambda: self.process_elevation_image(True)),
        )

    def update_all_in_one_hotkey(self, key_str: str):
        self.hotkey_service.bind(
            action_name="all_in_one_calculation",
            key_combo=key_str,
            callback=lambda: self.after(0, self.all_in_one_calculation),
        )

    def process_elevation_image(self, combat: bool = False):
        if combat:
            if self.overlay is not None:
                self.overlay.add_command(Clear())
                time.sleep(0.05)
            self.logic.elevation_image = take_screenshot()

        if self.logic.elevation_image is None:
            return

        processed_img, elev_data = self.logic.set_elevation_image(
            self.logic.elevation_image,
            elevation_settings=self._get_elevation_settings(),
            mark_settings=self._get_mark_settings(),
            dictor_settings=self._get_dictor_settings(),
            general_settings=self._get_general_settings(),
            combat=combat,
        )

        if self.overlay is not None and combat:
            self.logic.draw_to_overlay(self.overlay, self._get_overlay_settings())

        self.elevation_image_preview.set_cv2(processed_img)
        self._update_elevation_data_ui(elev_data)

    def all_in_one_calculation(self):
        self.process_map_image(True, False)
        self.process_elevation_image(True)

    def _get_grid_settings(self) -> GridSettings:
        return GridSettings(
            canny1=self.grid_detector_block.canny1_threshold_slider.get(),
            canny2=self.grid_detector_block.canny2_threshold_slider.get(),
            line_threshold=self.grid_detector_block.line_threshold_slider.get() / 100,
            line_gap=self.grid_detector_block.line_gap_slider.get() / 100,
            line_merge=self.grid_detector_block.line_merge_threshold_slider.get(),
            show_processed=self.grid_detector_block.show_processed_image_checkbox.get(),
            draw_lines=self.grid_detector_block.draw_grid_lines_checkbox.get(),
        )

    def _get_mark_settings(self) -> MarkSettings:
        return MarkSettings(
            use_yolo=self.mark_detector_block.yolo_checkbox.get(),
            color=self.mark_detector_block.color_combobox.get(),
            min_radius=self.mark_detector_block.min_radius_slider.get(),
            max_radius=self.mark_detector_block.max_radius_slider.get(),
            draw_marks=self.mark_detector_block.draw_checkbox.get(),
            zoom_to_points=self.mark_detector_block.zoom_to_points_checkbox.get(),
            show_processed=self.mark_detector_block.show_processed_image_checkbox.get(),
        )

    def _get_minimap_settings(self) -> MinimapSettings:
        return MinimapSettings(
            enabled=self.minimap_detector_block.enabled_checkbox.get(),
            offset=self.minimap_detector_block.offset_slider.get(),
            small_size=self.minimap_detector_block.small_minimap_size_slider.get(),
            large_size=self.minimap_detector_block.large_minimap_size_slider.get(),
        )

    def _get_elevation_settings(self) -> ElevationSettings:
        return ElevationSettings(
            fov=self.elevation_detector_block.fov_slider.get(),
            draw_processed=self.elevation_detector_block.draw_processed_checkbox.get(),
            draw_points=self.elevation_detector_block.draw_points_checkbox.get(),
        )

    def _get_dictor_settings(self) -> DictorSettings:
        return DictorSettings(
            enabled=self.dictor_settings_block.dictor_checkbox.get(),
            volume=self.dictor_settings_block.volume_slider.get(),
            rate=self.dictor_settings_block.rate_slider.get(),
        )

    def _get_general_settings(self) -> GeneralSettings:
        return GeneralSettings(
            debug_mode=self.general_settings_block.debug_mode_checkbox.get(),
            calculation_hotkey=self.general_settings_block.map_hotkey_entry.get(),
            elevation_hotkey=self.general_settings_block.elevation_hotkey_entry.get(),
            all_in_one_hotkey=self.general_settings_block.all_in_one_hotkey_entry.get(),
            app_title=self.general_settings_block.title_entry.get(),
        )

    def _get_overlay_settings(self) -> OverlaySettings:
        return OverlaySettings(
            enabled=self.overlay_settings_block.enabled_checkbox.get(),
            draw_borders=self.overlay_settings_block.draw_borders_checkbox.get(),
            scale=self.overlay_settings_block.scale_slider.get(),
            draw_map_marks=self.overlay_settings_block.draw_map_marks_checkbox.get(),
            draw_elevation_marks=self.overlay_settings_block.draw_elevation_marks_checkbox.get(),
            draw_minimap=self.overlay_settings_block.draw_minimap_box_checkbox.get(),
        )

    def _update_map_data_ui(self, data):
        self.map_data_block.set_value(
            "격자 간격", f"{data.grid_gap}px" if data.grid_gap else "없음"
        )
        self.map_data_block.set_value(
            "마커 위치", "없음" if data.mark_position is None else str(data.mark_position)
        )
        self.map_data_block.set_value(
            "플레이어 위치",
            "없음" if data.player_position is None else str(data.player_position),
        )
        self.map_data_block.set_value(
            "거리", f"{round(data.distance, 1)}m" if data.distance else "없음"
        )
        minimap_types = ["없음", "소형", "대형"]
        self.map_data_block.set_value("미니맵", minimap_types[data.minimap_type.value])

    def _update_elevation_data_ui(self, data):
        self.elevation_data_block.set_value(
            "마커 위치", "없음" if data.mark_position is None else str(data.mark_position)
        )
        self.elevation_data_block.set_value(
            "고도",
            f"{round(data.elevation, 1)}m" if data.elevation is not None else "없음",
        )
        self.elevation_data_block.set_value(
            "고도 반영 거리",
            f"{round(data.elevated_distance, 1)}m"
            if data.elevated_distance is not None
            else "없음",
        )
        dist_str = (
            f"{data.mortar_elevated_distance} m"
            if isinstance(data.mortar_elevated_distance, int)
            else "없음"
            if data.mortar_elevated_distance is None
            else str(data.mortar_elevated_distance)
        )
        self.elevation_data_block.set_value("박격포 사거리", dist_str)

    def _initialize_overlay(self):
        if self.overlay is not None:
            self.overlay.add_command(Stop())

        settings = self._get_overlay_settings()
        if settings.enabled:
            self.overlay = AppOverlay(self._get_general_settings().app_title)
            self.logic.draw_to_overlay(self.overlay, self._get_overlay_settings())

    def load_map_preview(self):
        image = self.logic.load_image()
        if image is None:
            return

        processed_image, map_data = self.logic.set_map_image(
            image,
            self._get_grid_settings(),
            self._get_mark_settings(),
            self._get_minimap_settings(),
            self._get_dictor_settings(),
            self._get_general_settings(),
            False,
        )
        self.map_image_preview.set_cv2(processed_image)
        self._update_map_data_ui(map_data)
        self.process_elevation_image(False)

    def load_elevation_preview(self):
        image = self.logic.load_image()
        if image is None:
            return

        self.logic.set_elevation_image(
            image,
            self._get_elevation_settings(),
            self._get_mark_settings(),
            self._get_dictor_settings(),
            self._get_general_settings(),
            False,
        )

        processed_image, elevation_data = self.logic.set_elevation_image(
            image,
            self._get_elevation_settings(),
            self._get_mark_settings(),
            self._get_dictor_settings(),
            self._get_general_settings(),
            False,
        )
        self.elevation_image_preview.set_cv2(processed_image)
        self._update_elevation_data_ui(elevation_data)
