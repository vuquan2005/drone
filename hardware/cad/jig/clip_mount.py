# %%
"""Build a rectangular plate with four edge clips. All dimensions are in millimeters."""

from argparse import SUPPRESS, ArgumentParser
from pathlib import Path

from build123d import Align, Axis, Box, Color, Pos, Rot, chamfer, export_step, export_stl

PLATE_LENGTH = 120.0  # Chiều dài tấm theo trục X.
PLATE_WIDTH = 100.0  # Chiều rộng tấm theo trục Y.
PLATE_THICKNESS = 2  # Bề dày tấm theo trục Z.
PLATE_GAP = 3  # Khe từ mặt trên của mỏ kẹp trên đến mặt dưới của tấm.

GRIPPED_PART_SIZE = 73.0  # Chiều dài cạnh của vật vuông cần kẹp.
GRIPPED_PART_THICKNESS = 2  # Bề dày vật cần kẹp theo trục Z.
CLIP_WIDTH = 10.0  # Chiều rộng mỗi ngàm, đo dọc theo cạnh vật.
CLIP_WALL = 1  # Bề dày thành đứng của ngàm, đo vuông góc với cạnh vật.
JAW_THICKNESS = 1.0  # Bề dày của từng mỏ kẹp trên và dưới theo trục Z.
JAW_ENGAGEMENT = 1  # Khoảng mỗi mỏ kẹp nhô vào phía trong mép vật.
JAW_LEAD_IN = 0.75  # Độ vát ở mép dưới phía trong của mỏ kẹp dưới.
EDGE_CLEARANCE = 0.2  # Khe hở ngang giữa mỗi cạnh vật và thành ngàm tương ứng.
FACE_CLEARANCE = 0.05  # Khe hở theo Z giữa mỗi mặt vật và mỏ kẹp tương ứng.


def build_mount():
    bottom_aligned = (Align.CENTER, Align.CENTER, Align.MIN)
    upper_jaw_z = GRIPPED_PART_THICKNESS + FACE_CLEARANCE
    lower_jaw_z = -FACE_CLEARANCE - JAW_THICKNESS
    plate_z = upper_jaw_z + JAW_THICKNESS + PLATE_GAP

    plate = Pos(0, 0, plate_z) * Box(
        PLATE_LENGTH, PLATE_WIDTH, PLATE_THICKNESS, align=bottom_aligned
    )

    part_edge = GRIPPED_PART_SIZE / 2
    wall_start = part_edge + EDGE_CLEARANCE
    outer_edge = wall_start + CLIP_WALL
    clip_height = plate_z - lower_jaw_z
    wall = Pos((wall_start + outer_edge) / 2, 0, lower_jaw_z) * Box(
        CLIP_WALL, CLIP_WIDTH, clip_height, align=bottom_aligned
    )
    jaw_tip = part_edge - JAW_ENGAGEMENT
    jaw = Pos((jaw_tip + outer_edge) / 2, 0, 0) * Box(
        outer_edge - jaw_tip, CLIP_WIDTH, JAW_THICKNESS, align=bottom_aligned
    )
    entry_edge = jaw.edges().filter_by(Axis.Y).group_by(Axis.X)[0].sort_by(Axis.Z)[0]
    lower_jaw = chamfer(entry_edge, length=JAW_LEAD_IN)
    clip = wall.fuse(Pos(0, 0, lower_jaw_z) * lower_jaw, Pos(0, 0, upper_jaw_z) * jaw)

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
    if not export_stl(mount, exports / "clip_mount.stl"):
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
