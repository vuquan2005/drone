# Datasheet linh kiện

Lưu PDF gốc tại đây; bản Markdown và ảnh nhúng được sinh trong [`datasheets_md/`](./datasheets_md/).

| Linh kiện | PDF gốc | Markdown trích xuất |
| --- | --- | --- |
| STM32F103RC | [DS_stm32f103rc.pdf](./DS_stm32f103rc.pdf) | [DS_stm32f103rc.md](./datasheets_md/DS_stm32f103rc/DS_stm32f103rc.md) |

Chạy từ thư mục gốc repository sau khi thêm hoặc cập nhật PDF:

```bash
python3 tools/sync_reference_pdfs.py --collection datasheets --dry-run
python3 tools/sync_reference_pdfs.py --collection datasheets
```

Mỗi PDF có một thư mục con chứa Markdown chia theo trang và ảnh nhúng. `.pdf-sync.json` là trạng thái đồng bộ cục bộ, được Git bỏ qua và ẩn trong VS Code; giữ tệp trên máy để script nhận biết thay đổi và bảo vệ nội dung đã sửa tay. Nếu thiếu manifest, script giữ nguyên thư mục đã có (`KEEP`). Thêm ghi chú vào tệp riêng nếu muốn tiếp tục đồng bộ tự động.

Bản trích xuất giữ văn bản theo bố cục trang; bảng thông số, sơ đồ vector và công thức cần đối chiếu PDF gốc. Xem [hướng dẫn công cụ](../../../tools/README.md) để biết các tùy chọn.
