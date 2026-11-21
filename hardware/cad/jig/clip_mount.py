# %%
"""Build a rectangular plate with edge clips and mounting holes. Dimensions are in millimeters."""

from argparse import SUPPRESS, ArgumentParser
from pathlib import Path

from build123d import (
    Align,
    Axis,
    Box,
    Color,
    Cylinder,
    GeomType,
    GridLocations,
    Pos,
    Rot,
    chamfer,
    export_step,
    export_stl,
)

PLATE_LENGTH = 120.0  # Chiều dài tấm theo trục X.
PLATE_WIDTH = 100.0  # Chiều rộng tấm theo trục Y.
PLATE_THICKNESS = 2  # Bề dày tấm theo trục Z.
PLATE_GAP = 3  # Khe từ đầu mỏ kẹp trên đến mặt dưới của tấm.
MOUNT_HOLE_DIAMETER = 3.4  # Đường kính lỗ xuyên cho vít M3.
MOUNT_HOLE_SPACING_X = 110.0  # Khoảng cách tâm lỗ theo X.
MOUNT_HOLE_SPACING_Y = 90.0  # Khoảng cách tâm lỗ theo Y.
INNER_HOLE_DIAMETER = 3.0  # Đường kính bốn lỗ xuyên ở giữa tấm.
INNER_HOLE_SPACING = 45.0  # Cạnh hình vuông tính giữa các tâm lỗ.
MOUNT_HOLE_CHAMFER = 0.3  # Độ vát miệng lỗ ở mặt áp bàn in.

GRIPPED_PART_SIZE = 73.0  # Chiều dài cạnh của vật vuông cần kẹp.
GRIPPED_PART_THICKNESS = 2  # Bề dày vật cần kẹp theo trục Z.
CLIP_WIDTH = 10.0  # Chiều rộng mỗi ngàm, đo dọc theo cạnh vật.
CLIP_WALL = 1  # Bề dày thành đứng của ngàm, đo vuông góc với cạnh vật.
UPPER_JAW_THICKNESS = 1.0  # Bề dày đầu mỏ kẹp trên theo trục Z.
LOWER_JAW_THICKNESS = 2.0  # Bề dày mỏ kẹp dưới tại chân mỏ.
UPPER_JAW_ENGAGEMENT = 1.0  # Khoảng mỏ kẹp trên nhô qua mép vật.
LOWER_JAW_ENGAGEMENT = 0.75  # Khoảng mỏ kẹp dưới nhô qua mép vật.
LOWER_JAW_LAND = 0.45  # Bề rộng mặt kẹp phẳng còn lại tính từ thành ngàm.
JAW_LEAD_IN = 0.75  # Độ vát ở mép dưới phía trong của mỏ kẹp dưới.
EDGE_CLEARANCE = 0.2  # Khe hở ngang giữa mỗi cạnh vật và thành ngàm tương ứng.
FACE_CLEARANCE = 0.2  # Khe hở theo Z giữa mỗi mặt vật và mỏ kẹp tương ứng.


