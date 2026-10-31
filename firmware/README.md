# STM32 firmware

Project CubeMX hiện nằm trong `drone/`. Giữ cấu trúc do CubeMX sinh ra; mở `drone/drone.ioc` để thay đổi cấu hình phần cứng. Mã IMU được chia thành `App`, `BSP` và driver Bosch trong `Drivers/BMI270`; xem [hướng dẫn BMI270 qua SPI](drone/README.md) để gán bus và chân CS sau.

## Project configuration

- MCU: STM32F103VET6, Cortex-M3; linker script khai báo Flash 512 KiB và RAM 64 KiB. Kiểm tra mã chip trên board thực tế trước khi nạp.
- Generator: STM32CubeMX 6.18.1, STM32Cube FW_F1 V1.8.7, target CMake.
- Preset: `Debug` và `Release`, dùng Ninja và GNU Arm Embedded (`arm-none-eabi-gcc`).
- Clock hiện tại: HSE 8 MHz, PLL x9, SYSCLK/HCLK 72 MHz; APB1 36 MHz, APB2 72 MHz, Flash latency 2. Cấu hình `HSE_ON` yêu cầu thạch anh ngoài phù hợp; nếu board dùng nguồn clock ngoài cấp trực tiếp thì cần kiểm tra lựa chọn HSE bypass trong CubeMX. Không có HSE hoạt động sẽ khiến `SystemClock_Config()` đi vào `Error_Handler()`.
- SYS Debug hiện tại: `Serial Wire`, PA13 là SWDIO, PA14 là SWCLK. Mã khởi tạo tắt JTAG và giữ SWD bằng `__HAL_AFIO_REMAP_SWJ_NOJTAG()`.

### Chân và ngoại vi đã cấu hình

| Thiết bị       | Tín hiệu             | Chân STM32            | Cấu hình                                                                   |
| -------------- | -------------------- | --------------------- | -------------------------------------------------------------------------- |
| BMI270         | SCK / MISO / MOSI    | PA5 / PA6 / PA7       | SPI1 master, full duplex, 8-bit, MSB first, mode 0, software NSS; 2,25 MHz |
| BMI270         | CSB                  | PA4 (`IMU_CS`)        | GPIO output push-pull, mức khởi tạo High                                   |
| ELRS UART      | TX receiver → RX MCU | PA10                  | USART1_RX, 420000 baud, 8N1                                                |
| ELRS UART      | RX receiver ← TX MCU | PA9                   | USART1_TX, không flow control                                              |
| ESC output 1–4 | PWM signal           | PB6 / PB7 / PB8 / PB9 | TIM4_CH1 / CH2 / CH3 / CH4                                                 |

TIM4 dùng clock 72 MHz, prescaler 71 và ARR 19999: tick 1 µs, chu kỳ 20 ms
(50 Hz). Bốn kênh PWM mode 1, polarity High có CCR khởi tạo 1000 (xung 1000 µs
nếu được start). Đây là cấu hình thử cho ESC PWM thông thường; xác nhận dải
xung, tốc độ cập nhật và mức tín hiệu theo model ESC trước khi sử dụng.
Output 1–4 chưa quy định vị trí motor trên khung drone.

Firmware hiện chỉ khởi tạo ngoại vi, chưa gọi `HAL_TIM_PWM_Start`, chưa bắt
đầu nhận UART và chưa có driver BMI270, bộ giải mã CRSF, điều khiển motor hoặc
arm/failsafe trong nhánh này. Generate Code không tự cung cấp các chức năng đó.
Kiểm tra sơ đồ board để xác nhận chân khả dụng và điện áp nguồn của từng module;
nối chung GND tín hiệu. Khi thử motor, tháo cánh trước khi cấp nguồn ESC.

## Build

Từ thư mục gốc repository, `make clean` xóa toàn bộ `firmware/drone/build/` (Debug, Release và cache CMake) cùng `report/build/`. Lần build tiếp theo cần configure lại CMake. Build và debug firmware bằng VS Code theo hướng dẫn bên dưới.

Mở thư mục gốc `drone/` của repository trong VS Code. Workspace Settings đã trỏ CMake Tools đến `firmware/drone` và dùng các preset có sẵn; không tự configure khi chỉ mở workspace để viết báo cáo.

