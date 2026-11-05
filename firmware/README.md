# STM32 firmware

Firmware cho STM32F103VET6 nằm trong [`drone/`](drone/README.md).
Hiện đã có khởi tạo ngoại vi, đọc BMI270, nhận CRSF và xuất PWM ESC;
chưa có mixer, điều khiển motor từ RC, arm/failsafe hay telemetry.
IMU và PWM chưa được kiểm chứng trên board.

## Cấu hình board

- STM32F103VET6: Flash 512 KiB, RAM 64 KiB; đối chiếu mã chip trước khi nạp.
- HSE thạch anh 8 MHz, PLL ×9, CPU 72 MHz; APB1 36 MHz, APB2 72 MHz.
  Board dùng nguồn clock ngoài trực tiếp cần kiểm tra HSE bypass trong CubeMX.
- CubeMX 6.18.1, STM32Cube FW_F1 V1.8.7; cấu hình tại [`drone.ioc`](drone/drone.ioc).

| Thiết bị | Tín hiệu                                    | Chân STM32            | Cấu hình                                                     |
| -------- | ------------------------------------------- | --------------------- | ------------------------------------------------------------ |
| BMI270   | SCK / MISO / MOSI                           | PB3 / PB4 / PB5       | SPI1 remap, mode 0, 8-bit, MSB first, software NSS; 2,25 MHz |
| BMI270   | CSB                                         | PD7 (`IMU_CS`)        | Output push-pull, khởi tạo High                              |
| ELRS     | TX receiver → RX MCU / RX receiver ← TX MCU | PA10 / PA9            | USART1, 420000 baud, 8N1, không flow control                 |
| ESC 1–4  | PWM                                         | PB6 / PB7 / PB8 / PB9 | TIM4_CH1–4, 400 Hz, tick 1 µs, CCR ban đầu 1000 µs           |

USART1 đã nhận CRSF bằng DMA circular và ngắt IDLE; TIM4 phát PWM 1000 µs
trên cả bốn kênh sau `app_init()`. Output 1–4 chưa gán
vị trí motor. Xác nhận chân, nguồn và mức tín hiệu theo board/module, nối chung
GND; tháo cánh trước khi thử motor. Dải xung và tốc độ PWM cần phù hợp ESC.

## Build

Mở **gốc repository** trong VS Code, dùng STM32CubeIDE extension để
import/discover `firmware/drone`, chọn preset **Debug** hoặc **Release** rồi
build target **drone** bằng CMake Tools. Workspace đã cấu hình đường dẫn project.

Nếu terminal có CMake ≥ 3.22, Ninja và `arm-none-eabi-gcc`, chạy từ gốc repository:

```sh
cd firmware/drone
cmake --preset Debug
cmake --build --preset Debug
```

Trong terminal do ST extension chuẩn bị, có thể thay `cmake` bằng `cube cmake`.
Công cụ của extension có thể chưa nằm trong `PATH` của terminal bên ngoài.
Đầu ra: `firmware/drone/build/Debug/drone.elf` tính từ gốc repository;
Release dùng thư mục tương ứng. `make clean` ở gốc xóa cả build firmware và
`report/build/`; cần configure lại sau đó.

## Debug với ST-LINK

Nối SWDIO → PA13, SWCLK → PA14, GND → GND; VTref (nếu có) → 3,3 V của
board, NRST → NRST để hỗ trợ reset phần cứng. VTref là chân tham chiếu,
không mặc định cấp nguồn board. JTAG đã tắt, SWD vẫn hoạt động.

1. Để ST extension discover project và cài các bundle GNU tools/GDB,
   ST-LINK GDB Server, STM32CubeProgrammer trước lần debug đầu tiên.
2. Launch trỏ cố định tới ELF Debug. Trước khi debug, task
   `firmware: build Debug` tự configure rồi build đúng preset **Debug**.
   Có thể chạy task riêng bằng **Terminal → Run Build Task**.
3. Chọn **STM32: Debug (ST-LINK)** trong Run and Debug, nhấn **F5**.
   Thao tác này nạp/reset board rồi dừng tại `main`.
4. Chạy qua `SystemClock_Config()`, kiểm tra `SystemCoreClock = 72000000`;
   xem [các biến IMU](drone/README.md#kiểm-tra-imu) để kiểm tra cảm biến.

Task build ở gốc repo lấy vị trí CLI bằng lệnh
`stm32cube-ide-core.get-cube-path` của extension ST rồi gọi `cube cmake`.
Không cần ghi đường dẫn cài extension hoặc sửa `PATH` trong task; CLI chọn công cụ
theo bundle của project tại thư mục làm việc `firmware/drone`.
Semihosting đang tắt; debug thông thường không cần bật tính năng này.

Gốc repo có task configure/build cho cả **Debug** và **Release**; build task tự
chạy configure tương ứng trước. `Ctrl+Shift+B` mặc định build Debug;
**Terminal → Run Task → firmware: build Release** build Release mà không nạp board.
Task in lỗi compiler ra terminal; không phụ thuộc problem matcher `$gcc` của
extension khác.

Khi mở trực tiếp `firmware/drone`, project root cũng là workspace root nên
extension nhận ngữ cảnh project và `.vscode` tại đó. Khi mở gốc repo, VS Code
không tự nạp `.vscode` của thư mục con; `cmake.sourceDirectory` chỉ định nguồn
cho CMake, không thay thế toàn bộ ngữ cảnh project của extension ST.
Nếu cần làm việc cùng nhiều thư mục, có thể thêm `firmware/drone` thành một
workspace folder riêng trong workspace nhiều root để dùng cấu hình của project.

Nếu mở trực tiếp `firmware/drone`, chọn configure/build preset **Debug** trong
CMake Tools rồi configure/build. Cấu hình launch và task ở gốc repo không tự
được nạp trong cửa sổ này. Để debug, tạo cấu hình ST-LINK bằng extension ST tại
`.vscode/launch.json` của project rồi nhấn **F5**; cấu hình do ST tạo dùng bước
build và đường dẫn binary theo project/preset đang chọn.
Muốn build Release, chọn cả configure/build preset **Release** rồi chạy Build;
chọn lại Debug trước phiên debug cần thông tin symbol đầy đủ.

<details>
<summary>Khi debug hoặc điều hướng mã gặp lỗi</summary>

- Thiếu `.settings/ide.store.json`: mở riêng `firmware/drone` trong VS Code,
  chọn STM32CubeIDE → **Discover STM32Cube project**, MCU STM32F103VE,
  toolchain GNU Tools for STM32, project `drone`, rồi **Save and close**.
  Quay lại cửa sổ gốc repository để debug.
- Không kết nối: kiểm tra nguồn, dây SWD, quyền USB/udev và firmware probe.
  Launch dùng SWD 1 MHz, software reset; muốn connect under reset cần nối NRST
  và đổi Reset mode. Dừng tại `Error_Handler()` thì kiểm tra HSE 8 MHz.
- Đọc lỗi đầu tiên trong `drone/build/Debug/gdb-client.log`;
  `gdb is not running` có thể chỉ là lỗi dọn phiên sau khi launch thất bại.
- F12 không hoạt động: configure lại Debug để tạo
  `drone/build/Debug/compile_commands.json` cho `.clangd`. Kiểm tra
  **Output → STM32Cube clangd** và `--query-driver`, rồi **Developer: Reload Window**.

</details>

Xem tiếp: [IMU và kiểm thử](drone/README.md),
[kiến trúc và hướng phát triển](drone/ARCHITECTURE.md).
