# STM32 firmware

Project CubeMX hiện nằm trong `drone/`. Giữ cấu trúc do CubeMX sinh ra; mở `drone/drone.ioc` để thay đổi cấu hình phần cứng.

## Project configuration

- MCU: STM32F103VET6, Cortex-M3; linker script khai báo Flash 512 KiB và RAM 64 KiB. Kiểm tra mã chip trên board thực tế trước khi nạp.
- Generator: STM32CubeMX 6.18.1, STM32Cube FW_F1 V1.8.7, target CMake.
- Preset: `Debug` và `Release`, dùng Ninja và GNU Arm Embedded (`arm-none-eabi-gcc`).
- Clock hiện tại: HSE 8 MHz, PLL x9, SYSCLK/HCLK 72 MHz; APB1 36 MHz, APB2 72 MHz, Flash latency 2. Cấu hình `HSE_ON` yêu cầu thạch anh ngoài phù hợp; nếu board dùng nguồn clock ngoài cấp trực tiếp thì cần kiểm tra lựa chọn HSE bypass trong CubeMX. Không có HSE hoạt động sẽ khiến `SystemClock_Config()` đi vào `Error_Handler()`.
- SYS Debug hiện tại: `Serial Wire`, PA13 là SWDIO, PA14 là SWCLK. Mã khởi tạo tắt JTAG và giữ SWD bằng `__HAL_AFIO_REMAP_SWJ_NOJTAG()`.

## Build

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

Preset Debug dùng `-O0 -g3`; Release dùng `-Os -g0` nên không phù hợp để bắt đầu debug theo dòng nguồn. Vòng lặp hiện còn trống và chưa cấu hình LED/UART/PWM, nên chưa có tín hiệu đầu ra ứng dụng là bình thường.

Nếu không kết nối được, kiểm tra nguồn, GND chung, SWDIO/SWCLK, quyền USB và probe firmware. Nếu dừng tại `Error_Handler()`, kiểm tra HSE 8 MHz trên board. Nếu không tìm thấy ELF, kiểm tra đã build đúng preset Debug. Không cần thêm Cortex-Debug để dùng launch của ST này.

Đã kiểm tra build Debug bằng GNU tools for STM32 14.3.1+st.2 trong bundle cục bộ: tạo được ELF, Flash dùng 3836 B và vùng RAM dùng 1584 B (gồm các vùng dự trữ do linker script khai báo). Log phiên debug của người dùng lúc 20:42 ngày 17/10/2026 xác nhận đã kết nối và dừng tại `main` ở lệnh `HAL_Init()`; điều này chưa xác nhận chương trình đã chạy qua bước cấu hình HSE/PLL.

## Configuration ownership

| File                                      | Vai trò và cách quản lý                                        |
| ----------------------------------------- | -------------------------------------------------------------- |
| `drone/drone.ioc`                         | Cấu hình phần cứng; chỉnh bằng CubeMX                          |
| `drone/CMakePresets.json`                 | Nguồn định nghĩa preset Debug/Release và thư mục đầu ra        |
| `drone/.settings/ide.store.json`          | Nhận diện MCU/core/toolchain cho ST extension; giữ trong Git   |
| `drone/.settings/bundles.store.json`      | Chọn phiên bản các tool bundle; giữ trong Git                  |
| `drone/.settings/bundles-lock.store.json` | Khóa các bundle đã giải quyết cho từng nền tảng; giữ trong Git |
| `../.vscode/settings.json`                | Cấu hình khi mở root repository; có cả firmware và LaTeX       |
| `drone/.vscode/settings.json`             | Cấu hình khi mở riêng firmware; giữ phần ST đồng bộ với root   |
| `../.vscode/launch.json`                  | Launch ST-LINK dùng khi mở root repository                     |
| `drone/.clangd`                           | Chọn compile database tại `build/Debug`                        |