Trong STM32CubeIDE for VS Code, import/discover project CubeMX và kiểm tra các tool bundle được extension yêu cầu. Sau đó chọn configure preset `Debug` hoặc `Release` và build bằng CMake Tools. Hai cách mở thư mục đều dùng `cube-cmake` và các bundle ST đã chọn. Extension quản lý công cụ riêng; compiler có thể chưa xuất hiện trong terminal bên ngoài VS Code.

Trong terminal do ST extension chuẩn bị, có thể dùng launcher để chọn công cụ theo metadata của project:

```sh
cd firmware/drone
cube cmake --preset Debug
cube cmake --build --preset Debug
```

Nếu CMake (từ 3.22), Ninja và GNU Arm Embedded đã có trong PATH của terminal:

```sh
cd firmware/drone
cmake --preset Debug
cmake --build --preset Debug
```

Đầu ra dự kiến: `drone/build/Debug/drone.elf`, tính từ thư mục `firmware/`. Preset Release tạo đầu ra tương ứng trong `drone/build/Release/`.

## Debug with ST-LINK

Workspace đã có cấu hình `STM32: Debug (ST-LINK)` trong `../.vscode/launch.json`, sử dụng debugger của ST extension pack (`stlinkgdbtarget`). Cấu hình nạp ELF Debug và dừng tại `main`; đây là thao tác nạp/reset chương trình trên board khi bạn nhấn F5.

**Bắt buộc setup project bằng ST extension trước lần debug đầu tiên.** Build CMake thành công chưa thay thế bước này. Nếu log báo thiếu `firmware/drone/.settings/ide.store.json`, mở riêng `firmware/drone` bằng một cửa sổ VS Code, chọn biểu tượng STM32CubeIDE → **Discover STM32Cube project**, chọn MCU STM32F103VE tương ứng, toolchain GNU Tools for STM32 và project `drone`, rồi **Save and close**. Để extension sinh metadata `.settings/ide.store.json`; sau đó quay lại cửa sổ mở root repository, chọn preset Debug và chạy launch ST-LINK. Thông báo `gdb is not running` có thể là lỗi xuất hiện khi dọn phiên đã thất bại trước đó; đọc lỗi `launch` đầu tiên trong `build/Debug/gdb-client.log` để tìm nguyên nhân.

Nối theo tên tín hiệu của ST-LINK và board, không suy ra số chân trên đầu nối khi chưa biết loại probe:

| ST-LINK                                    | STM32F103VE                             |
| ------------------------------------------ | --------------------------------------- |
| SWDIO                                      | PA13                                    |
| SWCLK                                      | PA14                                    |
| GND                                        | GND                                     |
| VTref / Target voltage sense, nếu probe có | Đường 3.3 V của board                   |
| NRST, nếu probe có                         | NRST, nên nối để hỗ trợ reset phần cứng |

Cấp nguồn board đúng thiết kế; chân VTref là tham chiếu điện áp, không mặc định là nguồn cấp cho board. Cấu hình đang dùng software reset và SWD 1 MHz; khi cần connect under reset, phải nối NRST và đổi Reset mode trong cấu hình debug.

1. Mở root repository trong VS Code; để STM32 extension import/discover project `firmware/drone`. Kiểm tra các bundle GNU tools/GDB, ST-LINK GDB Server và STM32CubeProgrammer đã được tải/kích hoạt.
2. Chọn CMake configure preset **Debug** và build target **drone**. Chạy build thành công trước khi debug. Chọn Debug mỗi lần dùng launch này: lệnh `preBuild` dùng preset đang chọn trong extension, còn đường dẫn ELF của launch trỏ cố định vào Debug.
3. Cắm ST-LINK, kiểm tra probe xuất hiện trong **STM32CUBE DEVICES AND BOARDS**. Trên Linux, cài udev rules do ST cung cấp nếu gặp lỗi quyền USB.
4. Trong **Run and Debug** (`Ctrl+Shift+D`), chọn **STM32: Debug (ST-LINK)**, rồi nhấn **F5**.
5. Khi dừng ở `main`, dùng F10 để step over; F11 để step into; F9 để đặt breakpoint. Có thể đặt breakpoint tại lời gọi `MX_GPIO_Init()` và `Error_Handler()`, rồi xem `SystemCoreClock` và `uwTick` trong Watch. Sau `SystemClock_Config()`, `SystemCoreClock` dự kiến bằng `72000000`.

