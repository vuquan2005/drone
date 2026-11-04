# Firmware drone

Ứng dụng hiện khởi tạo và poll BMI270 qua SPI1/CS PD7, đổi mẫu đo sang SI.
Đã nhận 16 kênh RC qua CRSF; chưa có estimator, vòng điều khiển, motor output
hay arm/failsafe.
Xem [cấu hình chân, build và debug](../README.md) để bắt đầu.

## Mã nguồn

| Thư mục / tệp                  | Nội dung                                         |
| ------------------------------ | ------------------------------------------------ |
| `Core/`, `drone.ioc`, `cmake/` | Khởi tạo và cấu hình do CubeMX quản lý           |
| `App/`                         | Điều phối ứng dụng, polling IMU và CRSF          |
| `BSP/`                         | Ánh xạ board, SPI/CS, UART receiver và delay DWT |
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

### Receiver ExpressLRS

USART1 nhận CRSF ở 420000 baud, 8N1: nối TX của receiver vào PA10 (RX)
STM32 và nối chung GND; cấp nguồn receiver theo thông số của thiết bị. Chọn
serial protocol CRSF trên receiver. PA9 là TX của STM32 nhưng telemetry chưa
được triển khai.

`app_receiver_get_sample()` trả về 16 kênh CRSF thô và trạng thái `connected`;
trạng thái này chỉ đúng khi đã nhận frame RC có CRC hợp lệ trong 100 ms gần
nhất. Thứ tự kênh và ý nghĩa arm/throttle phụ thuộc cấu hình tay điều khiển.
Đây chưa phải logic arm hoặc failsafe điều khiển động cơ; hệ thống điều khiển
phải tự xử lý mất tín hiệu và kiểm tra các điều kiện an toàn trước khi chạy.

USART1 RX dùng DMA1 Channel 5 circular, buffer DMA 64 byte. Ngắt IDLE và
DMA half/full chuyển phần dữ liệu mới vào bộ đệm BSP (255 byte hữu ích),
không khởi động lại DMA sau mỗi sự kiện. Hai IRQ cùng priority 5 để tránh
tranh chấp trạng thái ghi. Khi đường truyền liên tục ở 420 kbaud, DMA phát
ngắt mỗi 32 byte (khoảng 0,76 ms); cần phục vụ ngắt trong khoảng này để tránh
DMA ghi đè dữ liệu chưa sao chép. Mất cả vòng DMA không tự phát hiện được.
Timestamp là HAL tick tại sự kiện DMA/IDLE, dùng chung cho byte trong batch;
độ trễ gồm thời gian gom batch, độ trễ IRQ và lượng tử tick 1 ms.
`app_receiver_poll()` giải mã trong foreground; gọi thường xuyên, mục
tiêu không quá 2 ms giữa các lần poll. Bộ đệm đầy hoặc lỗi UART sẽ bỏ dữ liệu
đang chờ và vô hiệu trạng thái kết nối cho đến khi nhận khung RC mới. SPI IMU
có thể chặn lâu khi lỗi; bộ đệm hiện tại không bảo đảm giữ được RC trong
khoảng chặn đó. Cần đo lịch chạy và độ trễ trên board.

Parser chỉ hỗ trợ khung RC `0x16` đủ 16 kênh, kiểm tra độ dài và CRC, giới
hạn tổng thời gian ghép khung là 3 ms theo HAL tick của batch nhận (không phải timestamp chính xác từng byte). Khung khác được
kiểm tra và bỏ qua; không hỗ trợ subset `0x17`, link statistics hoặc telemetry.
Giá trị kênh cũ được giữ khi `connected = false`; trạng thái này phản ánh tuổi
khung RC, không xác nhận chất lượng liên kết RF.

Kiểm thử CRSF và tích hợp UART với HAL mô phỏng:

```sh
cmake -S firmware/drone/tests/crsf -B /tmp/drone-crsf-tests -G Ninja
cmake --build /tmp/drone-crsf-tests
ctest --test-dir /tmp/drone-crsf-tests --output-on-failure
```

Các ca kiểm tra gồm CRC sai, độ dài sai, khung thiếu, đồng bộ lại trong dữ
liệu lỗi, timeout toàn khung, tràn timestamp, tràn bộ đệm, lỗi UART và phục
hồi sau khi yêu cầu nhận thất bại, wrap DMA và callback HT đến sau IDLE. Test chạy mã parser, BSP và App thật;
HAL được mô phỏng nên chưa xác nhận timing hoặc phần cứng.

Thiết kế được đối chiếu với bản sao cục bộ của
[Betaflight CRSF](../../docs/external/repos/betaflight/src/main/rx/crsf.c)
và [ArduPilot CRSF](../../docs/external/repos/ardupilot/libraries/AP_RCProtocol/AP_RCProtocol_CRSF.cpp):
giới hạn độ dài và thời gian toàn khung, kiểm tra CRC, phục hồi đồng bộ, chỉ
cập nhật thời điểm RC khi nhận đúng khung kênh. Mã triển khai riêng cho project;
không nhập nguyên module từ hai firmware này.
