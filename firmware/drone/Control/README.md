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

### Biểu diễn fixed-point

Chỉ `mixer_command_t` nhận `float`. Sau khi kiểm tra miền và tính hữu hạn,
mixer lượng tử hóa một lần sang `norm_q15_t` trong `Inc/normalized.h`:
`int32_t`, 15 bit phần lẻ, giá trị thực bằng số lưu chia 32768. Ví dụ 0,5 là
16384; cả −1 và +1 đều biểu diễn chính xác. Đầu vào làm tròn gần nhất, điểm
giữa làm tròn ra xa 0. Bước lượng tử là khoảng 0,00003052.

Toàn bộ phép phân bổ sau đó dùng số nguyên, phép nhân trung gian dùng
`int64_t`. `motor[]`, `achieved`, `residual`, `roll_pitch_scale` và `yaw_scale`
đều dùng cùng thang; hệ số 1 được lưu bằng `NORM_Q15_ONE` (32768).
`residual` là lệnh **đã lượng tử hóa** trừ `achieved`; sai số tối đa nửa bước
ở cửa vào không nằm trong phản hồi này. Hệ số co làm tròn xuống để giữ tính
khả thi; phép chia có dấu làm tròn về 0. Không kẹp từng motor để che sai số
phân bổ. `achieved` được tính từ đầu ra số nguyên cuối cùng.

### Phân bổ khi bão hòa

1. Trộn roll/pitch; co chung hai trục nếu khoảng max−min vượt 1.
2. Tìm biên tỷ lệ yaw khả thi trong `[0,1]`, làm tròn xuống theo thang fixed-point.
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
đặc tính lực đẩy sang lệnh ESC và quyết định dừng motor. Đường đặc tính và
giới hạn bổ sung cần giữ dạng fixed-point khi triển khai.
`app_motor_set_all()` nhận mảng `norm_q15_t` theo thứ tự kênh board và đổi
tuyến tính bằng số nguyên sang độ rộng PWM: 0 → 1000 µs, 32768 → 2000 µs;
làm tròn gần nhất, điểm giữa làm tròn lên. Không có bước đổi ngược sang float.
Chung kiểu số không đồng nghĩa chung đại lượng: đầu ra mixer là lực đẩy,
đầu vào App motor là lệnh ESC. Không thể mặc định ánh xạ tuyến tính tương đương
lực đẩy thực tế. Lỗi đầu vào mixer trả `false` và xóa kết quả;
App phải xử lý lỗi trước khi xuất lệnh. Giá trị 0 không thay thế lệnh disarm.

### Kiểm thử trên máy tính

```sh
cmake -S firmware/drone/tests/mixer -B /tmp/drone-mixer-tests
cmake --build /tmp/drone-mixer-tests
ctest --test-dir /tmp/drone-mixer-tests --output-on-failure
```

Kiểm tra 101871 tổ hợp lệnh và 100000 mẫu có seed cố định, các điểm biên
lượng tử hóa, giới hạn motor, ưu tiên roll/pitch, biên yaw và đầu vào lỗi.
Đối chiếu từng motor với thuật toán float liên tục với dung sai 8 bước Q15
(khoảng 0,0002442); đây là dung sai kiểm thử, chưa phải chứng minh sai số
toàn miền. Test PWM kiểm tra truyền đầu ra mixer sang thanh ghi timer bằng
HAL mô phỏng, giả định ánh xạ motor/kênh và lực đẩy/lệnh ESC đồng nhất chỉ
trong fixture. Timing và đáp ứng lực đẩy cần đo trên board/bộ gá.
