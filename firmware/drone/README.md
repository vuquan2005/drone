# Firmware drone

Ứng dụng hiện khởi tạo và poll BMI270 qua SPI1/CS PD7, đổi mẫu đo sang SI.
Đã nhận 16 kênh RC qua CRSF và xuất PWM ESC qua TIM4; chưa có estimator,
vòng điều khiển, mixer hay arm/failsafe.
Xem [cấu hình chân, build và debug](../README.md) để bắt đầu.
Khung RTT hai kênh và Live Watch được mô tả tại [DEBUG.md](DEBUG.md).

## Mã nguồn

| Thư mục / tệp                  | Nội dung                                                                     |
| ------------------------------ | ---------------------------------------------------------------------------- |
| `Core/`, `drone.ioc`, `cmake/` | Khởi tạo và cấu hình do CubeMX quản lý                                       |
| `App/`                         | Điều phối ứng dụng, polling IMU và CRSF                                      |
| `Control/` (dự kiến)           | Lọc, quaternion, estimator, PID, allocator và mô hình motor; API độc lập HAL |
| `BSP/`                         | Ánh xạ board, SPI/CS, UART receiver, PWM và delay DWT                        |
| `Drivers/`                     | Bosch Sensor API, STM32 HAL và CMSIS                                         |
| `tests/imu/`                   | Kiểm thử trên máy tính với HAL mô phỏng                                      |
| `CMakeLists.txt`               | Source/include cho mã tự viết                                                |

Luồng gọi: `main` → `app` → `app_imu` → `bsp_bmi270` → Bosch API → callback HAL.
CubeMX khởi tạo HAL, clock và ngoại vi trước `app_init()`; vòng lặp gọi
`app_process()`. [Kiến trúc](ARCHITECTURE.md) giải thích ranh giới module và
chuỗi điều khiển dự kiến.

## Kiểm tra IMU

Cấu hình ban đầu: accel/gyro 400 Hz, ±16 g và ±2000 °/s; advanced power save
tắt. Chưa có FIFO, ngắt DRDY, hiệu chuẩn bias hoặc chuyển trục sang thân drone.
IMU và định thời chưa được kiểm chứng trên board.

Xem biến `imu` trong `App/Src/app.c` bằng debugger:

| Trường                                         | Ý nghĩa                                                                         |
| ---------------------------------------------- | ------------------------------------------------------------------------------- |
| `status`                                       | `BSP_IMU_OK`, `BSP_IMU_NO_DATA` hoặc mã lỗi                                     |
| `sample_valid`                                 | Lần poll vừa rồi có mẫu mới hợp lệ                                              |
| `sample.accel_mps2[3]`, `sample.gyro_radps[3]` | X/Y/Z theo package cảm biến; m/s² và rad/s                                      |
| `sample.sensor_time_ticks`                     | Timestamp cảm biến 24-bit, chưa mở rộng khi tràn                                |
| `sample.t_read_us`                             | Uptime MCU sau lần đọc thành công, µs, tràn uint32; không phải acquisition time |
| `sample_count`, `read_error_count`             | Số mẫu hợp lệ và lỗi đọc                                                        |
| `sensor.driver_error`                          | Mã lỗi Bosch gần nhất                                                           |

Chỉ cập nhật mẫu khi cả accel và gyro sẵn sàng. Khi chưa có mẫu mới hoặc lỗi,
dữ liệu cũ được giữ nhưng `sample_valid = false`; lần poll sau tiếp tục đọc.
Sau lỗi khởi tạo, có thể gọi lại `app_init()` để khởi tạo lại ứng dụng.

## PWM ESC

`bsp_pwm` dùng TIM4 đã được CubeMX cấu hình: clock timer 72 MHz,
prescaler 71, ARR 2499, tick 1 µs, chu kỳ 2,5 ms (400 Hz), PWM1 active high.
Kênh 1–4 tương ứng PB6–PB9; chưa gán vị trí motor trên khung drone.

`app_init()` khởi động cả bốn kênh ở 1000 µs trước khi khởi tạo IMU/receiver.
Xem `pwm_ready` trong `App/Src/app.c` bằng debugger để biết kết quả. Nếu một
kênh khởi động lỗi, BSP tắt cả bốn đầu ra và từ chối lệnh ghi; gọi lại
`bsp_pwm_init()` để thử lại. Hiện `app_process()` giữ nguyên xung tối thiểu,
chưa nối throttle RC với PWM.

API tại [BSP/Inc/bsp_pwm.h](BSP/Inc/bsp_pwm.h), chỉ gọi trong foreground
sau khởi tạo ngoại vi. Chấp nhận xung 1000–2000 µs; lệnh sai trả `false`
và giữ giá trị cũ. Xung mới áp dụng tại update timer.
`bsp_pwm_stop()` ngừng phát xung; init lại bắt đầu từ 1000 µs.
Tắt tín hiệu khác với gửi ga tối thiểu; phản ứng khi mất tín hiệu phụ thuộc ESC.

