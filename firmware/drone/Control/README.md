# Thuật toán điều khiển

## Mixer Quad X

`mixer_quad_x()` trong `Inc/mixer.h` chạy độc lập HAL, không có trạng thái hoặc
chu kỳ lấy mẫu riêng. Gọi mỗi chu kỳ có lệnh điều khiển mới.

Hệ trục thân FRD: x trước, y phải, z xuống. Thrust dương hướng lên;
roll dương nghiêng phải xuống, pitch dương nâng mũi, yaw dương quay phải.
Các đầu vào được chuẩn hóa: throttle `[0,1]`, roll/pitch/yaw `[-1,1]`;
không đưa trực tiếp N hoặc N·m vào API. Hệ số mỗi trục có độ lớn 0,5.

Cấu hình props-in; đánh số M1–M4 theo chiều kim đồng hồ, bắt đầu trước phải,
nhìn từ trên. Mảng C dùng index 0–3 tương ứng M1–M4.

| Motor | Index | Vị trí     | Chiều quay | Roll | Pitch | Yaw  |
| ----- | ----- | ---------- | ---------- | ---- | ----- | ---- |
| M1    | 0     | Trước phải | CCW        | -0,5 | +0,5  | +0,5 |
| M2    | 1     | Sau phải   | CW         | -0,5 | -0,5  | -0,5 |
| M3    | 2     | Sau trái   | CCW        | +0,5 | -0,5  | +0,5 |
| M4    | 3     | Trước trái | CW         | +0,5 | +0,5  | -0,5 |

Đây là thứ tự logic, chưa phải ánh xạ TIM4 trên board.

### Phân bổ khi bão hòa

1. Trộn roll/pitch; co chung hai trục nếu khoảng max−min vượt 1.
2. Chọn tỷ lệ yaw lớn nhất trong `[0,1]` sao cho mọi chênh lệch motor không vượt 1.
3. Dịch throttle tới điểm gần yêu cầu nhất trong khoảng khả thi.
4. Khôi phục lệnh thực hiện từ đầu ra; trả `achieved` và
   `residual = requested - achieved` để bộ điều khiển có thể xử lý chống tích lũy I.

Chính sách ưu tiên roll/pitch, rồi yaw, rồi throttle. Throttle có thể tăng kể cả
khi yêu cầu bằng 0 để giữ mômen. Nếu roll/pitch đã dùng hết khả năng thì yaw có
thể bằng 0; chưa có mức dự trữ yaw. Biên `[0,1]` là biên lực đẩy lý tưởng, chưa
xét idle, giới hạn pin hoặc sai khác giữa motor.

### Tích hợp

Module đã được đưa vào CMake firmware, chưa được gọi từ `app_process()`.
App cần sở hữu arm/failsafe, ánh xạ motor logic sang kênh board, áp dụng đường
đặc tính lực đẩy sang lệnh ESC và quyết định dừng motor. `app_motor_set_all()`
hiện đổi tuyến tính lệnh sang độ rộng PWM, nên không thể mặc định đầu ra mixer
là lệnh PWM tương đương lực đẩy thực tế. Lỗi đầu vào trả `false` và xóa kết quả;
App phải xử lý lỗi trước khi xuất lệnh. Giá trị 0 không thay thế lệnh disarm.

### Kiểm thử trên máy tính

```sh
cmake -S firmware/drone/tests/mixer -B /tmp/drone-mixer-tests
cmake --build /tmp/drone-mixer-tests
ctest --test-dir /tmp/drone-mixer-tests --output-on-failure
```

Kiểm tra dấu từng trục, 101871 tổ hợp lệnh, giới hạn motor, bảo toàn roll/pitch,
biên yaw và đầu vào lỗi. Timing và đáp ứng lực đẩy cần đo trên board/bộ gá.
