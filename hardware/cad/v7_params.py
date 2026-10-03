"""V7 wireless Product Edition concept-fit defaults.

All dimensions are millimetres.  OFFICIAL values come from vendor CAD/PDF;
ENGINEERING_DEFAULT values are the current product decisions; ASSUMED values
must be revisited after vendor replies and after a first physical fit check.
"""

from __future__ import annotations

from dataclasses import dataclass
from math import cos, pi, radians, tan


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
    ring_front_normal_mm: float = 13.2
    ring_bearing_start_normal_mm: float = 0.8
    deck_aperture_diameter_mm: float = 52.6

    # Reference-inspired CMF geometry; sample defaults, not machining approval.
    ring_knurl_count: int = 64
    ring_knurl_groove_width_mm: float = 0.55
    ring_knurl_depth_mm: float = 0.20
    ring_knurl_angle_deg: float = 45.0
    ring_knurl_axial_land_mm: float = 0.80
    ring_waist_radius_mm: float = 25.0
    ring_waist_normal_mm: float = 10.4
    ring_crown_radius_mm: float = 25.5
    ring_crown_normal_mm: float = 12.0
    ring_top_radius_mm: float = 24.9
    ring_lower_edge_inset_mm: float = 0.20
    ring_transition_width_mm: float = 1.20
    ring_transition_recess_mm: float = 0.20
    ring_transition_blend_mm: float = 0.30
    ring_marker_length_mm: float = 2.8
    ring_marker_width_mm: float = 1.0
    ring_marker_depth_mm: float = 0.10

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
    bezel_thickness_mm: float = 0.5
    display_cover_thickness_mm: float = 0.5
    support_tube_outer_diameter_mm: float = 5.0
    support_tube_inner_diameter_mm: float = 3.2
    support_tube_back_normal_mm: float = -30.0

    # Official GL30 STEP coordinate has output-side maximum X at +4.5 mm.
    gl30_output_face_local_x_mm: float = 4.5

    # Four low-visibility side/rear keys; electrical switch/travel remains HOLD.
    side_button_length_mm: float = 11.0
    side_button_height_mm: float = 4.0
    side_button_face_recess_mm: float = 0.30
    side_button_clearance_mm: float = 0.20
    side_button_body_depth_mm: float = 2.70
    side_button_flange_margin_mm: float = 0.70
    side_button_flange_thickness_mm: float = 0.60
    side_button_front_y_mm: float = 20.0
    side_button_rear_y_mm: float = 35.0
    side_button_z_mm: float = 43.0
    side_button_pocket_length_mm: float = 33.0
    side_button_pocket_height_mm: float = 6.6
    side_button_pocket_depth_mm: float = 0.60
    side_button_clearance_check_travel_mm: float = 0.40
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
    def display_cover_front_normal_mm(self) -> float:
        return (
            self.bezel_start_normal_mm
            + self.bezel_thickness_mm
            + self.display_cover_thickness_mm
        )

    @property
    def ring_knurl_root_wall_mm(self) -> float:
        return (
            (self.knob_outer_diameter_mm - self.bearing_outer_diameter_mm) / 2.0
            - self.ring_shell_radial_clearance_mm
            - self.ring_knurl_depth_mm
        )

    @property
    def ring_marker_radius_mm(self) -> float:
        return (
            self.ring_top_radius_mm
            + self.knob_inner_diameter_mm / 2.0
            + self.ring_transition_width_mm + self.ring_transition_blend_mm
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
        if self.ring_knurl_root_wall_mm < 0.65:
            raise ValueError("Cosmetic knurl leaves less than 0.65 mm of sleeve wall")
        radius = self.knob_outer_diameter_mm / 2.0
        if self.ring_knurl_count < 8 or not (
            0.0 < self.ring_knurl_groove_width_mm < pi * radius / self.ring_knurl_count
        ):
            raise ValueError("Knurl width/count must leave broad, flat diamond lands")
        if not 0.0 < self.ring_knurl_depth_mm <= 0.20:
            raise ValueError("Knurl depth is capped at 0.20 mm for the existing sleeve")
        if not 20.0 <= self.ring_knurl_angle_deg <= 60.0:
            raise ValueError("Knurl angle must remain within the shallow-grip design range")
        bearing_front = self.ring_bearing_start_normal_mm + self.bearing_width_mm
        if not bearing_front < self.ring_waist_normal_mm < self.ring_crown_normal_mm < self.ring_front_normal_mm:
            raise ValueError("Sculpted shoulder must remain entirely above the bearing")
        if not self.ring_top_radius_mm < self.ring_waist_radius_mm < self.ring_crown_radius_mm < radius:
            raise ValueError("Waist and crown must stay within the existing outer radius")
        if not 0.0 < self.ring_lower_edge_inset_mm <= self.ring_knurl_depth_mm:
            raise ValueError("Lower edge must retain at least the groove-root sleeve wall")
        if not 0.0 < self.ring_knurl_axial_land_mm < self.bearing_width_mm / 2.0:
            raise ValueError("Grip band needs positive height between smooth sleeve lands")
        if not (
            0.0 < self.ring_transition_recess_mm < self.ring_transition_blend_mm
            < self.ring_transition_width_mm
        ):
            raise ValueError("Transition shoulder needs a shallow recess and positive blends")
        flat_ring_width = (
            self.ring_top_radius_mm - self.knob_inner_diameter_mm / 2.0
            - self.ring_transition_width_mm - self.ring_transition_blend_mm
        )
        if not 0.0 < self.ring_marker_width_mm < self.ring_marker_length_mm < flat_ring_width:
            raise ValueError("The index mark must fit on the flat annular top")
        if not 0.0 < self.ring_marker_depth_mm < self.ring_transition_recess_mm:
            raise ValueError("The index mark must remain a shallow filled engraving")
        if min(self.bezel_thickness_mm, self.display_cover_thickness_mm) <= 0.0:
            raise ValueError("The fixed cover and its backing must have positive thickness")
        if abs(self.display_cover_front_normal_mm - self.ring_front_normal_mm) > 1.0e-6:
            raise ValueError("The fixed cover and rotating ring top must be nominally flush")
        if abs(self.side_button_body_depth_mm + self.side_button_face_recess_mm - self.housing_wall_mm) > 1.0e-6:
            raise ValueError("The key flange must meet the inner wall at its outward stop")
        if not 0.0 < self.side_button_clearance_mm < self.side_button_flange_margin_mm:
            raise ValueError("Retaining flange must be larger than the key guide opening")
        if not 0.0 < self.side_button_face_recess_mm < self.side_button_pocket_depth_mm < self.housing_wall_mm:
            raise ValueError("Key face must sit below the side wall but above the pocket floor")
        pocket_center = (self.side_button_front_y_mm + self.side_button_rear_y_mm) / 2.0
        if pocket_center + self.side_button_pocket_length_mm / 2.0 > self.depth_mm / 2.0 - self.housing_edge_radius_mm:
            raise ValueError("Hidden-key pocket clips the rear corner")
        if self.side_button_z_mm + self.side_button_pocket_height_mm / 2.0 > self.rear_height_mm - self.housing_wall_mm:
            raise ValueError("Hidden-key pocket is too close to the rear roof")
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
    "ring_knurl_count": "ENGINEERING_DEFAULT_USER_SELECTED_BENTLEY_REFERENCE",
    "ring_knurl_groove_width_mm": "ENGINEERING_DEFAULT_CMF_SAMPLE_NOT_MACHINING_RELEASE",
    "ring_knurl_depth_mm": "ENGINEERING_DEFAULT_TOTAL_DEPTH_LIMITED_BY_EXISTING_SLEEVE_WALL",
    "ring_knurl_angle_deg": "ENGINEERING_DEFAULT_CROSSED_SHALLOW_GRIP_NOT_MACHINING_RELEASE",
    "ring_knurl_axial_land_mm": "ENGINEERING_DEFAULT_SMOOTH_CONTACT_EDGES",
    "ring_waist_radius_mm": "USER_REQUESTED_SCULPTED_SIDE_PROFILE_ABOVE_BEARING",
    "ring_waist_normal_mm": "ENGINEERING_DEFAULT_INTERNAL_STACK_UNCHANGED",
    "ring_crown_radius_mm": "ENGINEERING_DEFAULT_SUBTLE_ROLLED_UPPER_SHOULDER",
    "ring_crown_normal_mm": "ENGINEERING_DEFAULT_INTERNAL_STACK_UNCHANGED",
    "ring_top_radius_mm": "ENGINEERING_DEFAULT_NARROWER_FLAT_BLACK_TOP",
    "ring_lower_edge_inset_mm": "ENGINEERING_DEFAULT_SMOOTH_EDGE_WITH_ROOT_WALL_PRESERVED",
    "ring_transition_width_mm": "USER_REQUESTED_TRANSITION_RING_INTEGRAL_WITH_ROTATING_CAP",
    "ring_transition_recess_mm": "ENGINEERING_DEFAULT_SHALLOW_BLACK_TRANSITION_SHOULDER",
    "ring_transition_blend_mm": "ENGINEERING_DEFAULT_NO_OVERHANG_ACROSS_MOVING_GAP",
    "ring_marker_length_mm": "ENGINEERING_DEFAULT_BLUE_FILLED_INDEX_MARK",
    "ring_marker_width_mm": "ENGINEERING_DEFAULT_BLUE_FILLED_INDEX_MARK",
    "ring_marker_depth_mm": "ASSUMED_PAINT_FILL_SAMPLE_NOT_MACHINING_RELEASE",
    "ring_front_normal_mm": "ENGINEERING_DEFAULT_FLUSH_WITH_FIXED_BLACK_COVER",
    "bezel_thickness_mm": "ASSUMED_FIXED_COVER_BACKING_NOT_PRODUCTION_RELEASE",
    "display_cover_thickness_mm": "ASSUMED_DEAD_FRONT_OPTICAL_SAMPLE_REQUIRED",
    "fixed_bezel_outer_diameter_mm": "ASSUMED",
    "bearing_name": "ASSUMED_PENDING_LOAD_DATA",
    "bearing_inner_diameter_mm": "STANDARD_ENVELOPE_ASSUMED",
    "bearing_outer_diameter_mm": "STANDARD_ENVELOPE_ASSUMED",
    "bearing_width_mm": "STANDARD_ENVELOPE_ASSUMED",
    "motor_output_face_normal_mm": "ASSUMED_PENDING_FACE_ROLE",
    "display_origin_normal_mm": "ASSUMED_FROM_OFFICIAL_STEP_ENVELOPE",
    "support_tube_outer_diameter_mm": "ASSUMED_PENDING_BORE_PERMISSION",
    "side_button_length_mm": "USER_REQUESTED_DISCREET_SIDE_REAR_KEYS",
    "side_button_height_mm": "ENGINEERING_DEFAULT_FINGER_REACH_REQUIRES_MOCKUP",
    "side_button_face_recess_mm": "ENGINEERING_DEFAULT_BELOW_WALL_ABOVE_RECESSED_POCKET",
    "side_button_clearance_mm": "PROTOTYPE_GUIDE_GAP_FINISHED_DIMENSIONS",
    "side_button_body_depth_mm": "ENGINEERING_DEFAULT_FLANGE_AT_INNER_WALL",
    "side_button_flange_margin_mm": "PROTOTYPE_OUTWARD_RETENTION_ONLY",
    "side_button_flange_thickness_mm": "PROTOTYPE_OUTWARD_RETENTION_ONLY",
    "side_button_front_y_mm": "ENGINEERING_DEFAULT_SIDE_REAR_ZONE",
    "side_button_rear_y_mm": "ENGINEERING_DEFAULT_SIDE_REAR_ZONE",
    "side_button_z_mm": "ENGINEERING_DEFAULT_ABOVE_BATTERY_AND_ELECTRONICS_KEEP_OUTS",
    "side_button_pocket_length_mm": "ENGINEERING_DEFAULT_LOW_VISIBILITY_RECESSED_KEY_BAND",
    "side_button_pocket_height_mm": "ENGINEERING_DEFAULT_LOW_VISIBILITY_RECESSED_KEY_BAND",
    "side_button_pocket_depth_mm": "ENGINEERING_DEFAULT_LOW_VISIBILITY_RECESSED_KEY_BAND",
    "side_button_clearance_check_travel_mm": "CAD_CLEARANCE_TEST_ONLY_NOT_SWITCH_WORKING_TRAVEL",
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