VS Code không tự áp dụng `.vscode/settings.json` của thư mục con khi chỉ mở repo gốc như một workspace đơn. Vì vậy hai file settings phục vụ hai cách mở khác nhau. Nếu ST setup thay đổi phiên bản bundle hoặc sinh lại phần cấu hình editor, cập nhật các khóa `cmake.cmakePath`, `cmake.configureArgs`, `cmake.preferredGenerators` và `stm32cube-ide-clangd.*` tương ứng ở root. Phiên bản compiler trong `--query-driver` phải khớp `bundles.store.json`.

Workspace chọn clangd của ST làm dịch vụ C/C++; Microsoft C/C++ IntelliSense được tắt để tránh chạy thêm engine trùng. Không giữ `drone/.vscode/c_cpp_properties.json` trong Git vì cấu hình hiện tại không dùng tệp này; `.gitignore` bỏ qua nếu công cụ sinh lại. Khi cần dùng lại Microsoft IntelliSense, bật engine và tạo cấu hình bằng lệnh **C/C++: Edit Configurations** trong VS Code. Cấu hình build thực tế luôn lấy từ CMake và `compile_commands.json`.

Các file `.settings` hiện không chứa đường dẫn home tuyệt đối hay mã probe riêng. Không bỏ qua cả thư mục `.settings` hoặc `.vscode`. Các file build, compile database sinh tự động và cache clangd được bỏ qua; đường dẫn công cụ cá nhân đặt trong User Settings hoặc `CMakeUserPresets.json`.

## Regenerate and Git

### Formatting

Quy tắc clang-format dùng chung nằm tại `../.clang-format`: LLVM, thụt 4 dấu cách, giữ nhóm và thứ tự include. Trong VS Code, dùng **Format Document** (`Shift+Alt+F` trên Linux) cho mã tự viết; workspace dùng formatter của clangd ST và tắt format-on-save cho C/C++.

`Core/`, `Drivers/` và mã tham khảo ngoài được bảo vệ bằng `.clang-format` con có `DisableFormat: true`. `.clang-format-ignore` ở root còn giúp lệnh CLI bỏ qua các thư mục đó cùng build outputs; không chỉ dựa vào ignore của CLI để bảo vệ thao tác format từ editor.

Để format một file tự viết từ root repository, chạy `clang-format -i path/to/file.c` sau khi thay đường dẫn bằng file cần sửa. Commit thay đổi format riêng khi sửa nhiều file. Sau khi CubeMX regenerate hoặc import lại project, giữ các file bảo vệ format và kiểm tra thiết lập editor nếu ST sinh lại chúng.

### Source control

- `Keep User Code` đang bật. Trong file do CubeMX quản lý, chỉ thêm mã vào các vùng `USER CODE BEGIN/END`.
- Khi cần viết module riêng, thêm file riêng rồi đăng ký source/include trong `drone/CMakeLists.txt`. File này có ghi chú được sinh một lần để người dùng bổ sung; `drone/cmake/stm32cubemx/CMakeLists.txt` là phần được generator cập nhật.
- Sau khi Generate Code, xem `git diff` trước khi commit. Giữ `.ioc`, `Core/`, `Drivers/` cùng license đi kèm, startup, linker script, CMake files/presets và `.mxproject` trong Git.
- Thư mục `build/` được bỏ qua. Cấu hình đường dẫn công cụ riêng cho máy có thể đặt trong `CMakeUserPresets.json`; file này được bỏ qua cùng `compile_commands.json` sinh tự động.
- Không đưa đường dẫn compiler tuyệt đối của máy vào cấu hình dùng chung.

Ví dụ commit cho project khởi tạo: `feat(firmware): initialize STM32F103 CubeMX project`. Thay đổi cấu hình build VS Code phục vụ firmware có thể nằm cùng commit này.

Tham khảo: [STM32CubeMX](https://dev.st.com/stm32cube-docs/stm32cubemx/), [STM32CubeIDE for VS Code](https://marketplace.visualstudio.com/items?itemName=stmicroelectronics.stm32-vscode-extension), [ST hướng dẫn dành chân SWD](https://www.st.com/resource/en/application_note/an4989-stm32-microcontroller-debug-toolbox-stmicroelectronics.pdf), [hướng dẫn debug của ST](https://dev.st.com/stm32cube-docs/stm32cubeide-vscode/latest/en/docs/markup/development/debug.html).
