# STM32 firmware

Project [drone](drone/README.md) chạy trên STM32F103VET6. Firmware đã tích hợp
IMU, receiver CRSF, PWM ESC và vòng điều khiển tốc độ góc. Cấu hình hiện tại
chỉ dành cho thử nghiệm; xem giới hạn và quy trình tại
[DEBUG.md](drone/DEBUG.md#thử-trên-board).

## Build và nạp

Cần CMake ≥ 3.22, Ninja và `arm-none-eabi-gcc`. Từ gốc repository:

```sh
make fw-build                 # preset Debug
make fw-build PRESET=Release
make fw-flash                 # build, nạp, verify và reset qua ST-LINK
```

ELF nằm tại `firmware/drone/build/Debug/drone.elf` hoặc thư mục Release tương
ứng. Có thể dùng CMake Tools/STM32CubeIDE extension trong VS Code để build;
workspace gốc đã có task cho Debug và Release. `make clean` cũng xóa build báo
cáo và cần configure lại sau đó.

Nối SWDIO → PA13, SWCLK → PA14, GND → GND và VTref → 3,3 V board nếu probe
có chân này. VTref là điện áp tham chiếu, không mặc định cấp nguồn board.
Xem [debug với VS Code và RTT](drone/DEBUG.md#dùng-từ-máy-tính).

Khi sửa `drone.ioc`, chạy `make cubemx` từ gốc repo và kiểm tra mã sinh lại.
Chi tiết cấu trúc và ranh giới module nằm trong
[kiến trúc firmware](drone/ARCHITECTURE.md).

Xem [cấu hình radio Jumper T20](Jumper-T20/README.md) để thiết lập receiver
và các kênh điều khiển.
