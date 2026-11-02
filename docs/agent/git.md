# Git cho agent

Chạy lệnh từ gốc repository và tuân theo yêu cầu của người dùng cùng các giới hạn quyền hiện hành.

- Kiểm tra `git status --short` trước khi thao tác; phân biệt thay đổi có sẵn với thay đổi của công việc hiện tại.
- Xem diff và kiểm tra nội dung tệp mới trước khi stage. Chỉ stage những thay đổi thuộc phạm vi được yêu cầu; giữ nguyên thay đổi không liên quan.
- Khi được yêu cầu commit, kiểm tra `git diff --cached` và áp dụng quy ước thông điệp bên dưới; không tự thay đổi danh tính Git.
- Chạy các kiểm tra phù hợp. Nếu hook sửa tệp hoặc thất bại, xem lại thay đổi và xử lý trước khi hoàn tất commit.
- Báo mã commit, kiểm tra đã chạy và những giới hạn liên quan. Không tự push hoặc thực hiện thao tác phá hủy khi chưa được yêu cầu.

## Thông điệp commit

- Dùng Conventional Commits: `type(scope): description`. Ưu tiên scope ngắn, cụ thể; bỏ scope khi không có phạm vi rõ ràng. Viết type và scope bằng chữ thường.
- Viết dòng đầu của commit message ngắn gọn, dễ hiểu và cụ thể, bằng tiếng Anh đơn giản; dùng tiếng Việt nếu tiếng Anh khó diễn đạt rõ. Nếu cần body, ưu tiên tiếng Việt.
- Chọn type theo mục đích chính: `feat`, `fix`, `docs`, `refactor`, `test`, `build`, `ci`, `perf`, `style` hoặc `chore`. Ví dụ: `chore(codegraph): limit root index to firmware`.
- Thêm body để giải thích lý do hoặc tác động khi dòng đầu chưa đủ. Với thay đổi không tương thích, đánh dấu bằng `!` trước dấu `:` hoặc footer `BREAKING CHANGE:`; thêm cách chuyển đổi khi cần.
- Giữ mỗi commit tập trung vào một việc; tuân theo thông điệp hoặc quy ước khác nếu người dùng yêu cầu.

## Branch và PR

- Kiểm tra branch hiện tại và trạng thái làm việc trước khi thay đổi branch. Mặc định tiếp tục trên branch hiện tại; không tự tạo hoặc chuyển branch chỉ vì bắt đầu một công việc.
- Khi người dùng yêu cầu tạo branch mà không chỉ định tên, dùng tên ngắn mô tả công việc như `feat/imu-calibration`, `fix/pid-windup` hoặc `docs/git-guide`; giữ tên chữ thường, dùng dấu gạch ngang giữa các từ.
- Giữ nguyên các thay đổi chưa commit khi chuyển branch. Nếu chúng ngăn việc chuyển an toàn, không tự stash, ghi đè hoặc xóa chúng; làm rõ với người dùng.
- Khi được yêu cầu merge PR, mặc định dùng squash and merge. Dùng tiêu đề PR theo Conventional Commits làm tiêu đề commit squash; chỉ dùng merge commit khi người dùng yêu cầu giữ lịch sử từng commit.
- Sau khi merge thành công, mặc định xóa branch công việc ở local và remote; giữ lại nếu người dùng yêu cầu hoặc đó là branch làm việc lâu dài. Trước khi xóa, xác nhận PR đã merge và cả branch local lẫn remote không có commit ngoài PR đã merge. Sau squash, không chỉ dựa vào việc Git đánh dấu branch là đã merge.
- Không tự xóa các branch khác, viết lại lịch sử đã chia sẻ hoặc force push. Chỉ push, mở PR hoặc sửa lịch sử khi được yêu cầu và trong phạm vi đã được phép.
- Trước khi chuyển PR sang trạng thái "ready for review", kiểm tra các commit và nội dung PR; thêm commit cleanup nếu cần.
- Mọi PR của thành viên trong nhóm vào main cần được @vuquan2005 phê duyệt trên GitHub. Nếu @vuquan2005 trực tiếp yêu cầu, không cần bước approve thủ công. Agent không tự approve, bypass quy tắc hoặc merge khi chưa được phép.

## Đồng bộ nhánh làm việc với main

- Khi bắt đầu công việc và trước khi chuyển PR sang "ready for review", chạy `git fetch origin` (nếu quyền cho phép) rồi kiểm tra nhánh còn thiếu commit nào từ `origin/main` bằng `git rev-list --count HEAD..origin/main`. Nếu không fetch được, nói rõ kết quả chỉ dựa trên dữ liệu local.
- Nếu còn thiếu commit, nhắc người dùng đồng bộ. Gợi ý `git rebase origin/main` cho nhánh cá nhân, hoặc `git merge origin/main` cho nhánh nhiều người cùng làm; không tự rebase lịch sử đã chia sẻ hoặc force push.
- Chỉ đồng bộ khi người dùng cho phép. Trước đó kiểm tra working tree; không tự stash hoặc xử lý thay đổi chưa commit ngoài phạm vi công việc.
