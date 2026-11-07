# Firmware drone

Project STM32F103VET6 này hiện có khung ứng dụng và đọc BMI270;
nhận RC qua CRSF và xuất PWM ESC qua TIM4; mixer, điều khiển motor từ RC
và arm/failsafe chưa triển khai.

## Kiến trúc cơ bản

Luồng gọi hiện tại: `Core/main` → `App` → `BSP` → driver → callback HAL.
`main()` khởi tạo HAL, clock, ngoại vi rồi gọi `app_init()`; vòng lặp gọi
`app_process()`.

| Thành phần                     | Trách nhiệm                                                  |
| ------------------------------ | ------------------------------------------------------------ |
| `Core/`, `drone.ioc`, `cmake/` | Khởi tạo và cấu hình do CubeMX quản lý                       |
| `App/`                         | Trạng thái ứng dụng, điều phối module, đọc IMU và đổi đơn vị |
| `BSP/`                         | Ánh xạ board, SPI/CS, delay và tích hợp driver với HAL       |
| `Drivers/`                     | Sensor API, STM32 HAL và CMSIS                               |
| `tests/`                       | Kiểm thử trên máy tính, HAL mô phỏng tách khỏi firmware      |
| `control/` ở gốc repo          | Thuật toán điều khiển và ước lượng độc lập phần cứng         |

Giữ `app.c` làm nơi điều phối; thuật toán tính toán nhận mẫu, trạng thái và
`dt` qua API, không phụ thuộc HAL/Bosch. BSP không gọi ngược App hoặc chứa
thuật toán điều khiển bay.

## CubeMX và file `.ioc`

- Giữ `drone.ioc` đồng bộ với mã khởi tạo; ưu tiên đổi cấu hình qua CubeMX.
  Khi sửa `.ioc`, chạy STM32CubeMX
  sinh lại mã khởi tạo; nếu không chạy được, báo người dùng thực hiện.

## Coding style

- Format code: C dùng 4 dấu cách, không tab; để formatter quyết định cách xuống dòng và đặt ngoặc.
  Chỉ format mã tự viết, tránh đổi định dạng mã CubeMX và driver upstream.
- Theo cách đặt tên hiện có: tệp, hàm và biến dùng `snake_case`, kiểu tự định
  nghĩa có hậu tố `_t`, macro và giá trị enum dùng `UPPER_SNAKE_CASE`.
  API module mang tiền tố tương ứng như `app_imu_`, `bsp_bmi270_`.
- Khai báo API công khai trong `Inc/`, triển khai trong `Src/`; hàm và trạng
  thái chỉ dùng nội bộ tệp để `static`. Dùng `const` cho dữ liệu chỉ đọc và
  kiểu số nguyên có độ rộng rõ ràng cho thanh ghi, giao thức và bộ đếm.
- Ghi đơn vị trong tên khi cần, như `_us`, `_ms`, `_radps`; mô tả hệ trục,
  nguồn timestamp và cách xử lý tràn trong hợp đồng dữ liệu.
- Viết Doxygen tiếng Anh trong header cho API công khai: hành vi, tham số,
  điều kiện gọi, kết quả và dữ liệu khi lỗi; nêu ràng buộc con trỏ, thời gian
  sống, blocking/ISR khi có. Comment `.c` giải thích lý do hoặc trình tự bắt
  buộc, tránh diễn lại code hay chép hợp đồng từ header. Markdown dùng tiếng Việt.
