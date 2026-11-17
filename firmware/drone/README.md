# Firmware drone

Firmware đọc BMI160/BMI270 qua SPI, nhận 16 kênh CRSF, tính rate PID và mixer
Quad X, rồi xuất PWM đến ESC. Gain và ánh xạ thrust hiện dành cho bộ gá, chưa
hiệu chuẩn bay. Xem [build và nạp](../README.md),
[debug và thử nghiệm](DEBUG.md) và [kiến trúc](ARCHITECTURE.md).

`Core/main` khởi tạo ngoại vi rồi gọi `runtime`; runtime quản lý receiver,
chọn chế độ và chuyển command vào `App`. `App` điều phối IMU, an toàn và
đầu ra; `BSP` kết nối phần cứng; `Control` chứa thuật toán độc lập HAL.
Kiểm thử host nằm trong `tests/`.

## Board

STM32F103VET6 có Flash 512 KiB, RAM 64 KiB; HSE 8 MHz, PLL ×9, CPU 72 MHz.
Cấu hình CubeMX nằm tại [drone.ioc](drone.ioc). Xác nhận mã chip, nguồn,
mức tín hiệu và wiring trên board thực tế trước khi nạp hoặc cấp nguồn ESC.

| Thiết bị          | Kết nối STM32             | Cấu hình                      |
| ----------------- | ------------------------- | ----------------------------- |
| IMU BMI160/BMI270 | SPI1: PA5/PA6/PA7, CS PA4 | SPI mode 0, 9 MHz             |
| Receiver ELRS     | TX → PA10, RX ← PA9       | USART1, CRSF 420000 baud, 8N1 |
| ESC               | PB6/PB7/PB8/PB9           | TIM4 PWM 400 Hz, 1000–2000 µs |

Thứ tự chân không trùng thứ tự motor: xem [PWM ESC](#pwm-esc).
Nối chung GND và tháo cánh trước khi thử motor.

## Chọn chế độ sau reset

Với cấu hình chọn mode thông thường, runtime chờ RC hợp lệ, CH5/SA arm OFF
và ga ≤2%; PWM tắt trong lúc chờ. Snapshot đầu tiên đủ điều kiện chọn mode
đến lần reset tiếp theo:

| CH7/SB     | CH8/SD                         | Chế độ                                        |
| ---------- | ------------------------------ | --------------------------------------------- |
| HIGH       | Bất kỳ                         | ESC_CALIB: hiệu chuẩn ESC                     |
| Không HIGH | HIGH / STREAM                  | FLASH_EXPORT: chưa triển khai, PWM không chạy |
| Không HIGH | LOW / OFF hoặc MIDDLE / RECORD | FLIGHT                                        |

Preset Debug mặc định bật `DRONE_DEBUG_FORCE_FLIGHT`, bỏ qua bước chọn trên;
Release mặc định tắt tùy chọn này. Để thử chọn mode hoặc hiệu chuẩn ESC bằng
Debug, tắt tùy chọn trong cấu hình build. Xem
[cấu hình radio](../Jumper-T20/README.md) cho ánh xạ kênh. CH6/SC chưa chọn
thuật toán bay; trong FLIGHT, SB HIGH chặn motor, không chuyển sang calib.
Các yêu cầu ghi/xuất telemetry qua SD chưa được thực thi trong FLIGHT.

## Hiệu chuẩn ESC

Tháo cánh và xác nhận ESC hỗ trợ hiệu chuẩn endpoint bằng PWM 400 Hz. Chọn
ESC_CALIB với SB HIGH, arm OFF và ga ≤2%. Firmware bắt đầu ở 1000 µs, sau đó
phát 2000 µs khi command còn hợp lệ. Nếu ESC học endpoint lúc cấp nguồn, cấp
nguồn flight controller và receiver riêng trước, xác nhận đang phát max rồi
mới cấp nguồn ESC. Theo hướng dẫn ESC, đưa SB về OFF để phát min; firmware giữ
min 3 giây rồi chờ reset. DONE chỉ xác nhận trình tự xung, không xác nhận ESC
đã học thành công.

Mất RC, bật arm, ga >2% hoặc giữ max quá 30 giây hủy phiên về min; lỗi PWM
tắt output. Các thời gian thử nằm trong `App/Inc/esc_calibration.h`. Timer vẫn
phát xung nếu CPU bị kẹt, nên timeout phần mềm không bảo vệ trường hợp đó.
Cần kiểm chứng xung, timing và kết quả trên board/ESC.

## IMU và receiver

IMU được cấu hình 400 Hz, accel ±16 g và gyro ±2000 °/s. App đổi mẫu sang SI
và hệ thân FRD (X tiến, Y phải, Z xuống), theo ánh xạ sensor `[-X, +Y, -Z]`;
cần xác nhận lại theo cách lắp thực tế. Chưa có hiệu chuẩn gyro bias. Timestamp
`t_read_us` là uptime MCU sau khi đọc thành công, không phải thời điểm cảm biến
lấy mẫu; tràn uint32 sau khoảng 71,6 phút. Xem API trong
[app_imu.h](App/Inc/app_imu.h) và driver [BMI160](Drivers/BMI160/README.md),
[BMI270](Drivers/BMI270/README.md).

USART1 nhận CRSF qua DMA; nối TX receiver vào PA10 và chung GND. `connected`
chỉ phản ánh frame RC hợp lệ trong 100 ms gần nhất, không đánh giá chất lượng
RF. Command dùng CH1–CH4 cho roll/pitch/throttle/yaw, CH5 arm, CH6 mode,
CH7 calib và CH8 telemetry. Endpoint và chiều stick cần xác nhận trên radio và
board; hợp đồng giải mã nằm trong [app_command.h](App/Inc/app_command.h).
Cần đo timing receiver trên board, đặc biệt khi đọc SPI gặp lỗi.

## PWM ESC

TIM4 phát PWM 400 Hz với tick 1 µs. Theo mixer Quad X, nhìn từ trên:

| Motor | Vị trí và chiều quay | Chân           |
| ----- | -------------------- | -------------- |
| M1    | Trước phải, CCW      | PB8 / TIM4_CH3 |
| M2    | Sau phải, CW         | PB9 / TIM4_CH4 |
| M3    | Sau trái, CCW        | PB6 / TIM4_CH1 |
| M4    | Trước trái, CW       | PB7 / TIM4_CH2 |

Khi chưa armed hoặc ga ≤2%, App gửi 1000 µs; lỗi ghi PWM tắt tín hiệu và khóa
đến lần khởi tạo App tiếp theo. Mất tín hiệu và xung min có thể khiến ESC phản
ứng khác nhau. Dải thrust → ESC tuyến tính 0–1 → 1000–2000 µs chưa được đo
và hiệu chuẩn. Xác nhận thứ tự, chiều quay, xung và hành vi ESC trên bộ gá.
API nằm trong [bsp_motor_pwm.h](BSP/Inc/bsp_motor_pwm.h).

## Kiểm thử host

Các bộ test có thể chạy riêng từ gốc repository; ví dụ:

```sh
cmake -S firmware/drone/tests/command -B /tmp/drone-command-tests
cmake --build /tmp/drone-command-tests
ctest --test-dir /tmp/drone-command-tests --output-on-failure
```

Thay `command` bằng `imu`, `crsf`, `pwm` hoặc `esc_calibration` để chọn bộ
tương ứng. Các test mô phỏng không xác nhận I/O hoặc timing trên board.
