# Công cụ hỗ trợ

## Lệnh nhanh từ thư mục gốc

Chạy `make` hoặc `make help` để xem hướng dẫn:

| Lệnh                   | Tác dụng                                                                           |
| ---------------------- | ---------------------------------------------------------------------------------- |
| `make format`          | Sửa định dạng bằng Prettier, clang-format, cmake-format, latexindent và Black      |
| `make lint`            | Kiểm tra Markdown bằng markdownlint-cli2, CMake bằng cmake-lint, LaTeX bằng ChkTeX |
| `make sync-papers`     | Trích xuất toàn bộ PDF bài báo                                                     |
| `make sync-datasheets` | Trích xuất toàn bộ PDF datasheet                                                   |

Cần Make, Git và Python để chạy bộ công cụ; không cần cài sẵn Node.js hoặc từng formatter. Cài framework một lần, rồi đăng ký hook cho mỗi bản clone:

```bash
uv tool install pre-commit
# Chỉ cần khi chuyển từ cấu hình hook cũ của repository:
git config --local --unset core.hooksPath
pre-commit install --install-hooks
```

Nếu `core.hooksPath` chưa được đặt thì bỏ qua lệnh `--unset`. Pre-commit tự tạo môi trường, tải Node.js theo `default_language_version` và cài các hook từ repo mạng đã ghim `rev` trong `.pre-commit-config.yaml`: Black, clang-format, cmake-format/cmake-lint và markdownlint-cli2. Prettier dùng mirror `rbubley/mirrors-prettier` vì mirror cũ của tổ chức `pre-commit` đã được archive. Lần đầu cần mạng; các lần sau dùng cache. Riêng latexindent và ChkTeX dùng bản đã cài trong TeX distribution trên máy.

Dùng `pre-commit autoupdate` để cập nhật các `rev`, rồi kiểm tra diff trước khi áp dụng; phiên bản Node được cập nhật riêng ở `default_language_version`. Makefile gọi trực tiếp ID của các hook tương ứng. `make format` và `make lint` xử lý toàn bộ tệp Git đang theo dõi; tệp mới phải `git add` trước. Phạm vi loại trừ chung được khai báo ở `exclude` trong `.pre-commit-config.yaml`: tài liệu tham khảo, kho ngoài, mã CubeMX/vendor, dữ liệu thí nghiệm và thư mục build. Các công cụ tiếp tục đọc cấu hình riêng của chúng. Python dùng Black với `pyproject.toml` (100 ký tự/dòng, Python 3.9+ cho mã nguồn); VS Code dùng extension `ms-python.black-formatter`.

Khi commit, chỉ các formatter chạy trên tệp đã stage; lint được gọi riêng bằng `make lint`. Framework tạm cất phần chưa stage rồi khôi phục lại, không tự stage thay đổi. Nếu formatter sửa tệp, commit dừng để bạn xem diff, `git add` phần mong muốn và commit lần nữa. Khi việc khôi phục phần chưa stage xung đột với kết quả format, framework hoàn tác phần tự sửa và khôi phục thay đổi của bạn. Công cụ thiếu hoặc kiểm tra thất bại đều trả lỗi.

`make format` cũng có thể trả mã thoát 1 khi formatter vừa sửa tệp; xem diff và chạy lại để xác nhận. Lint hiện gồm Markdown, CMake và tài liệu LaTeX (`*.tex`); ChkTeX dùng `report/.chktexrc` như cấu hình báo cáo trong editor. Các tệp macro `.sty`/`.cls` vẫn được format, nhưng không đưa qua linter dành cho tài liệu. Chưa phân tích tĩnh C/C++. Không có target `test` hoặc `check` tổng hợp.

Có thể chạy trực tiếp `pre-commit run` để xử lý staged, `pre-commit run --all-files` để chạy các formatter trên toàn bộ tệp theo dõi, hoặc `pre-commit run black --files tools/sync_reference_pdfs.py` để format một tệp. Build/test có thể bổ sung sau; chưa được bật trong cấu hình hiện tại.

## Đồng bộ PDF bài báo và datasheet sang Markdown

`sync_reference_pdfs.py` dùng Python 3.9+ và Poppler (`pdftotext`, `pdfimages`), không cần thư viện Python bên ngoài. Trên Ubuntu/Debian hoặc Fedora, gói Poppler tương ứng là `poppler-utils`.

Chạy từ thư mục gốc repository:

