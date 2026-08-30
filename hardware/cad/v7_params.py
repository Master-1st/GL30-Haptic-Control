"""V7 Product Edition concept-fit defaults.

All dimensions are millimetres.  OFFICIAL values come from vendor CAD/PDF;
ENGINEERING_DEFAULT values come from the V7 design manual; ASSUMED values must
be revisited after CubeMars replies and after a first physical fit check.
"""

from __future__ import annotations

from dataclasses import dataclass
from math import cos, radians, tan


@dataclass(frozen=True)
class V7Defaults:
    # Product envelope: ENGINEERING_DEFAULT
    width_mm: float = 128.0
    depth_mm: float = 100.0
    front_height_mm: float = 17.0
    rear_height_mm: float = 52.0
    deck_angle_deg: float = 26.0
    housing_wall_mm: float = 3.0
    housing_edge_radius_mm: float = 2.0

    # HMI location and ring: ENGINEERING_DEFAULT / ASSUMED
    knob_center_from_front_mm: float = 45.0
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

    # Secondary controls: ASSUMED.
    button_diameter_mm: float = 8.0
    button_height_mm: float = 2.2
    button_center_from_front_mm: float = 10.0

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
}
