# Hướng dẫn cho agent

File này chứa hướng dẫn chung đủ để bắt đầu công việc. Tài liệu trong `docs/agent/` dùng để tra cứu khi cần thêm chi tiết.

## Cách làm việc

- Chọn cách thực hiện và mức kiểm tra theo quy mô thay đổi; việc nhỏ có thể xử lý trực tiếp.
- Giữ nguyên thay đổi có sẵn không thuộc công việc. Báo rõ kết quả đã kiểm tra và phần chưa xác minh.
- Ưu tiên cấu trúc và công cụ hiện có; điều chỉnh cách làm theo yêu cầu của người dùng và tình trạng thực tế của repo.
- Khi commit, dùng Conventional Commits, mô tả ngắn bằng tiếng Anh đơn giản; có thể dùng tiếng Việt nếu cần rõ hơn. Mặc định làm trên branch hiện tại; thao tác chia sẻ hoặc viết lại lịch sử theo phạm vi người dùng cho phép.
- Khi làm thuật toán, chú ý đơn vị, hệ tọa độ và chu kỳ lấy mẫu. Với firmware, chú ý vùng code sinh tự động; với thí nghiệm, giữ dữ liệu gốc và khả năng tái lập.

## Ranh giới module

- `control/`: thuật toán điều khiển và ước lượng độc lập phần cứng; phần phụ thuộc STM32 HAL đặt trong `firmware/`.
- `firmware/`: project STM32, cấu hình board, driver và tích hợp thuật toán.
- `hardware/`: sơ đồ điện, PCB, CAD và thông số linh kiện.
- `experiments/`: cấu hình, dữ liệu và kết quả thí nghiệm.
- `report/`: báo cáo LaTeX.
- `docs/`: hướng dẫn, nghiên cứu và nguồn tham khảo, mã repos tham khảo; hướng dẫn dành cho agent đặt trong `docs/agent/`.
- `tools/`: script hỗ trợ.

Thêm `AGENTS.md` trong module khi có hướng dẫn riêng hữu ích. Ưu tiên ít file, nội dung cụ thể và tránh lặp lại quy tắc chung.

## Tra cứu khi cần

- [Công cụ](tools/AGENTS.md): cách dùng script, ví dụ `tools/AGENTS.md`.
- [Git](docs/agent/git.md): quy ước commit và branch.
- [Báo cáo](report/README.md): cách build LaTeX và quản lý trích dẫn.

## CodeGraph

Trong repository có `.codegraph/` ở gốc, dùng CodeGraph trước khi tìm kiếm hoặc đọc tệp để tìm hiểu mã nguồn:

- Ưu tiên MCP `codegraph_explore` nếu có; đưa tên tệp, symbol hoặc câu hỏi vào truy vấn.
- Nếu không có MCP, dùng `codegraph explore "<symbol hoặc câu hỏi>"`.
- Nếu không có `.codegraph/`, bỏ qua CodeGraph; không tự tạo index.

Index gốc dành cho firmware. Để tra cứu mã tham khảo, dùng index riêng tại `docs/external/repos/`, nếu có; ví dụ `codegraph explore -p docs/external/repos "<symbol>"`.