def build_mount():
    bottom_aligned = (Align.CENTER, Align.CENTER, Align.MIN)
    upper_jaw_z = GRIPPED_PART_THICKNESS + FACE_CLEARANCE
    lower_jaw_z = -FACE_CLEARANCE - LOWER_JAW_THICKNESS
    plate_z = upper_jaw_z + UPPER_JAW_THICKNESS + PLATE_GAP

    plate = Pos(0, 0, plate_z) * Box(
        PLATE_LENGTH, PLATE_WIDTH, PLATE_THICKNESS, align=bottom_aligned
    )
    hole_patterns = (
        (MOUNT_HOLE_DIAMETER, MOUNT_HOLE_SPACING_X, MOUNT_HOLE_SPACING_Y),
        (INNER_HOLE_DIAMETER, INNER_HOLE_SPACING, INNER_HOLE_SPACING),
    )
    for diameter, spacing_x, spacing_y in hole_patterns:
        hole = Pos(0, 0, plate_z) * Cylinder(diameter / 2, PLATE_THICKNESS, align=bottom_aligned)
        hole_locations = GridLocations(spacing_x, spacing_y, 2, 2)
        plate = plate.cut(*(location * hole for location in hole_locations.locations))
    hole_entries = plate.edges().filter_by(GeomType.CIRCLE).group_by(Axis.Z)[-1]
    plate = chamfer(hole_entries, length=MOUNT_HOLE_CHAMFER)

    part_edge = GRIPPED_PART_SIZE / 2
    wall_start = part_edge + EDGE_CLEARANCE
    outer_edge = wall_start + CLIP_WALL
    clip_height = plate_z - lower_jaw_z
    wall = Pos((wall_start + outer_edge) / 2, 0, lower_jaw_z) * Box(
        CLIP_WALL, CLIP_WIDTH, clip_height, align=bottom_aligned
    )

    # In với mặt tấm úp xuống bàn. Mặt dốc 45° đỡ toàn bộ phần nhô của mỏ trên.
    upper_tip = part_edge - UPPER_JAW_ENGAGEMENT
    upper_overhang = wall_start - upper_tip
    upper_jaw = Pos((upper_tip + outer_edge) / 2, 0, upper_jaw_z) * Box(
        outer_edge - upper_tip,
        CLIP_WIDTH,
        UPPER_JAW_THICKNESS + upper_overhang,
        align=bottom_aligned,
    )
    upper_print_edge = upper_jaw.edges().filter_by(Axis.Y).group_by(Axis.X)[0].sort_by(Axis.Z)[-1]
    upper_jaw = chamfer(upper_print_edge, length=upper_overhang)

    # Giữ một dải phẳng trên mỏ dưới để móc mép vật và vát phần nhô còn lại.
    lower_tip = part_edge - LOWER_JAW_ENGAGEMENT
    lower_jaw = Pos((lower_tip + outer_edge) / 2, 0, lower_jaw_z) * Box(
        outer_edge - lower_tip, CLIP_WIDTH, LOWER_JAW_THICKNESS, align=bottom_aligned
    )
    lower_inner_edges = lower_jaw.edges().filter_by(Axis.Y).group_by(Axis.X)[0].sort_by(Axis.Z)
    lower_jaw = chamfer(lower_inner_edges[0], length=JAW_LEAD_IN)
    lower_print_edge = lower_jaw.edges().filter_by(Axis.Y).group_by(Axis.X)[0].sort_by(Axis.Z)[-1]
    lower_jaw = chamfer(lower_print_edge, length=wall_start - lower_tip - LOWER_JAW_LAND)
    clip = wall.fuse(lower_jaw, upper_jaw)

    clips = [Rot(0, 0, angle) * clip for angle in range(0, 360, 90)]
    mount = plate.fuse(*clips).clean()
    mount.label = "Plate with four clips"
    mount.color = Color(0.12, 0.51, 0.72)
    return mount


def main(*, export_format=None):
    mount = build_mount()

    if export_format is None:
        from ocp_vscode import find_and_set_port, get_port, port_check, show

        port = get_port()
        if port is None or not port_check(port):
            find_and_set_port()
            port = get_port()
            if port is None or not port_check(port):
                raise SystemExit(
                    "OCP CAD Viewer is not listening. In VS Code, run "
                    "'OCP CAD Viewer: Open viewer' and keep its tab open while running this file."
                )
        show(mount, port=port)
        return

    exports = Path(__file__).resolve().parent / "exports"
    exports.mkdir(exist_ok=True)
    if export_format == "both" and not export_step(mount, exports / "clip_mount.step"):
        raise RuntimeError("Could not export the STEP mount")
    print_mount = Pos(0, 0, mount.bounding_box().max.Z) * Rot(180, 0, 0) * mount
    if not export_stl(print_mount, exports / "clip_mount.stl"):
        raise RuntimeError("Could not export the STL mount")


# %%
if __name__ == "__main__":
    # Run Cell shares IPython's namespace and command-line arguments.
    if "get_ipython" in globals():
        main()
    else:
        parser = ArgumentParser(
            description=__doc__, epilog="Run without arguments to preview in OCP CAD Viewer."
        )
        mode = parser.add_mutually_exclusive_group()
        mode.add_argument(
            "--export",
            dest="export_format",
            nargs="?",
            const="both",
            choices=("both", "stl"),
            help="Export STEP and STL, or pass stl to export STL only",
        )
        mode.add_argument(
            "--preview", dest="export_format", action="store_const", const=None, help=SUPPRESS
        )
        main(export_format=parser.parse_args().export_format)
