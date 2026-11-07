# AGENTS.md

Đây là đồ án phát triển drone quadrotor dùng STM32. Firmware tích hợp thuật toán điều khiển và giao tiếp với cảm biến, bộ thu và ESC. File này cung cấp hướng dẫn chung; tài liệu trong `docs/agent/` dùng để tra cứu khi cần thêm chi tiết.

## Cách làm việc

- Chọn cách thực hiện và mức kiểm tra theo quy mô thay đổi; việc nhỏ có thể xử lý trực tiếp.
- Giữ nguyên thay đổi có sẵn không thuộc công việc. Báo rõ kết quả đã kiểm tra và phần chưa xác minh.
- Ưu tiên cấu trúc và công cụ hiện có; điều chỉnh cách làm theo yêu cầu của người dùng và tình trạng thực tế của repo.
- Khi commit, dùng Conventional Commits, mô tả ngắn bằng tiếng Anh đơn giản; có thể dùng tiếng Việt nếu cần rõ hơn. Mặc định làm trên branch hiện tại; thao tác chia sẻ hoặc viết lại lịch sử theo phạm vi người dùng cho phép.
- Khi làm thuật toán, chú ý đơn vị, hệ tọa độ và chu kỳ lấy mẫu. Với firmware, chú ý vùng code sinh tự động; với thí nghiệm, giữ dữ liệu gốc và khả năng tái lập.

## Lệnh thường dùng

Chạy từ gốc repository; chọn phạm vi kiểm tra phù hợp với thay đổi.

| Lệnh                   | Tác dụng                                                     |
| ---------------------- | ------------------------------------------------------------ |
| `make report`          | Build `report/build/main.pdf` bằng latexmk.                  |
| `make format`          | Format toàn bộ tệp Git theo dõi qua pre-commit.              |
| `make lint`            | Lint Markdown, CMake và LaTeX trên toàn bộ tệp Git theo dõi. |
| `make sync-papers`     | Trích xuất PDF bài báo sang Markdown và ảnh.                 |
| `make sync-datasheets` | Trích xuất PDF datasheet sang Markdown và ảnh.               |
| `make clean`           | Xóa build của firmware và báo cáo, gồm cả PDF đã build.      |

## Kiến trúc

- `control/`: thuật toán điều khiển và ước lượng độc lập phần cứng; phần phụ thuộc STM32 HAL đặt trong `firmware/`.
- `firmware/`: project STM32, cấu hình board, driver và tích hợp thuật toán. `firmware/drone/AGENTS.md` hướng dẫn khi làm việc trong `firmware/drone/`.
- `hardware/`: sơ đồ điện, PCB, CAD và thông số linh kiện.
- `experiments/`: cấu hình, dữ liệu và kết quả thí nghiệm.
- `report/`: báo cáo LaTeX. `report/AGENTS.md` hướng dẫn khi làm việc trong `report/`.
- `docs/`: hướng dẫn, nghiên cứu và nguồn tham khảo, mã repos tham khảo; hướng dẫn dành cho agent đặt trong `docs/agent/`.
- `tools/`: script hỗ trợ. `tools/AGENTS.md` hướng dẫn khi làm việc trong `tools/`.

Thêm `AGENTS.md` trong module khi có hướng dẫn riêng hữu ích. Ưu tiên ít file, nội dung cụ thể và tránh lặp lại quy tắc chung.

## CodeGraph

Với mã nguồn đã có index `.codegraph/`, ưu tiên CodeGraph thay vì tìm kiếm hoặc đọc mã trực tiếp:

Index gốc dành cho mã nguồn chính. Để tra cứu mã tham khảo, dùng index riêng tại `docs/external/repos/`, nếu có; ví dụ `codegraph explore -p docs/external/repos "<symbol>"`. Với MCP, truyền `projectPath` trỏ đến thư mục tương ứng.

## Tra cứu khi cần

- [Git](docs/agent/git.md): Đọc khi có các thao tác Git, commit, branch, PR, issue hoặc đồng bộ nhánh làm việc.