Preset Debug dùng `-O0 -g3`; Release dùng `-Os -g0` nên không phù hợp để bắt đầu debug theo dòng nguồn. Vòng lặp hiện còn trống; SPI1, USART1 và TIM4 đã được khởi tạo nhưng chưa có logic ứng dụng hay lệnh bắt đầu PWM/nhận UART, nên chưa có tín hiệu đầu ra ứng dụng là bình thường.
Preset Debug dùng `-O0 -g3`; Release dùng `-Os -g0` nên không phù hợp để bắt đầu debug theo dòng nguồn. Vòng lặp gọi App IMU, nhưng chưa gán bus/chân BMI270 nên trạng thái hiện tại là `BSP_IMU_NOT_CONFIGURED`. Chưa cấu hình LED/UART/PWM, nên chưa có tín hiệu đầu ra ứng dụng là bình thường.

Nếu không kết nối được, kiểm tra nguồn, GND chung, SWDIO/SWCLK, quyền USB và probe firmware. Nếu dừng tại `Error_Handler()`, kiểm tra HSE 8 MHz trên board. Nếu không tìm thấy ELF, kiểm tra đã build đúng preset Debug. Không cần thêm Cortex-Debug để dùng launch của ST này.

Đã kiểm tra build Debug bằng GNU tools for STM32 14.3.1+st.2 trong bundle cục bộ: tạo được ELF cho cả Debug và Release. Debug dùng Flash 9324 B, Release dùng Flash 5464 B; cả hai dùng vùng RAM 1816 B (gồm các vùng dự trữ do linker script khai báo). Log phiên debug của người dùng lúc 20:42 ngày 17/10/2026 xác nhận đã kết nối và dừng tại `main` ở lệnh `HAL_Init()`; điều này chưa xác nhận chương trình đã chạy qua bước cấu hình HSE/PLL.
Đã kiểm tra build Debug bằng GNU tools for STM32 14.3.1+st.2 trong bundle cục bộ: tạo được ELF, Flash dùng 3836 B và vùng RAM dùng 1584 B (gồm các vùng dự trữ do linker script khai báo). Log phiên debug của người dùng lúc 20:42 ngày 17/10/2026 xác nhận đã kết nối và dừng tại `main` ở lệnh `HAL_Init()`; điều này chưa xác nhận chương trình đã chạy qua bước cấu hình HSE/PLL.

## Telemetry qua UART DMA (dự kiến)

Có thể dùng UART DMA để truyền telemetry về PC, lưu CSV và vẽ biểu đồ/phân tích bằng công cụ khác; ST-LINK phục vụ nạp và debug, không cần đảm nhiệm toàn bộ việc thu dữ liệu. Firmware hiện chưa triển khai telemetry.

- Trong vòng điều khiển, chụp một mẫu nhị phân có timestamp từ MCU và số thứ tự vào bộ đệm RAM; truyền bằng DMA ở nền, không chờ UART hoặc dùng `printf` blocking. Không ghi đè vùng nhớ DMA đang đọc; khi đầy bộ đệm, đếm mẫu bị bỏ thay vì chặn vòng điều khiển.
- Chọn baud theo kích thước mẫu và tần số log: UART 8N1 cần khoảng `10 × byte/mẫu × mẫu/giây` bit/s, cộng dư địa. Ví dụ, **nếu dùng 115200 baud**, trần lý thuyết là 11520 byte/s; mẫu 40 byte ở 100 Hz dùng 4000 byte/s, còn ở 1 kHz vượt băng thông. DMA giảm tải CPU nhưng không tăng băng thông UART; cần đo jitter và số mẫu mất khi bật log.

## CubeMX và mã tự viết

Mở `drone/drone.ioc` trong CubeMX để thay đổi cấu hình. Trong các tệp do CubeMX quản lý, đặt mã tự viết trong vùng `USER CODE BEGIN/END` để giữ lại khi sinh mã. Với module riêng, thêm source và include vào `drone/CMakeLists.txt`; kiểm tra thay đổi sau khi Generate Code.

Quy tắc format C/C++ dùng chung nằm tại `../.clang-format`. Các thư mục mã CubeMX sinh ra được loại khỏi format để tránh thay đổi không cần thiết.

Tham khảo: [STM32CubeMX](https://dev.st.com/stm32cube-docs/stm32cubemx/) và [hướng dẫn debug của ST](https://dev.st.com/stm32cube-docs/stm32cubeide-vscode/latest/en/docs/markup/development/debug.html).
