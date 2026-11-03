# BMI270 Sensor API

Driver gốc của Bosch Sensortec, sao chép nguyên trạng từ
`docs/external/repos/BMI270_SensorAPI` trong repository này.

- Upstream: https://github.com/boschsensortec/BMI270_SensorAPI
- Commit nguồn: `41129fcfe39c583ee5462d79195741945d51c1fe`
- Phiên bản ghi trong header: `v2.86.1`
- Giấy phép: BSD-3-Clause, xem `LICENSE` và phần bản quyền trong từng tệp.

## Các tệp được sử dụng

| Tệp | Vai trò |
| --- | --- |
| `bmi2.c`, `bmi2.h`, `bmi2_defs.h` | API chung, kiểu dữ liệu và định nghĩa thanh ghi BMI2 |
| `bmi270.c`, `bmi270.h` | API BMI270 tiêu chuẩn và mảng cấu hình cảm biến nhúng sẵn |

`firmware/drone/CMakeLists.txt` đã thêm hai tệp `.c` và đường dẫn include
vào target `drone`. Mã firmware có thể dùng `#include "bmi270.h"`.
Không cần lấy mã từ thư mục `docs` khi build.

Các biến thể context, legacy, DSD, maximum FIFO, giao tiếp OIS và ví dụ
phụ thuộc board Bosch COINES không nằm trong bộ driver tiêu chuẩn này.

## Kết nối với STM32

Lớp `BSP/Src/bsp_bmi270.c` đã cung cấp callback SPI dùng STM32 HAL,
khởi tạo và cấu hình cảm biến. `App/Src/app_imu.c` đọc mẫu và đổi đơn vị SI.
`main.c` đã gọi App trong các vùng `USER CODE`.

BSP đã gắn SPI1 và CS PA4 tại `BSP/Src/bsp_board.c`.
Xem [kiểm tra IMU](../../README.md#kiểm-tra-imu) để theo dõi mẫu và trạng thái lỗi;
IMU chưa được kiểm chứng trên board.
