"""Build a centered plate with four edge clips. All dimensions are in millimeters."""

from pathlib import Path

from build123d import Align, Axis, Box, Color, Compound, Pos, Rot, chamfer, export_step, export_stl


BASE_SIZE = 73.0
BASE_THICKNESS = 2.0
PLATE_LENGTH = 12.0
PLATE_WIDTH = 10.0
PLATE_THICKNESS = 0.5
PLATE_GAP = 5.0  # Upper gripping jaw to underside of the mounted plate.

CLIP_WIDTH = 10.0
CLIP_WALL = 1.5
JAW_THICKNESS = 1.0
JAW_ENGAGEMENT = 2.0
JAW_LEAD_IN = 0.75
EDGE_CLEARANCE = 0.2
FACE_CLEARANCE = 0.15  # Clearance on each face of the square base.
ARM_THICKNESS = 1.5
ARM_OVERLAP = 0.75


def build_mount():
    bottom_aligned = (Align.CENTER, Align.CENTER, Align.MIN)
    base_edge = BASE_SIZE / 2
    wall_start = base_edge + EDGE_CLEARANCE
    outer_edge = wall_start + CLIP_WALL
    upper_jaw_z = BASE_THICKNESS + FACE_CLEARANCE
    lower_jaw_z = -FACE_CLEARANCE - JAW_THICKNESS
    plate_z = upper_jaw_z + JAW_THICKNESS + PLATE_GAP

    plate = Pos(0, 0, plate_z) * Box(
        PLATE_LENGTH, PLATE_WIDTH, PLATE_THICKNESS, align=bottom_aligned
    )
    wall = Pos((wall_start + outer_edge) / 2, 0, lower_jaw_z) * Box(
        CLIP_WALL, CLIP_WIDTH, plate_z - lower_jaw_z, align=bottom_aligned
    )
    jaw_tip = base_edge - JAW_ENGAGEMENT
    jaw = Pos((jaw_tip + outer_edge) / 2, 0, 0) * Box(
        outer_edge - jaw_tip, CLIP_WIDTH, JAW_THICKNESS, align=bottom_aligned
    )
    entry_edge = jaw.edges().filter_by(Axis.Y).group_by(Axis.X)[0].sort_by(Axis.Z)[0]
    lower_jaw = chamfer(entry_edge, length=JAW_LEAD_IN)
    clip = wall.fuse(Pos(0, 0, lower_jaw_z) * lower_jaw, Pos(0, 0, upper_jaw_z) * jaw)

    supports = []
    for angle, span in ((0, PLATE_LENGTH), (90, PLATE_WIDTH), (180, PLATE_LENGTH), (270, PLATE_WIDTH)):
        arm_start = span / 2 - ARM_OVERLAP
        arm = Pos((arm_start + outer_edge) / 2, 0, plate_z - ARM_THICKNESS) * Box(
            outer_edge - arm_start, CLIP_WIDTH, ARM_THICKNESS, align=bottom_aligned
        )
        supports.append(Rot(0, 0, angle) * clip.fuse(arm))

    mount = plate.fuse(*supports).clean()
    mount.label = "Plate with four clips"
    mount.color = Color(0.12, 0.51, 0.72)
    return mount


def main():
    mount = build_mount()
    base = Box(BASE_SIZE, BASE_SIZE, BASE_THICKNESS, align=(Align.CENTER, Align.CENTER, Align.MIN))
    base.label = "73 mm square reference base"
    base.color = Color(0.68, 0.70, 0.72)
    assembly = Compound(label="Clip mount assembly", children=[base, mount])

    exports = Path(__file__).resolve().parent / "exports"
    exports.mkdir(exist_ok=True)
    if not export_step(assembly, exports / "clip_mount.step"):
        raise RuntimeError("Could not export the STEP assembly")
    if not export_stl(mount, exports / "clip_mount.stl"):
        raise RuntimeError("Could not export the STL mount")


if __name__ == "__main__":
    main()
