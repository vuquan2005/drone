# Firmware drone

[Kiến trúc firmware](ARCHITECTURE.md) ghi quyết định giữ `main → App → BSP → Drivers`,
ranh giới module và chuỗi điều khiển dự kiến. Phần hiện đã triển khai mới gồm
khung ứng dụng và IMU; các vòng điều khiển, RC, motor output và arm/failsafe
chưa được cài đặt.

## Phân lớp mã nguồn

```text
Core/                 Mã khởi tạo do STM32CubeMX quản lý
Drivers/BMI270/       Driver Bosch gốc, giữ nguyên
Drivers/STM32F1xx_HAL_Driver/
                      STM32 HAL, gồm phần SPI từ STM32Cube FW_F1 V1.8.7
BSP/Inc, BSP/Src/     SPI/CS, delay micro giây, cấu hình board và BMI270
App/Inc, App/Src/     Điều phối ứng dụng, đọc IMU và đổi đơn vị SI
tests/imu/            Kiểm thử trên máy tính với SPI HAL mô phỏng
```

Luồng gọi: `main.c` → `app.c` → `app_imu` → `bsp_bmi270` → Bosch Sensor API →
callback SPI dùng STM32 HAL. `main.c` giữ phần khởi tạo HAL, clock và peripheral
do CubeMX sinh; sau đó chỉ gọi `app_init()` và `app_process()` trong các vùng
`USER CODE`, để CubeMX giữ lại khi sinh mã. `App/Src/app.c` sở hữu trạng thái
IMU và điều phối các module ứng dụng. Firmware chỉ có một hàm `main()`.

Chưa cần thư mục `Lib` hoặc `ThirdParty`: driver ngoài đã nằm trong `Drivers`.

## Trạng thái hiện tại

Giao tiếp BMI270 dùng SPI. Chưa gán chân hoặc chọn SPI1/SPI2/SPI3;
`bsp_board_imu_config()` trả về `NULL`, nên ứng dụng giữ trạng thái
`BSP_IMU_NOT_CONFIGURED` và không truy cập SPI/GPIO của cảm biến.
Có thể hoàn thiện sơ đồ nối dây sau mà không đổi phần App hay driver Bosch.

Khi board được cấu hình, BSP tải mảng cấu hình Bosch, tắt advanced power save,
cấu hình cả accelerometer và gyroscope ở 400 Hz, dải đo ±16 g và ±2000 °/s,
rồi bật hai cảm biến. Đây là cấu hình ban đầu để kiểm tra cảm biến.
Chưa có FIFO, ngắt DRDY, hiệu chuẩn bias, bộ lọc bổ sung hay biến đổi trục
từ cảm biến sang thân drone.

## Gán bus và chân sau

1. Trong `drone.ioc`, bật SPI dùng cho BMI270: master, full duplex, 8-bit,
   MSB first, software NSS; dùng mode 0 (CPOL low, first edge) hoặc mode 3
   (CPOL high, second edge). Chọn clock phù hợp với BMI270 và module thực tế.
2. Gán SCK/MISO/MOSI. Tạo một GPIO output cho CS, đặt tên `IMU_CS`,
   mức khởi tạo high. Kết nối nguồn và GND theo module thực tế.
3. Generate Code. Đảm bảo hàm khởi tạo SPI chạy trước `app_init()`.
4. Thay nội dung `BSP/Src/bsp_board.c` bằng cấu hình thực tế. Ví dụ sau
   **chỉ áp dụng nếu bạn đã chọn SPI1 và tên GPIO là `IMU_CS`**:

```c
#include "bsp_board.h"
#include "bsp_time.h"
#include "main.h"

extern SPI_HandleTypeDef hspi1; /* Handle do CubeMX sinh. */

const bsp_bmi270_config_t *bsp_board_imu_config(void) {
    static const bsp_bmi270_config_t config = {
        .spi = &hspi1,
        .cs_port = IMU_CS_GPIO_Port,
        .cs_pin = IMU_CS_Pin,
        .timeout_ms = 100U,
        .delay_us = bsp_time_delay_us,
    };
    bsp_time_init();
    return &config;
}
```

`bsp_time` dùng bộ đếm DWT trên Cortex-M3. Gọi init sau khi cấu hình clock;
không tắt bộ đếm trong khi sử dụng delay. Có thể cung cấp hàm delay micro giây
khác qua `config.delay_us`. Không truyền trực tiếp `HAL_Delay` vì nó dùng mili giây.

Bus SPI phải được khởi tạo trước và chỉ được một luồng thực thi sử dụng tại
một thời điểm. API hiện dùng truyền đồng bộ, không gọi từ interrupt.
Mỗi giao dịch giữ CS low suốt địa chỉ và dữ liệu, rồi nhả CS cả khi HAL báo lỗi.
BSP giữ byte giả SPI cho Bosch xử lý. Bộ đệm hiện hỗ trợ truy cập thanh ghi và
tải cấu hình theo khối 32 byte; chưa hỗ trợ đọc FIFO dài.

SPI HAL được bật và đăng ký source trong `CMakeLists.txt` cấp project để
build được trước khi chọn chân; phần CMake do CubeMX sinh được giữ nguyên.

## Dùng mẫu IMU

Biến `imu` trong `App/Src/app.c` có thể xem trong debugger:

- `status`: `BSP_IMU_OK` khi vừa đọc mẫu, `BSP_IMU_NO_DATA` khi chưa có đủ
  dữ liệu mới, hoặc mã lỗi cấu hình/driver.
- `sample_valid`: chỉ true khi lần polling vừa rồi có mẫu mới hợp lệ.
- `sample.accel_mps2[3]`, `sample.gyro_radps[3]`: trục X/Y/Z theo package
  cảm biến, đơn vị m/s² và rad/s.
- `sample.sensor_time_ticks`: bộ đếm 24-bit của cảm biến, chưa mở rộng khi tràn.
- `sample_count`, `read_error_count`: số mẫu hợp lệ và số lỗi đọc.
- `sensor.driver_error`: mã lỗi Bosch gần nhất, giúp phân biệt sai chip ID,
  lỗi tải cấu hình và lỗi giao tiếp.

App chỉ cập nhật mẫu khi cả accel và gyro đã sẵn sàng. Khi lỗi hoặc chưa có
mẫu mới, dữ liệu cũ vẫn được giữ nhưng `sample_valid` là false. Sau lỗi đọc,
lần polling tiếp theo tiếp tục thử đọc; có thể gọi lại `app_init()` để khởi tạo
lại toàn bộ ứng dụng sau lỗi khởi tạo.
Giữ đối tượng `app_imu_t` ở địa chỉ cố định vì callback Bosch tham chiếu vào nó.
Việc kiểm tra định thời và điều khiển bay sẽ cần thêm bộ lập lịch phù hợp.

## Build và kiểm thử

Build firmware theo [hướng dẫn chung](../README.md).
Chạy kiểm thử trên máy tính, không cần board hoặc toolchain ARM:

```sh
cmake -S firmware/drone/tests/imu -B /tmp/drone-imu-tests -G Ninja
cmake --build /tmp/drone-imu-tests
ctest --test-dir /tmp/drone-imu-tests --output-on-failure
```

Các lệnh trên chạy từ root repository. Kiểm thử dùng driver Bosch thật và
mô phỏng HAL SPI để kiểm tra tải cấu hình, byte giả, CS, dải đo/ODR,
đổi đơn vị có dấu, mẫu chưa sẵn sàng và các lỗi truyền dữ liệu/khởi tạo.
Kiểm thử mô phỏng không xác nhận được nối dây hoặc định thời trên board.
