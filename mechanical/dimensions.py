"""Millimetres. X right, Y rear, Z up; aluminium chassis underside is Z=0.

Concept parameters, NOT released machining dimensions. Sensor hole pattern is
from the linked manufacturer drawing; display thickness/battery are envelopes.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class Dimensions:
    width: float = 160
    depth: float = 140
    wall: float = 2
    frame_t: float = 4
    foot_h: float = 4
    cell_l: float = 130
    cell_w: float = 30
    cell_h: float = 22
    cell_y: float = 15
    cell_hole_span: float = 106
    cell_hole_pitch: float = 15
    cell_clearance_d: float = 6.6  # M6 clearance in brackets, not sensor thread
    spacer_t: float = 3
    upper_w: float = 134
    upper_d: float = 96
    upper_y: float = 13
    upper_depth: float = 10
    upper_skin: float = 3
    upper_wall: float = 4
    upper_center_rib: float = 8
    upper_pocket_r: float = 3
    upper_m6_counterbore_d: float = 11
    upper_m6_counterbore_depth: float = 6.5
    upper_boss_corner: float = 14
    upper_m3_pilot_depth: float = 8
    upper_m3_thread_depth: float = 6
    plate_t: float = 3
    plate_w: float = 150
    plate_front: float = -40
    plate_rear: float = 65
    panel_depth: float = 28
    stop_gap: float = 0.8  # layout placeholder; calibrate from measured deflection
    pcb_x_right: float = 65
    pcb_y_front: float = -45
    pcb_z: float = 11
    screen_x: float = -38
    screen_y: float = -56
    screen_w: float = 38.5
    screen_d: float = 22
    screen_h: float = 5  # provisional incl. module PCB; connector space separate
    screen_active_w: float = 32.35
    screen_active_d: float = 17.39
    battery_x: float = -50
    battery_y: float = -24
    battery_w: float = 32
    battery_d: float = 38
    battery_h: float = 8
    # v0.3 active assembly: integral pan; upper_* / plate_* retain v0.2 reference.
    pan_w: float = 160
    pan_front: float = -43
    pan_rear: float = 70
    pan_depth: float = 11
    pan_skin: float = 3
    pan_skirt_wall: float = 2
    pan_skirt_h: float = 5
    adapter_w: float = 28
    adapter_d: float = 56
    adapter_t: float = 4
    pan_m4_pilot_depth: float = 7
    pan_m4_thread_depth: float = 6.5
    rim_radial_gap: float = 2
    rim_wall: float = 2
    rim_roof_gap: float = 2.5
    underside_gap: float = 3
    front_film_t: float = .3
    front_adhesive_t: float = .2

    @property
    def cell_z(self):
        return self.frame_t + self.spacer_t

    @property
    def spider_z(self):
        return self.cell_z + self.cell_h + self.spacer_t

    @property
    def plate_z(self):
        return self.spider_z + self.upper_depth

    @property
    def wall_top(self):
        return self.pan_roof_z-self.pan_skirt_h-self.underside_gap

    @property
    def panel_z(self):
        return self.wall_top-self.front_film_t-self.front_adhesive_t-3

    @property
    def screen_z(self):
        return self.panel_z - self.screen_h

    @property
    def plate_mounts(self):
        return [(x, y) for x in (-61, 61) for y in (-29, 55)]

    @property
    def total_height(self):
        return self.foot_h + self.pan_top

    @property
    def pan_z(self):
        return self.cell_z+self.cell_h+self.adapter_t

    @property
    def pan_top(self):
        return self.pan_z+self.pan_depth

    @property
    def pan_roof_z(self):
        return self.pan_top-self.pan_skin

    @property
    def pan_y(self):
        return (self.pan_front+self.pan_rear)/2

    @property
    def pan_plan_depth(self):
        return self.pan_rear-self.pan_front

    @property
    def pan_mounts(self):
        return [(x,y) for x in (47,61) for y in (-7,37)]

    @property
    def rim_outer_w(self):
        return self.pan_w-2*(self.pan_skirt_wall+self.rim_radial_gap)

    @property
    def rim_outer_d(self):
        return self.pan_plan_depth-2*(self.pan_skirt_wall+self.rim_radial_gap)

    @property
    def rim_top(self):
        return self.pan_roof_z-self.rim_roof_gap

    @property
    def feet(self):
        return [(x, y) for x in (-68, 68) for y in (-60, 60)]

    @property
    def stops(self):
        return [(x, y) for x in (-72, 72) for y in (-18, 43)]

    @property
    def tray_posts(self):
        # Outside the PCB. These fasten the removable carrier, not the PCB.
        return [(x, y) for x in (-28, 70) for y in (-47, -6)]

    @property
    def shell_mounts(self):
        return [(x, y) for x in (-73, 73) for y in (-58, 55)]

    def pcb_xy(self, native_x, native_y):
        return self.pcb_x_right - native_x, self.pcb_y_front + native_y


D = Dimensions()
