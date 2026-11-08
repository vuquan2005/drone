# AGENTS.md

Đây là đồ án phát triển drone quadrotor dùng STM32. Firmware tích hợp thuật toán điều khiển và giao tiếp với cảm biến, bộ thu và ESC. File này cung cấp hướng dẫn chung cho agent.

## Cách làm việc

- Đọc `AGENTS.md` trong module liên quan trước khi sửa; ưu tiên cấu trúc và công cụ hiện có.
- Giữ nguyên thay đổi ngoài phạm vi; kiểm tra tương xứng với thay đổi và báo rõ phần chưa xác minh.
- Với thuật toán và thí nghiệm, làm rõ đơn vị, hệ tọa độ, chu kỳ lấy mẫu; giữ dữ liệu gốc và khả năng tái lập.

## Lệnh thường dùng

Chạy từ gốc repository; chọn phạm vi kiểm tra phù hợp với thay đổi.

| Lệnh                   | Tác dụng                                                     |
| ---------------------- | ------------------------------------------------------------ |
| `make report`          | Build `report/build/main.pdf` bằng latexmk.                  |
| `make cubemx`          | Sinh lại firmware từ `drone.ioc` bằng CubeMX CLI.            |
| `make format`          | Format toàn bộ tệp Git theo dõi qua pre-commit.              |
| `make lint`            | Lint Markdown, CMake và LaTeX trên toàn bộ tệp Git theo dõi. |
| `make sync-papers`     | Trích xuất PDF bài báo sang Markdown và ảnh.                 |
| `make sync-datasheets` | Trích xuất PDF datasheet sang Markdown và ảnh.               |
| `make clean`           | Xóa build của firmware và báo cáo, gồm cả PDF đã build.      |

## Kiến trúc

- `firmware/`: project STM32, cấu hình board, driver và tích hợp thuật toán. Thuật toán lọc, ước lượng và điều khiển đặt trong `firmware/drone/Control/`, độc lập HAL; `App/` điều phối và quản lý trạng thái. `firmware/drone/AGENTS.md` hướng dẫn khi làm việc trong `firmware/drone/`.
- `hardware/`: sơ đồ điện, PCB, CAD và thông số linh kiện.
- `experiments/`: cấu hình, dữ liệu và kết quả thí nghiệm.
- `report/`: báo cáo LaTeX. `report/AGENTS.md` hướng dẫn khi làm việc trong `report/`.
- `docs/`: hướng dẫn, nghiên cứu và nguồn tham khảo, mã repos tham khảo.
- `tools/`: script hỗ trợ. `tools/AGENTS.md` hướng dẫn khi làm việc trong `tools/`.

Thêm `AGENTS.md` trong module khi có hướng dẫn riêng hữu ích. Ưu tiên ít file, nội dung cụ thể và tránh lặp lại quy tắc chung.

Kế hoạch hiện tại không triển khai mô phỏng vòng kín hoặc SITL. Giữ kiểm thử
thuật toán nhỏ trên máy tính khi có ý nghĩa; kiểm chứng I/O, timing và đáp ứng
điều khiển trên board/bộ gá. Lưu dữ liệu đo tại `experiments/`, script phân tích
tại `tools/`.

## CodeGraph

Với mã nguồn đã có index `.codegraph/`, ưu tiên CodeGraph thay vì tìm kiếm hoặc đọc mã trực tiếp:

Index gốc dành cho mã nguồn chính. Để tra cứu mã tham khảo, dùng index riêng tại `docs/external/repos/`, nếu có; ví dụ `codegraph explore -p docs/external/repos "<symbol>"`. Với MCP, truyền `projectPath` trỏ đến thư mục tương ứng.

## Git

Chạy lệnh từ gốc repository, tuân theo yêu cầu của người dùng và giới hạn quyền hiện hành.

- Kiểm tra `git status --short` trước khi thao tác; phân biệt thay đổi có sẵn với thay đổi của công việc hiện tại.
- Xem diff và nội dung tệp mới trước khi stage; chỉ stage thay đổi thuộc phạm vi được yêu cầu.
- Khi được yêu cầu commit, kiểm tra `git diff --cached`, chạy kiểm tra phù hợp và xử lý thay đổi hoặc lỗi từ hook trước khi hoàn tất. Không tự thay đổi danh tính Git; báo mã commit, kiểm tra đã chạy và giới hạn còn lại.
- Chỉ push, mở PR hoặc sửa lịch sử khi được yêu cầu và trong phạm vi đã được phép. Không tự force push, viết lại lịch sử đã chia sẻ hoặc thực hiện thao tác phá hủy.

### Commit

- Dùng Conventional Commits: `type(scope): description`; type và scope viết thường, scope ngắn và cụ thể, bỏ scope nếu không rõ phạm vi.
- Dòng đầu ngắn gọn, cụ thể, dùng tiếng Anh đơn giản; dùng tiếng Việt nếu diễn đạt rõ hơn. Body ưu tiên tiếng Việt, giải thích lý do hoặc tác động khi cần.
- Giữ mỗi commit tập trung vào một việc. Đánh dấu thay đổi không tương thích bằng `!` trước dấu `:` hoặc footer `BREAKING CHANGE:`, kèm cách chuyển đổi khi cần. Ưu tiên quy ước khác nếu người dùng yêu cầu.

### Branch và PR

- Mặc định tiếp tục trên branch hiện tại. Trước khi chuyển branch, kiểm tra branch và working tree; giữ nguyên thay đổi chưa commit. Nếu không thể chuyển an toàn, hỏi người dùng; không tự stash, ghi đè hoặc xóa thay đổi.
- Khi được yêu cầu tạo branch mà chưa có tên, dùng tên ngắn mô tả công việc, viết thường và ngăn cách từ bằng dấu gạch ngang, như `fix/pid-windup`.
- Trước khi chuyển PR sang "ready for review", kiểm tra các commit và nội dung PR; thêm commit cleanup nếu cần.
- PR của thành viên trong nhóm vào `main` cần được @vuquan2005 phê duyệt trên GitHub; nếu @vuquan2005 trực tiếp yêu cầu thì không cần approve thủ công. Agent không tự approve, bypass quy tắc hoặc merge khi chưa được phép.
- Khi được yêu cầu merge PR, mặc định squash and merge, lấy tiêu đề PR theo Conventional Commits làm tiêu đề commit squash. Chỉ dùng merge commit khi người dùng yêu cầu giữ lịch sử từng commit.
- Sau merge, mặc định xóa branch công việc ở local và remote; giữ lại nếu người dùng yêu cầu hoặc đó là branch lâu dài. Trước khi xóa, xác nhận PR đã merge và cả branch local lẫn remote không có commit ngoài PR; sau squash, không chỉ dựa vào dấu đã merge của Git. Không tự xóa branch khác.

### Đồng bộ với main

- Khi bắt đầu công việc có sửa repository và trước khi chuyển PR sang "ready for review", chạy `git fetch origin` nếu quyền cho phép, rồi kiểm tra số commit còn thiếu bằng `git rev-list --count HEAD..origin/main`. Nếu không fetch được, nói rõ kết quả chỉ dựa trên dữ liệu local.
- Nếu còn thiếu commit, nhắc người dùng đồng bộ: gợi ý `git rebase origin/main` cho nhánh cá nhân hoặc `git merge origin/main` cho nhánh nhiều người cùng làm.
- Chỉ đồng bộ khi người dùng cho phép; kiểm tra working tree trước và giữ nguyên thay đổi chưa commit ngoài phạm vi công việc.
