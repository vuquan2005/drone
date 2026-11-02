# Firmware drone

[Kiến trúc firmware](ARCHITECTURE.md) ghi quyết định giữ `main → App → BSP → Drivers`,
ranh giới module và chuỗi điều khiển dự kiến. Phần hiện đã triển khai mới gồm
khung ứng dụng và IMU; các vòng điều khiển, RC, motor output và arm/failsafe
chưa được cài đặt.

## Phân lớp mã nguồn

```text
firmware/drone/
├── Core/                     # Khởi tạo, startup hooks và ngắt do CubeMX quản lý
├── App/
│   ├── Inc/                  # API ứng dụng và kiểu mẫu IMU
│   └── Src/                  # Điều phối, polling và đổi đơn vị SI
├── BSP/
│   ├── Inc/                  # API board, bus cảm biến và delay
│   └── Src/                  # Ánh xạ SPI/CS, callback Bosch và timing
├── Drivers/
│   ├── BMI270/               # Bosch Sensor API gốc
│   ├── CMSIS/                # Định nghĩa core và thiết bị STM32
│   └── STM32F1xx_HAL_Driver/  # STM32 HAL, gồm SPI từ FW_F1 V1.8.7
├── cmake/                    # Toolchain và phần CMake do CubeMX sinh
├── tests/imu/                # Test trên máy tính với HAL mô phỏng
├── drone.ioc                 # Nguồn cấu hình ngoại vi trong CubeMX
├── CMakeLists.txt            # Đăng ký source/include của mã tự viết
├── CMakePresets.json         # Preset Debug và Release
├── .clangd                   # Chọn compile database cho editor
├── README.md                 # Bắt đầu, cấu hình board, build và test
└── ARCHITECTURE.md           # Ranh giới module và quy ước phát triển
```

Luồng gọi: `main.c` → `app.c` → `app_imu` → `bsp_bmi270` → Bosch Sensor API →
callback SPI dùng STM32 HAL. `main.c` giữ phần khởi tạo HAL, clock và peripheral
do CubeMX sinh; sau đó chỉ gọi `app_init()` và `app_process()` trong các vùng
`USER CODE`, để CubeMX giữ lại khi sinh mã. `App/Src/app.c` sở hữu trạng thái
IMU và điều phối các module ứng dụng. Firmware chỉ có một hàm `main()`.

Chưa cần thư mục `Lib` hoặc `ThirdParty`: driver ngoài đã nằm trong `Drivers`.

## Trạng thái hiện tại

BSP đã gắn BMI270 với SPI1: PA5 SCK, PA6 MISO, PA7 MOSI và PA4
`IMU_CS`. SPI dùng mode 0, 8-bit, MSB first, software NSS, SCK 2,25 MHz.
CubeMX khởi tạo CS High và SPI trước `app_init()`; `bsp_board_imu_config()`
bật delay DWT rồi trả cấu hình tĩnh với timeout HAL 100 ms mỗi giao dịch.
Ứng dụng sẽ thử khởi tạo IMU ngay khi chạy. Chưa kiểm chứng trên board.

Khi khởi tạo, BSP tải mảng cấu hình Bosch, tắt advanced power save,
cấu hình cả accelerometer và gyroscope ở 400 Hz, dải đo ±16 g và ±2000 °/s,
rồi bật hai cảm biến. Đây là cấu hình ban đầu để kiểm tra cảm biến.
Chưa có FIFO, ngắt DRDY, hiệu chuẩn bias, bộ lọc bổ sung hay biến đổi trục
từ cảm biến sang thân drone.

## Cấu hình board và thay đổi wiring

Ánh xạ đang sử dụng nằm trong `BSP/Src/bsp_board.c`; tên `IMU_CS` lấy từ
`Core/Inc/main.h` do CubeMX sinh. BSP dùng `hspi1` đã có, không khởi tạo lại SPI.
Nếu đổi bus hoặc chân:

1. Sửa `drone.ioc`, giữ CS là output push-pull, mức khởi tạo High và label
   `IMU_CS`. Chọn SPI master, full duplex, 8-bit, MSB first, software NSS,
   mode 0 hoặc 3, clock phù hợp module và dây nối.