Kiểm tra dải xung/tần số theo ESC, nối chung GND và tháo cánh trước khi thử.
Cần đo PB6–PB9 trên board để xác nhận chu kỳ, độ rộng và hành vi start/stop.
Kiểm thử PWM với thanh ghi và HAL mô phỏng:

```sh
cmake -S firmware/drone/tests/pwm -B /tmp/drone-pwm-tests -G Ninja
cmake --build /tmp/drone-pwm-tests
ctest --test-dir /tmp/drone-pwm-tests --output-on-failure
```

## Khi đổi wiring

Sửa `drone.ioc`, generate lại rồi cập nhật SPI handle tại
`BSP/Src/bsp_board.c` nếu đổi bus. Giữ CS output push-pull, ban đầu High,
label `IMU_CS`; SPI master, full duplex, 8-bit, MSB first, software NSS,
mode 0 hoặc 3, clock phù hợp module. Build, chạy test rồi kiểm tra chip ID
và mẫu đo trên board.

BSP dùng SPI đã được CubeMX khởi tạo và delay DWT bật sau cấu hình clock.
API truyền đồng bộ, timeout HAL 100 ms mỗi giao dịch; chưa hỗ trợ đọc FIFO dài.
Hợp đồng sử dụng API nằm trong header `App/Inc` và `BSP/Inc`.

## Build và kiểm thử

Build firmware theo [hướng dẫn chung](../README.md#build).
Kiểm thử IMU không cần board hoặc toolchain ARM; chạy từ gốc repository:

```sh
cmake -S firmware/drone/tests/imu -B /tmp/drone-imu-tests -G Ninja
cmake --build /tmp/drone-imu-tests
ctest --test-dir /tmp/drone-imu-tests --output-on-failure
```

Test dùng driver Bosch thật và HAL mô phỏng để kiểm tra khởi tạo, đọc mẫu
và xử lý lỗi; không xác nhận nối dây hoặc định thời trên board.

## Receiver ExpressLRS

USART1 nhận CRSF ở 420000 baud, 8N1: nối TX của receiver vào PA10 (RX)
STM32 và nối chung GND; cấp nguồn receiver theo thông số của thiết bị. Chọn
serial protocol CRSF trên receiver. PA9 là TX của STM32 nhưng telemetry chưa
được triển khai.

`app_receiver_get_sample()` trả về 16 kênh CRSF thô và trạng thái `connected`;
trạng thái này chỉ đúng khi đã nhận frame RC có CRC hợp lệ trong 100 ms gần
nhất. Thứ tự kênh và ý nghĩa arm/throttle phụ thuộc cấu hình tay điều khiển.
Đây chưa phải logic arm hoặc failsafe điều khiển động cơ; hệ thống điều khiển
phải tự xử lý mất tín hiệu và kiểm tra các điều kiện an toàn trước khi chạy.

USART1 RX dùng DMA1 Channel 5 circular và ngắt IDLE/half/full. Khi nhận
liên tục ở 420 kbaud, cần phục vụ IRQ trong khoảng 0,76 ms để tránh DMA
ghi đè; mất cả vòng DMA không tự phát hiện được. Timestamp nhận dùng HAL
tick của batch, có độ trễ gom byte/IRQ và lượng tử 1 ms.

`app_receiver_poll()` giải mã trong foreground, mục tiêu không quá 2 ms
giữa các lần poll. Tràn bộ đệm hoặc lỗi UART làm bỏ dữ liệu chờ và ngắt
trạng thái kết nối đến frame RC mới. SPI IMU có thể chặn lâu khi lỗi;
bộ đệm hiện tại không bảo đảm giữ được RC trong khoảng đó. Cần đo lịch chạy
và độ trễ trên board.

Parser hỗ trợ frame RC `0x16` đủ 16 kênh, kiểm tra độ dài/CRC và timeout
3 ms theo timestamp batch. Khung khác được kiểm tra rồi bỏ qua; chưa hỗ trợ
subset `0x17`, link statistics hoặc telemetry. Kênh cũ được giữ khi mất
kết nối; `connected` phản ánh tuổi frame RC, không xác nhận chất lượng RF.
Hợp đồng API xem [App/Inc/app_receiver.h](App/Inc/app_receiver.h).

Kiểm thử CRSF và tích hợp UART với HAL mô phỏng:

```sh
cmake -S firmware/drone/tests/crsf -B /tmp/drone-crsf-tests -G Ninja
cmake --build /tmp/drone-crsf-tests
ctest --test-dir /tmp/drone-crsf-tests --output-on-failure
```

Test kiểm tra parser, DMA/buffer, timeout và phục hồi lỗi với HAL mô phỏng;
chưa xác nhận timing hoặc phần cứng.
