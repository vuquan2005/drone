# Công cụ hỗ trợ

Hướng dẫn này dành cho công việc trong `tools/`. Các script hỗ trợ format, lint và trích xuất tài liệu; ưu tiên giữ chúng nhỏ, dễ chạy và phù hợp với cấu hình hiện có.

- Lệnh và cách cài đặt được mô tả trong [README.md](README.md); tra cứu khi cần, tránh lặp lại hướng dẫn ở nơi khác.
- Script Python hiện dùng Python 3.9+ và cấu hình Black trong `pyproject.toml` ở gốc. Ưu tiên thư viện chuẩn khi đáp ứng được yêu cầu.
- Khi sửa script xử lý file, chú ý bảo vệ đầu vào và bản biên tập thủ công; tận dụng chế độ xem trước hoặc thư mục đầu ra tạm để kiểm tra.
- Với script đồng bộ PDF, có thể kiểm tra bằng `python3 -m unittest discover -s tools/tests -v` từ gốc repo; bộ kiểm tra này sử dụng Poppler.
- Với thay đổi nhỏ, chọn hook trên đúng file đã sửa. Ví dụ từ gốc repo:

```sh
pre-commit run black --files tools/sync_reference_pdfs.py
```

`make help` liệt kê lệnh tổng hợp. `make format` sửa định dạng và `make lint` chạy lint trên toàn bộ file Git theo dõi; chọn phạm vi phù hợp với công việc.
