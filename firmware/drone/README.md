# Firmware drone

Ứng dụng hiện khởi tạo và poll BMI270 qua SPI1/CS PA4, đổi mẫu đo sang SI.
Chưa có RC, estimator, vòng điều khiển, motor output hay arm/failsafe.
Xem [cấu hình chân, build và debug](../README.md) để bắt đầu.

## Mã nguồn

| Thư mục / tệp                  | Nội dung                                         |
| ------------------------------ | ------------------------------------------------ |
| `Core/`, `drone.ioc`, `cmake/` | Khởi tạo và cấu hình do CubeMX quản lý           |
| `App/`                         | Điều phối ứng dụng, polling IMU và đổi đơn vị SI |
| `BSP/`                         | Ánh xạ board, callback SPI/CS và delay DWT       |
| `Drivers/`                     | Bosch Sensor API, STM32 HAL và CMSIS             |
| `tests/imu/`                   | Kiểm thử trên máy tính với HAL mô phỏng          |
| `CMakeLists.txt`               | Source/include cho mã tự viết                    |

Luồng gọi: `main` → `app` → `app_imu` → `bsp_bmi270` → Bosch API → callback HAL.
CubeMX khởi tạo HAL, clock và ngoại vi trước `app_init()`; vòng lặp gọi
`app_process()`. [Kiến trúc](ARCHITECTURE.md) giải thích ranh giới module và
chuỗi điều khiển dự kiến.

## Kiểm tra IMU

Cấu hình ban đầu: accel/gyro 400 Hz, ±16 g và ±2000 °/s; advanced power save
tắt. Chưa có FIFO, ngắt DRDY, hiệu chuẩn bias hoặc chuyển trục sang thân drone.
IMU và định thời chưa được kiểm chứng trên board.

Xem biến `imu` trong `App/Src/app.c` bằng debugger:

| Trường                                         | Ý nghĩa                                          |
| ---------------------------------------------- | ------------------------------------------------ |
| `status`                                       | `BSP_IMU_OK`, `BSP_IMU_NO_DATA` hoặc mã lỗi      |
| `sample_valid`                                 | Lần poll vừa rồi có mẫu mới hợp lệ               |
| `sample.accel_mps2[3]`, `sample.gyro_radps[3]` | X/Y/Z theo package cảm biến; m/s² và rad/s       |
| `sample.sensor_time_ticks`                     | Timestamp cảm biến 24-bit, chưa mở rộng khi tràn |
| `sample_count`, `read_error_count`             | Số mẫu hợp lệ và lỗi đọc                         |
| `sensor.driver_error`                          | Mã lỗi Bosch gần nhất                            |

Chỉ cập nhật mẫu khi cả accel và gyro sẵn sàng. Khi chưa có mẫu mới hoặc lỗi,
dữ liệu cũ được giữ nhưng `sample_valid = false`; lần poll sau tiếp tục đọc.
Sau lỗi khởi tạo, có thể gọi lại `app_init()` để khởi tạo lại ứng dụng.

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

Test dùng driver Bosch thật và HAL mô phỏng để kiểm tra gắn board, khởi tạo,
SPI/CS/byte giả, ODR/range, đổi đơn vị, trạng thái mẫu và lỗi giao tiếp.
Kết quả này không xác nhận nối dây hoặc định thời trên board.
