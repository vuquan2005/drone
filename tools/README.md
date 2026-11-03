# Công cụ hỗ trợ

Chạy các lệnh sau từ thư mục gốc repository:

| Lệnh                   | Tác dụng                                                          |
| ---------------------- | ----------------------------------------------------------------- |
| `make help`            | Xem danh sách lệnh                                                |
| `make report`          | Build PDF báo cáo; xem [hướng dẫn](../report/README.md#biên-dịch) |
| `make format`          | Định dạng các tệp được Git theo dõi                               |
| `make lint`            | Kiểm tra Markdown, CMake và LaTeX                                 |
| `make sync-papers`     | Đồng bộ PDF bài báo sang Markdown                                 |
| `make sync-datasheets` | Đồng bộ PDF datasheet sang Markdown                               |
| `make clean`           | Xóa thư mục build của firmware và báo cáo, gồm cả PDF đã build    |

## Cài đặt

Cần Make, Git, Python và [uv](https://docs.astral.sh/uv/). Cài pre-commit và hook cho mỗi bản clone:

```sh
uv tool install pre-commit
pre-commit install --install-hooks
```

Lần đầu cần mạng để tải môi trường và công cụ. Riêng `latexindent` và ChkTeX cần được cài cùng TeX distribution. Các lệnh đồng bộ PDF cần thêm Poppler (`pdftotext` và `pdfimages`; gói `poppler-utils` trên Ubuntu/Debian và Fedora).

`make format` và `make lint` xử lý các tệp Git đang theo dõi; tệp mới cần được `git add` trước. Khi commit, formatter chạy trên tệp đã stage; nếu formatter sửa tệp, xem lại thay đổi, stage và commit lại. Chạy `make lint` riêng khi cần kiểm tra.

## Đồng bộ PDF

```sh
# Xem trước hoặc đồng bộ bài báo
python3 tools/sync_reference_pdfs.py --dry-run
make sync-papers

# Xem trước hoặc đồng bộ datasheet
python3 tools/sync_reference_pdfs.py --collection datasheets --dry-run
make sync-datasheets

# Trích xuất một PDF vào thư mục riêng để đối chiếu
python3 tools/sync_reference_pdfs.py docs/references/papers/huy2024-262.pdf --output-dir /tmp/papers_md
```

Đầu ra mặc định nằm trong `docs/references/papers/papers_md/` hoặc `docs/references/datasheets/datasheets_md/`. Script giữ nguyên thư mục đã có nội dung nhưng không có manifest và bảo vệ các tệp đầu ra đã sửa thủ công. Manifest chỉ lưu trạng thái cục bộ, không được đưa vào Git; sau khi clone, dùng `--output-dir` khác nếu muốn tạo bản trích xuất để đối chiếu.

Văn bản và ảnh được trích xuất tự động, không thay thế việc kiểm tra PDF gốc. PDF không có lớp văn bản cần OCR; sơ đồ vector, công thức và bảng có thể không được trích xuất đầy đủ. Xem thêm tùy chọn bằng `python3 tools/sync_reference_pdfs.py --help`.

## Dựng sơ đồ quadrotor

`python tools/build_quadrotor.py` đọc thư viện tại `hardware/kicad/symbols/`
và dựng lại schematic cùng cấu hình dự án. Lệnh ghi đè các bản chỉnh sửa thủ công;
xem [hướng dẫn KiCad](../hardware/kicad/README.md#dựng-lại-sơ-đồ) trước khi chạy.
Dữ liệu chân CSV được lưu tại `hardware/kicad/resources/`.