```bash
# Xem trước các thay đổi
python3 tools/sync_reference_pdfs.py --dry-run

# Đồng bộ tất cả PDF trong docs/references/papers/
python3 tools/sync_reference_pdfs.py

# Đồng bộ PDF datasheet sang docs/references/datasheets/datasheets_md/
python3 tools/sync_reference_pdfs.py --collection datasheets

# Xem trước thay đổi cho datasheet
python3 tools/sync_reference_pdfs.py --collection datasheets --dry-run

# Chỉ xử lý một PDF
python3 tools/sync_reference_pdfs.py docs/references/papers/huy2024-262.pdf

# Trích xuất lại vào thư mục riêng để đối chiếu với bản biên tập hiện có
python3 tools/sync_reference_pdfs.py docs/references/papers/huy2024-262.pdf --output-dir /tmp/papers_md

# Thử thứ tự đọc mặc định của Poppler cho PDF nhiều cột
python3 tools/sync_reference_pdfs.py docs/references/papers/huy2024-262.pdf --output-dir /tmp/papers_md --text-mode reading
```

Mặc định, đầu ra nằm trong `docs/references/papers/papers_md/<tên-pdf>/`:

```text
huy2024-262/
├── huy2024-262.md   # Văn bản chia theo trang, kèm liên kết đến PDF
├── img-000.png     # Các ảnh nhúng lấy bằng pdfimages -png
├── ...
└── .pdf-sync.json  # SHA-256 của PDF, chế độ trích xuất và các tệp sinh ra
```

`pdftotext -layout -enc UTF-8` là chế độ mặc định. Văn bản được đặt trong khối `text` để giữ cách căn cột và các ký tự nguyên gốc. `--text-mode reading` bỏ `-layout` để dùng thứ tự đọc mặc định của Poppler. Hình nhúng được nhóm theo trang; sơ đồ vector và chữ phủ bên ngoài ảnh không được `pdfimages` trích xuất. Script không tự suy ra tiêu đề, công thức LaTeX, cấu trúc bảng, chú thích hình hoặc thông tin BibTeX; chúng cần được biên tập từ PDF. PDF không có lớp văn bản sẽ được báo lỗi cần OCR.

Quy tắc đồng bộ:

Toàn bộ tệp JSON trong `docs/references/` được Git bỏ qua, bao gồm metadata trích xuất và manifest `.pdf-sync.json` của cả papers lẫn datasheets. Manifest là trạng thái đồng bộ cục bộ và được ẩn trong Explorer của VS Code; không cần mở hoặc sửa. Nếu manifest bị xóa hoặc bạn clone repository sang máy khác, thư mục Markdown đã có sẽ được giữ nguyên với trạng thái `KEEP`; dùng `--output-dir` khác để tạo bản trích xuất mới.

Các tệp `*.jpeg` trong `docs/references/` cũng được ignore cho tệp mới. Ảnh JPEG đã được Git theo dõi vẫn được giữ: Markdown cũ đang tham chiếu tên dạng `_page_*_Picture_*.jpeg`, trong khi script này xuất `img-*.png`, nên chạy lại không khôi phục đúng các liên kết ảnh cũ. Quy tắc JPEG không áp dụng cho ảnh PNG do script hiện tại tạo.

- **CREATE:** Chưa có đầu ra, tạo Markdown, ảnh và manifest.
- **UPDATE:** PDF hoặc chế độ trích xuất thay đổi, hoặc thiếu tệp đầu ra; tạo lại các tệp do script quản lý. `--force` tạo lại dù PDF chưa đổi.
- **UNCHANGED:** PDF và các tệp đã sinh còn nguyên, bỏ qua để tránh chuyển đổi lại.
- **KEEP:** Thư mục đã có nội dung nhưng không có manifest; giữ nguyên bản biên tập thủ công. Các bài đang có trong kho thuộc trường hợp này. Dùng `--output-dir` khác để lấy bản tự động đối chiếu.
- **ERROR:** Có tệp đã sinh được sửa thủ công hoặc trích xuất thất bại; giữ bản cũ và trả mã thoát khác 0. `--force` cũng bảo vệ các chỉnh sửa thủ công.

`--collection papers` (mặc định) hoặc `--collection datasheets` chọn bộ tài liệu trong `docs/references/`. `--source-dir` chọn thư mục nguồn khác; `--papers-dir` vẫn được hỗ trợ như tên cũ. Nếu không truyền `--output-dir`, đầu ra là `<source-dir>/<collection>_md`. Khi truyền trực tiếp đường dẫn PDF datasheet, vẫn cần `--collection datasheets` để chọn đúng thư mục đầu ra. Đường dẫn PDF truyền trực tiếp được tính từ thư mục làm việc hiện tại. Script chỉ đồng bộ bản trích xuất: không sửa README/BibTeX và không xóa bản trích xuất khi PDF nguồn bị bỏ khỏi thư mục.

Chạy các lệnh trên trong terminal của VS Code. Material Icon Theme được đề xuất trong workspace; `datasheets` dùng icon PDF, `datasheets_md` và `papers_md` dùng icon Markdown.

Kiểm tra luồng đồng bộ bằng Poppler thật và PDF mẫu tự tạo:

```bash
python3 -m unittest discover -s tools/tests -v
```