2. Generate Code, kiểm tra diff và bảo đảm init ngoại vi vẫn chạy trước App.
3. Nếu đổi SPI instance, cập nhật handle trong `bsp_board.c`.
4. Build và chạy test; sau đó kiểm tra chip ID, trạng thái khởi tạo và mẫu đo
   trên board trước khi dùng dữ liệu cho điều khiển.

`bsp_time` dùng bộ đếm DWT trên Cortex-M3. Gọi init sau khi cấu hình clock;
không tắt bộ đếm trong khi sử dụng delay. Có thể cung cấp hàm delay micro giây
khác qua `config.delay_us`. Không truyền trực tiếp `HAL_Delay` vì nó dùng mili giây.

Bus SPI phải được khởi tạo trước và chỉ được một luồng thực thi sử dụng tại
một thời điểm. API hiện dùng truyền đồng bộ, không gọi từ interrupt.
Mỗi giao dịch giữ CS low suốt địa chỉ và dữ liệu, rồi nhả CS cả khi HAL báo lỗi.
BSP giữ byte giả SPI cho Bosch xử lý. Bộ đệm hiện hỗ trợ truy cập thanh ghi và
tải cấu hình theo khối 32 byte; chưa hỗ trợ đọc FIFO dài.

SPI HAL được bật bởi `Core/Inc/stm32f1xx_hal_conf.h` và source được đăng ký
trong `cmake/stm32cubemx/CMakeLists.txt` do CubeMX quản lý. CMake cấp project
chỉ thêm source/include cho Bosch, BSP và App.

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
Nếu GNU Arm Embedded, CMake và Ninja đã có trong `PATH`, chạy từ root repository:

```sh
cmake -S firmware/drone --preset Debug
cmake --build firmware/drone/build/Debug
```

Trong VS Code với STM32 extension, chọn configure preset **Debug**, chạy
**CMake: Configure**, rồi build target **drone**. Cấu hình đường dẫn tool bundle
được mô tả trong [hướng dẫn chung](../README.md#configuration-ownership).

### Điều hướng mã bằng clangd

`CMakeLists.txt` đã bật xuất compile database; bước configure tạo
`build/Debug/compile_commands.json`. File `.clangd` trỏ tới thư mục này để lấy
include paths, định nghĩa macro và lệnh biên dịch ARM. Chỉ cần configure để
cung cấp dữ liệu cho editor, không cần build toàn bộ firmware.

Sau `make clean` ở root repository, thư mục build và compile database bị xóa.
Chạy lại configure preset **Debug**. Nếu F12 vẫn không tìm được định nghĩa,
mở **Output → STM32Cube clangd**, kiểm tra database đã được nạp và đường dẫn
`--query-driver` khớp compiler trong database; sau đó chạy
**Developer: Reload Window** để khởi động lại dịch vụ và chờ index.
Không chỉnh trực tiếp `compile_commands.json` hoặc commit file này.

### Kiểm thử IMU trên máy tính

Chạy kiểm thử trên máy tính, không cần board hoặc toolchain ARM:

```sh
cmake -S firmware/drone/tests/imu -B /tmp/drone-imu-tests -G Ninja
cmake --build /tmp/drone-imu-tests
ctest --test-dir /tmp/drone-imu-tests --output-on-failure
```

Các lệnh trên chạy từ root repository. Kiểm thử dùng driver Bosch thật và
mô phỏng HAL SPI và delay để kiểm tra ánh xạ board theo header CubeMX,
thứ tự bật delay trước init cảm biến, tải cấu hình, byte giả, CS, dải đo/ODR,
đổi đơn vị có dấu, mẫu chưa sẵn sàng và các lỗi truyền dữ liệu/khởi tạo.
Kiểm thử mô phỏng không xác nhận được nối dây hoặc định thời trên board.

## Khi bổ sung hoặc thay đổi mã

Đọc [quy ước phát triển](ARCHITECTURE.md#quy-ước-phát-triển) trước khi thêm
module. Đặt hợp đồng API bằng Doxygen trong header `App/Inc` và `BSP/Inc`;
comment trong source giải thích quyết định hoặc ràng buộc khó thấy từ code.
Giữ hướng dẫn thao tác ở README và quyết định kiến trúc ở ARCHITECTURE.
Hiện đọc Doxygen trực tiếp trong header; chưa có bước sinh website API.
