"""Integration checks with real Poppler and a small, self-contained PDF fixture."""

import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "sync_papers.py"
SPEC = importlib.util.spec_from_file_location("sync_papers", SCRIPT)
sync = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sync)


def write_pdf(path, text="First version", *, image=True):
    resources = b"/Font << /F1 4 0 R >>"
    content = f"BT /F1 12 Tf 20 250 Td ({text}) Tj ET\n".encode("ascii")
    if image:
        resources += b" /XObject << /Im0 6 0 R >>"
        content += b"q 20 0 0 20 20 20 cm /Im0 Do Q\n"
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] "
        b"/Resources << " + resources + b" >> /Contents 5 0 R >>",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        f"<< /Length {len(content)} >>\nstream\n".encode() + content + b"endstream",
    ]
    if image:
        objects.append(
            b"<< /Type /XObject /Subtype /Image /Width 2 /Height 2 "
            b"/ColorSpace /DeviceRGB /BitsPerComponent 8 /Length 12 >>\nstream\n"
            + bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255])
            + b"\nendstream"
        )
    data = bytearray(b"%PDF-1.4\n")
    offsets = [0]
    for number, obj in enumerate(objects, 1):
        offsets.append(len(data))
        data.extend(f"{number} 0 obj\n".encode() + obj + b"\nendobj\n")
    xref = len(data)
    data.extend(f"xref\n0 {len(offsets)}\n0000000000 65535 f \n".encode())
    for offset in offsets[1:]:
        data.extend(f"{offset:010d} 00000 n \n".encode())
    data.extend(
        f"trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
    )
    path.write_bytes(data)


@unittest.skipUnless(shutil.which("pdftotext") and shutil.which("pdfimages"), "Poppler required")
class SyncPapersTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.pdf = self.root / "paper with spaces.pdf"
        self.output = self.root / "papers_md"
        self.destination = self.output / self.pdf.stem
        self.md = self.destination / f"{self.pdf.stem}.md"
        write_pdf(self.pdf)

    def run_sync(self, **kwargs):
        return sync.sync_paper(self.pdf, self.output, **kwargs)[0]

    def test_dry_run_writes_nothing(self):
        self.assertEqual(self.run_sync(dry_run=True), "CREATE")
        self.assertFalse(self.output.exists())

    def test_create_images_and_skip_unchanged(self):
        self.assertEqual(self.run_sync(), "CREATE")
        text = self.md.read_text()
        self.assertIn("First version", text)
        self.assertIn("## Trang 1", text)
        self.assertIn("paper%20with%20spaces.pdf", text)
        self.assertIn("img-000.png", text)
        self.assertTrue((self.destination / "img-000.png").is_file())
        modified = self.md.stat().st_mtime_ns
        self.assertEqual(self.run_sync(), "UNCHANGED")
        self.assertEqual(self.md.stat().st_mtime_ns, modified)

    def test_source_change_removes_stale_owned_images(self):
        self.run_sync()
        extra = self.destination / "notes.txt"
        extra.write_text("Keep my notes")
        write_pdf(self.pdf, "Second version", image=False)
        self.assertEqual(self.run_sync(), "UPDATE")
        self.assertIn("Second version", self.md.read_text())
        self.assertFalse((self.destination / "img-000.png").exists())
        self.assertEqual(extra.read_text(), "Keep my notes")

    def test_restore_missing_image_and_change_text_mode(self):
        self.run_sync()
        (self.destination / "img-000.png").unlink()
        self.assertEqual(self.run_sync(), "UPDATE")
        self.assertTrue((self.destination / "img-000.png").is_file())
        self.assertEqual(self.run_sync(text_mode="reading"), "UPDATE")
        self.assertEqual(self.run_sync(text_mode="reading"), "UNCHANGED")

    def test_manual_output_is_protected_even_with_force(self):
        self.run_sync()
        self.md.write_text("Manual corrections")
        with self.assertRaisesRegex(sync.SyncError, "Preserving edited output"):
            self.run_sync(force=True)
        self.assertEqual(self.md.read_text(), "Manual corrections")

    def test_existing_handwritten_folder_is_kept(self):
        self.destination.mkdir(parents=True)
        self.md.write_text("Handwritten paper summary")
        self.assertEqual(self.run_sync(force=True), "KEEP")
        self.assertEqual(self.md.read_text(), "Handwritten paper summary")
        self.assertFalse((self.destination / sync.MANIFEST).exists())

    def test_failed_extraction_keeps_previous_outputs(self):
        self.run_sync()
        before = {p.name: p.read_bytes() for p in self.destination.iterdir()}
        self.pdf.write_bytes(b"invalid PDF")
        with self.assertRaises(sync.SyncError):
            self.run_sync()
        after = {p.name: p.read_bytes() for p in self.destination.iterdir()}
        self.assertEqual(before, after)


if __name__ == "__main__":
    unittest.main()
