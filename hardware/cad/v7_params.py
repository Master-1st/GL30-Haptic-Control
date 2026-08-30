"""V7 wireless Product Edition concept-fit defaults.

All dimensions are millimetres.  OFFICIAL values come from vendor CAD/PDF;
ENGINEERING_DEFAULT values are the current product decisions; ASSUMED values
must be revisited after vendor replies and after a first physical fit check.
"""

from __future__ import annotations

from dataclasses import dataclass
from math import cos, radians, tan


@dataclass(frozen=True)
class V7Defaults:
    # Product envelope: ENGINEERING_DEFAULT
    width_mm: float = 96.0
    depth_mm: float = 96.0
    front_height_mm: float = 20.0
    rear_height_mm: float = 52.0
    deck_angle_deg: float = 26.0
    housing_wall_mm: float = 3.0
    housing_edge_radius_mm: float = 2.0

    # HMI location and ring: ENGINEERING_DEFAULT / ASSUMED
    knob_center_from_front_mm: float = 39.0
    knob_outer_diameter_mm: float = 54.0
    knob_inner_diameter_mm: float = 40.0
    fixed_bezel_outer_diameter_mm: float = 39.0
    fixed_bezel_aperture_diameter_mm: float = 33.8
    ring_front_normal_mm: float = 12.5
    ring_bearing_start_normal_mm: float = 0.8
    deck_aperture_diameter_mm: float = 52.6

    # Independent ring support: ASSUMED, pending load data from CubeMars.
    bearing_name: str = "NSK 6808 dimensional envelope; bearing not BOM-frozen"
    bearing_inner_diameter_mm: float = 40.0
    bearing_outer_diameter_mm: float = 52.0
    bearing_width_mm: float = 7.0
    ring_shell_radial_clearance_mm: float = 0.10

    # Axial stack measured normal to the inclined deck: ASSUMED.
    motor_output_face_normal_mm: float = -1.3
    torque_carrier_thickness_mm: float = 1.3
    fixed_spider_start_normal_mm: float = 0.2
    fixed_spider_thickness_mm: float = 0.6
    display_origin_normal_mm: float = 2.3
    bezel_start_normal_mm: float = 12.2
    bezel_thickness_mm: float = 1.0
    support_tube_outer_diameter_mm: float = 5.0
    support_tube_inner_diameter_mm: float = 3.2
    support_tube_back_normal_mm: float = -30.0

    # Official GL30 STEP coordinate has output-side maximum X at +4.5 mm.
    gl30_output_face_local_x_mm: float = 4.5

    # Four retained user keys move to the side walls.  Power is a separate,
    # recessed rear QON key; BOOT/RESET remains a bottom service pinhole.
    side_button_diameter_mm: float = 6.0
    side_button_protrusion_mm: float = 1.2
    side_button_front_y_mm: float = -13.0
    side_button_rear_y_mm: float = 7.0
    side_button_z_mm: float = 28.0
    power_button_diameter_mm: float = 7.0
    power_button_protrusion_mm: float = 0.8
    power_button_recess_diameter_mm: float = 9.0
    power_button_face_recess_mm: float = 0.3
    power_button_x_mm: float = 30.0
    power_button_z_mm: float = 29.0
    service_pinhole_diameter_mm: float = 2.0
    rear_usb_c_center_z_mm: float = 31.0
    front_light_strip_width_mm: float = 64.0
    front_light_strip_height_mm: float = 2.4
    front_light_strip_depth_mm: float = 0.8
    front_light_strip_center_z_mm: float = 7.5
    front_light_strip_face_recess_mm: float = 0.1
    front_light_strip_pocket_width_mm: float = 66.0
    front_light_strip_pocket_height_mm: float = 3.4
    front_light_strip_pocket_depth_mm: float = 1.2

    # 3S wireless power keep-outs.  The battery envelope starts from a
    # published 80 x 20 x 16 mm 3S/800 mAh pack and adds clearance on every
    # face.  It is not a frozen cell, BMS, holder or production pack.
    battery_keepout_width_mm: float = 84.0
    battery_keepout_depth_mm: float = 24.0
    battery_keepout_height_mm: float = 20.0
    battery_keepout_center_y_mm: float = 33.0
    battery_keepout_floor_gap_mm: float = 1.0
    electronics_keepout_width_mm: float = 84.0
    electronics_keepout_depth_mm: float = 24.0
    electronics_keepout_height_mm: float = 8.0
    electronics_keepout_center_y_mm: float = 33.0
    electronics_keepout_bottom_z_mm: float = 27.0

    @property
    def deck_angle_rad(self) -> float:
        return radians(self.deck_angle_deg)

    @property
    def active_deck_run_mm(self) -> float:
        return (self.rear_height_mm - self.front_height_mm) / tan(
            self.deck_angle_rad
        )

    @property
    def rear_platform_run_mm(self) -> float:
        return self.depth_mm - self.active_deck_run_mm

    @property
    def deck_break_y_mm(self) -> float:
        return -self.depth_mm / 2.0 + self.active_deck_run_mm

    @property
    def knob_center_y_mm(self) -> float:
        return -self.depth_mm / 2.0 + self.knob_center_from_front_mm

    @property
    def knob_center_z_mm(self) -> float:
        return self.front_height_mm + self.knob_center_from_front_mm * tan(
            self.deck_angle_rad
        )

    @property
    def motor_origin_normal_mm(self) -> float:
        return self.motor_output_face_normal_mm - self.gl30_output_face_local_x_mm

    @property
    def battery_keepout_center_z_mm(self) -> float:
        return (
            self.housing_wall_mm
            + self.battery_keepout_floor_gap_mm
            + self.battery_keepout_height_mm / 2.0
        )

    @property
    def electronics_keepout_center_z_mm(self) -> float:
        return (
            self.electronics_keepout_bottom_z_mm
            + self.electronics_keepout_height_mm / 2.0
        )

    @property
    def moving_radial_gap_mm(self) -> float:
        return (
            self.knob_inner_diameter_mm - self.fixed_bezel_outer_diameter_mm
        ) / 2.0

    @property
    def ring_rear_margin_mm(self) -> float:
        projected_radius = (
            self.knob_outer_diameter_mm / 2.0 * cos(self.deck_angle_rad)
        )
        return self.active_deck_run_mm - (
            self.knob_center_from_front_mm + projected_radius
        )

    def validate(self) -> None:
        if not 0.0 < self.active_deck_run_mm < self.depth_mm:
            raise ValueError("The 26-degree active deck does not fit the body depth")
        if self.rear_platform_run_mm <= 0.0:
            raise ValueError("A rear electronics platform is required")
        if self.moving_radial_gap_mm < 0.5:
            raise ValueError("Moving radial gap must be at least 0.50 mm")
        if self.bearing_inner_diameter_mm < self.knob_inner_diameter_mm:
            raise ValueError("Bearing bore clips the fixed display opening")
        if self.knob_outer_diameter_mm <= self.bearing_outer_diameter_mm:
            raise ValueError("Ring shell needs positive radial material around bearing")
        if self.ring_rear_margin_mm < 2.0:
            raise ValueError("Ring is too close to the active-deck/rear-bay breakline")
        inner_width = self.width_mm - 2.0 * self.housing_wall_mm
        inner_rear_y = self.depth_mm / 2.0 - self.housing_wall_mm
        if self.battery_keepout_width_mm > inner_width:
            raise ValueError("Battery keep-out exceeds the inner housing width")
        if (
            self.battery_keepout_center_y_mm
            + self.battery_keepout_depth_mm / 2.0
            > inner_rear_y
        ):
            raise ValueError("Battery keep-out exceeds the inner rear wall")
        if (
            self.battery_keepout_center_y_mm
            - self.battery_keepout_depth_mm / 2.0
            < self.deck_break_y_mm
        ):
            raise ValueError("Battery keep-out leaves the rear electronics bay")
        if self.electronics_keepout_width_mm > inner_width:
            raise ValueError("Electronics keep-out exceeds the inner housing width")
        if (
            self.electronics_keepout_center_y_mm
            + self.electronics_keepout_depth_mm / 2.0
            > inner_rear_y
        ):
            raise ValueError("Electronics keep-out exceeds the inner rear wall")
        battery_top = (
            self.battery_keepout_center_z_mm
            + self.battery_keepout_height_mm / 2.0
        )
        if self.electronics_keepout_bottom_z_mm - battery_top < 2.0:
            raise ValueError("Battery-to-electronics keep-out must be at least 2 mm")
        if (
            self.electronics_keepout_center_z_mm
            + self.electronics_keepout_height_mm / 2.0
            > self.rear_height_mm - self.housing_wall_mm
        ):
            raise ValueError("Electronics keep-out exceeds the inner roof")
        if self.front_light_strip_pocket_width_mm >= self.width_mm:
            raise ValueError("Front light-strip pocket exceeds the front width")
        if self.front_light_strip_width_mm >= self.front_light_strip_pocket_width_mm:
            raise ValueError("Front light-strip window needs a wider housing pocket")
        if (
            self.front_light_strip_center_z_mm
            - self.front_light_strip_pocket_height_mm / 2.0
            <= self.housing_wall_mm
        ):
            raise ValueError("Front light strip is too close to the housing floor")
        if (
            self.front_light_strip_center_z_mm
            + self.front_light_strip_pocket_height_mm / 2.0
            >= self.front_height_mm - self.housing_wall_mm
        ):
            raise ValueError("Front light strip is too close to the sloped deck")


