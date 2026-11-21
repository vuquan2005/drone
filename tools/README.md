# Công cụ hỗ trợ

Chạy các lệnh từ gốc repository; `make help` liệt kê đầy đủ.

| Lệnh                                        | Tác dụng                                                          |
| ------------------------------------------- | ----------------------------------------------------------------- |
| `make telemetry`                            | Mở viewer RTT và ghi phiên dữ liệu                                |
| `make format` / `make lint`                 | Format / lint các tệp Git theo dõi                                |
| `make sync-papers` / `make sync-datasheets` | Trích xuất PDF sang Markdown và ảnh                               |
| `make report`                               | Build PDF; xem [hướng dẫn báo cáo](../report/AGENTS.md#biên-dịch) |
| `make clean`                                | Xóa build firmware/báo cáo, gồm PDF đã build                      |

## Cài đặt

Cần Make, Git, Python 3.10+ và [uv](https://docs.astral.sh/uv/):

```sh
uv sync --locked
uv run pre-commit install --install-hooks
```

Dependency quản lý tại `pyproject.toml` và `uv.lock`; môi trường nằm trong
`.venv/`. Nhóm `dev` cài mặc định, nhóm GUI `rtt` chọn bằng `--group rtt`,
nhóm `cad` chọn bằng `--group cad` và dùng Python 3.11–3.14.
Khi đổi dependency, chạy `uv lock` và xem diff; giữ lockfile trong Git.
Lần đầu cần mạng để tải môi trường/công cụ.

| Phạm vi        | Phụ thuộc                                                         |
| -------------- | ----------------------------------------------------------------- |
| Trích xuất PDF | Poppler: `pdftotext`, `pdfimages`                                 |
| Format/lint    | pre-commit; LaTeX cần `latexindent`, `chktex` từ TeX distribution |

Chạy bằng `uv run ...`, hoặc `uv run make lint` / `uv run make format`.
Các lệnh tổng hợp chỉ xử lý tệp Git theo dõi. Khi commit, nếu formatter sửa
file thì xem diff, stage lại rồi commit; dùng `make lint` để kiểm tra riêng.

## RTT

Cấu hình debug **Drone: OpenOCD + RTT** tự mở viewer để xem IMU, 16 kênh RC
và log. Có thể mở thủ công khi OpenOCD đang chạy:

```sh
make telemetry
```

Xem [hướng dẫn RTT](rtt/README.md) để cấu hình cổng, ghi CSV/log/raw,
đọc timing và chạy kiểm thử.

## CAD

Mở extension OCP CAD Viewer trong VS Code. Để chạy từng ô `# %%` trong
`ocp_vscode_demo.py`, dùng extension Jupyter và chọn kernel từ `.venv/`.
Cũng có thể chạy toàn bộ demo từ gốc repo.

```sh
uv sync --locked --group cad
uv run --locked --group cad ocp_vscode_demo.py
```

Dùng `uv sync --locked --group cad --group rtt` để cài cả CAD và viewer RTT.

## Đồng bộ PDF

```sh
uv run tools/sync_reference_pdfs.py --dry-run
make sync-papers
uv run tools/sync_reference_pdfs.py --collection datasheets --dry-run
make sync-datasheets
# Trích xuất riêng để đối chiếu
uv run tools/sync_reference_pdfs.py docs/references/papers/huy2024-262.pdf --output-dir /tmp/papers_md
```

Đầu ra mặc định: `docs/references/papers/papers_md/` và
`docs/references/datasheets/datasheets_md/`. Script bảo vệ đầu ra sửa thủ công
và thư mục có nội dung nhưng không có manifest. Manifest chỉ lưu cục bộ;
sau clone, dùng `--output-dir` khác để tạo bản đối chiếu.

Ảnh trích xuất và manifest bị Git bỏ qua nên có thể thiếu sau khi clone.
Lệnh đồng bộ giữ nguyên thư mục không có manifest (`KEEP`), kể cả khi thiếu
ảnh; lệnh này không xác nhận bản trích xuất khớp PDF hiện tại. Khi cần ảnh,
trích xuất vào thư mục riêng bằng `--output-dir` và mở bản Markdown ở đó.
Tên ảnh của bản trích xuất mới có thể khác bản cũ, nên không chép ảnh sang
bản biên tập thủ công chỉ dựa vào số thứ tự.

Kiểm tra lại PDF gốc: file thiếu lớp văn bản cần OCR; sơ đồ vector, công
thức và bảng có thể trích xuất không đầy đủ. Xem tùy chọn bằng `--help`.
