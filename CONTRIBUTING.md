# Làm việc nhóm

## Nhận và thực hiện nhiệm vụ

1. Tạo một [issue](https://github.com/vuquan2005/drone/issues/new/choose) cho mỗi việc có kết quả rõ ràng. Dùng mẫu **Nhiệm vụ** hoặc viết tự do với mục tiêu và điều kiện hoàn thành.
2. Gán một người phụ trách chính ở **Assignees**. Chọn nhãn phù hợp: `firmware`, `hardware`, `control`, `report`; thêm `bug` nếu là lỗi.
3. Trước khi sửa, cập nhật `main` rồi tạo branch riêng, ví dụ `feat/gyro-calibration`, `fix/crsf-timeout` hoặc `docs/control-report`.
4. Cập nhật tiến độ, vướng mắc và kết quả trong bình luận issue. Dùng `@username` khi cần một người góp ý.
5. Mở Pull Request (PR) vào `main`, mô tả thay đổi và cách kiểm tra. Có thể mở Draft PR khi đang làm để trao đổi sớm.
6. Nhờ `@vuquan2005` review, xử lý góp ý rồi merge bằng **Squash and merge** khi đã được duyệt. Tiêu đề PR dùng Conventional Commits, ví dụ `feat(imu): calibrate gyro bias`.

Ghi `Closes #123` trong mô tả PR khi hoàn thành issue #123; GitHub sẽ đóng issue khi PR được merge vào nhánh mặc định. Nếu chỉ làm một phần, dùng `Refs #123`.

Với thử nghiệm phần cứng hoặc nghiên cứu không sửa tệp, đăng kết quả đo, ảnh hoặc kết luận vào issue; đóng khi đã đạt điều kiện hoàn thành.

## Review và kiểm tra

- Trao đổi yêu cầu và tiến độ trong issue; nhận xét code tại **Files changed** của PR.
- Reviewer chọn **Comment**, **Request changes** hoặc **Approve** tùy kết quả xem xét.
- Chọn kiểm tra phù hợp với thay đổi; xem hướng dẫn build/test trong từng module. Phân biệt kiểm thử trên máy tính với xác minh trên board.
- Mỗi PR nên tập trung vào một việc. Khi nhiều người cùng sửa một module, thống nhất phạm vi trong issue trước.

## Bảng tiến độ

Thêm issue vào [bảng tiến độ nhóm](https://github.com/users/vuquan2005/projects/5) với bốn trạng thái:

| Trạng thái  | Khi nào dùng                     |
| ----------- | -------------------------------- |
| Todo        | Đã xác định việc cần làm         |
| In progress | Người phụ trách đang thực hiện   |
| Review      | Đang chờ xem xét PR hoặc kết quả |
| Done        | Đã được chấp nhận và đóng issue  |

Chỉ đưa issue vào bảng để mỗi nhiệm vụ xuất hiện một lần; liên kết PR với issue. Nhãn dùng để phân loại lĩnh vực, trạng thái dùng để theo dõi tiến độ.
