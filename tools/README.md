# Công cụ hỗ trợ

## Đồng bộ PDF bài báo sang Markdown

`sync_papers.py` dùng Python 3.9+ và Poppler (`pdftotext`, `pdfimages`), không cần thư viện Python bên ngoài. Trên Ubuntu/Debian hoặc Fedora, gói Poppler tương ứng là `poppler-utils`.

Chạy từ thư mục gốc repository:

```bash
# Xem trước các thay đổi
python3 tools/sync_papers.py --dry-run

# Đồng bộ tất cả PDF trong docs/references/papers/
python3 tools/sync_papers.py

# Chỉ xử lý một PDF
python3 tools/sync_papers.py docs/references/papers/huy2024-262.pdf

# Trích xuất lại vào thư mục riêng để đối chiếu với bản biên tập hiện có
python3 tools/sync_papers.py docs/references/papers/huy2024-262.pdf --output-dir /tmp/papers_md

# Thử thứ tự đọc mặc định của Poppler cho PDF nhiều cột
python3 tools/sync_papers.py docs/references/papers/huy2024-262.pdf --output-dir /tmp/papers_md --text-mode reading
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

- **CREATE:** Chưa có đầu ra, tạo Markdown, ảnh và manifest.
- **UPDATE:** PDF hoặc chế độ trích xuất thay đổi, hoặc thiếu tệp đầu ra; tạo lại các tệp do script quản lý. `--force` tạo lại dù PDF chưa đổi.
- **UNCHANGED:** PDF và các tệp đã sinh còn nguyên, bỏ qua để tránh chuyển đổi lại.
- **KEEP:** Thư mục đã có nội dung nhưng không có manifest; giữ nguyên bản biên tập thủ công. Các bài đang có trong kho thuộc trường hợp này. Dùng `--output-dir` khác để lấy bản tự động đối chiếu.
- **ERROR:** Có tệp đã sinh được sửa thủ công hoặc trích xuất thất bại; giữ bản cũ và trả mã thoát khác 0. `--force` cũng bảo vệ các chỉnh sửa thủ công.

`--papers-dir` chọn thư mục nguồn khác; nếu không truyền `--output-dir`, đầu ra là `<papers-dir>/papers_md`. Đường dẫn PDF truyền trực tiếp được tính từ thư mục làm việc hiện tại. Script chỉ đồng bộ bản trích xuất: không sửa README/BibTeX và không xóa bài khi PDF nguồn bị bỏ khỏi thư mục.

Kiểm tra luồng đồng bộ bằng Poppler thật và PDF mẫu tự tạo:

```bash
python3 -m unittest discover -s tools/tests -v
```
