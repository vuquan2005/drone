# Repository tham khảo

`repos/` chứa các bản sao mã nguồn dùng để đọc và đối chiếu. Thư mục này được `.gitignore` bỏ qua; clone repository đồ án sẽ không tải các bản sao này.

Các bản sao hiện có trên máy:

- `Arduino-Quadcopter`
- `MonteQuadcopter`
- `Quadcopter`
- `crazyflie-firmware`
- `betaflight`
- `PX4-Autopilot`
- `ardupilot`
- `inav`
- `BMI270_SensorAPI`

Các bản sao hiện tại không có thư mục `.git` riêng. Chưa xác nhận URL gốc và commit tương ứng; giữ nguyên tên để các dẫn chiếu nghiên cứu tiếp tục sử dụng được.

Khi bổ sung repo, ghi tên, URL, commit hoặc phiên bản và mục đích tham khảo tại đây. Chỉ lưu bản sao mã trong `repos/`; phần phân tích của đồ án đặt trong [research/](../research/README.md).

## Tra cứu bằng CodeGraph

Repo tham khảo dùng index riêng tại `repos/.codegraph/`; index gốc loại trừ
`docs/`. Khi đã có index, ưu tiên CodeGraph trước `rg` hoặc đọc nguồn trực tiếp.

Chạy từ gốc repository; thêm tên repo/file vào query để tránh nhầm symbol:

```sh
codegraph explore -p docs/external/repos "betaflight/src/main/sensors/gyro_init.c gyroSetTargetLooptime"
```

Với MCP `codegraph_explore`, đặt `projectPath` thành đường dẫn tuyệt đối tới
`docs/external/repos` và truyền cùng nội dung vào `query`.

Nếu thiếu index, dùng `rg` và đọc nguồn, không tự tạo index. Nếu công cụ cảnh
báo nguồn cũ, kiểm tra trực tiếp file liên quan. Xem [quy tắc chung](../../AGENTS.md#codegraph).