DEFAULTS = V7Defaults()


PARAMETER_PROVENANCE = {
    "width_mm": "ENGINEERING_DEFAULT",
    "depth_mm": "ENGINEERING_DEFAULT",
    "front_height_mm": "ENGINEERING_DEFAULT",
    "rear_height_mm": "ENGINEERING_DEFAULT",
    "deck_angle_deg": "ENGINEERING_DEFAULT",
    "knob_center_from_front_mm": "ASSUMED",
    "knob_outer_diameter_mm": "ENGINEERING_DEFAULT_RANGE_MAX",
    "knob_inner_diameter_mm": "ASSUMED",
    "fixed_bezel_outer_diameter_mm": "ASSUMED",
    "bearing_name": "ASSUMED_PENDING_LOAD_DATA",
    "bearing_inner_diameter_mm": "STANDARD_ENVELOPE_ASSUMED",
    "bearing_outer_diameter_mm": "STANDARD_ENVELOPE_ASSUMED",
    "bearing_width_mm": "STANDARD_ENVELOPE_ASSUMED",
    "motor_output_face_normal_mm": "ASSUMED_PENDING_FACE_ROLE",
    "display_origin_normal_mm": "ASSUMED_FROM_OFFICIAL_STEP_ENVELOPE",
    "support_tube_outer_diameter_mm": "ASSUMED_PENDING_BORE_PERMISSION",
    "side_button_diameter_mm": "ENGINEERING_DEFAULT_RETAIN_FOUR_KEYS",
    "power_button_diameter_mm": "ENGINEERING_DEFAULT_RECESSED_QON",
    "front_light_strip_width_mm": "ENGINEERING_DEFAULT_RETAIN_FRONT_LIGHT_STRIP",
    "front_light_strip_height_mm": "ENGINEERING_DEFAULT_RETAIN_FRONT_LIGHT_STRIP",
    "front_light_strip_face_recess_mm": "ENGINEERING_DEFAULT_FLUSH_DIFFUSER",
    "battery_keepout_width_mm": "PUBLISHED_3S_800MAH_ENVELOPE_PLUS_CLEARANCE",
    "battery_keepout_depth_mm": "PUBLISHED_3S_800MAH_ENVELOPE_PLUS_CLEARANCE",
    "battery_keepout_height_mm": "PUBLISHED_3S_800MAH_ENVELOPE_PLUS_CLEARANCE",
    "electronics_keepout_width_mm": "ENGINEERING_KEEP_OUT_NOT_PCB_FROZEN",
    "electronics_keepout_depth_mm": "ENGINEERING_KEEP_OUT_NOT_PCB_FROZEN",
    "electronics_keepout_height_mm": "ENGINEERING_KEEP_OUT_NOT_PCB_FROZEN",
    "electronics_keepout_center_y_mm": "ENGINEERING_KEEP_OUT_NOT_PCB_FROZEN",
}
