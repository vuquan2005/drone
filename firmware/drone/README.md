# Firmware drone

Firmware đọc BMI160/BMI270 qua SPI, nhận 16 kênh CRSF, điều khiển attitude/rate
qua mixer Quad X và xuất PWM đến ESC. Gain và ánh xạ thrust hiện dành cho bộ
gá, chưa hiệu chuẩn bay. Xem [build và nạp](../README.md),
[debug và thử nghiệm](DEBUG.md) và [kiến trúc](ARCHITECTURE.md).

`Core/main` khởi tạo ngoại vi rồi gọi `runtime`; runtime quản lý receiver,
chọn chế độ và chuyển command vào `App`. `App` điều phối IMU, an toàn và
đầu ra; `BSP` kết nối phần cứng; `Control` chứa thuật toán độc lập HAL.
Kiểm thử host nằm trong `tests/`.

## Board

STM32F103VET6 có Flash 512 KiB, RAM 64 KiB; HSE 8 MHz, PLL ×9, CPU 72 MHz.
Cấu hình CubeMX nằm tại [drone.ioc](drone.ioc). Xác nhận mã chip, nguồn,
mức tín hiệu và wiring trên board thực tế trước khi nạp hoặc cấp nguồn ESC.

| Thiết bị          | Kết nối STM32             | Cấu hình                      |
| ----------------- | ------------------------- | ----------------------------- |
| IMU BMI160/BMI270 | SPI1: PA5/PA6/PA7, CS PA4 | SPI mode 0, 9 MHz             |
| Receiver ELRS     | TX → PA3, RX ← PA2        | USART2, CRSF 420000 baud, 8N1 |
| ESC               | PB6/PB7/PB8/PB9           | TIM4 PWM 400 Hz, 1000–2000 µs |

Thứ tự chân không trùng thứ tự motor: xem [PWM ESC](#pwm-esc).
Nối chung GND và tháo cánh trước khi thử motor.

## Chọn chế độ sau reset

Với cấu hình chọn mode thông thường, runtime giữ PWM tắt trong khi chờ RC
hợp lệ. Frame RC hợp lệ đầu tiên xác định chế độ theo bảng dưới, giữ đến lần
reset tiếp theo. Trong FLIGHT, hệ thống phải khỏe và firmware phải thấy
CH5/SA OFF trước khi chấp nhận lần gạt SA ON với ga ≤2%.

Khi thử trên bộ gá, giữ ga ≤2% liên tục 5 giây trong lúc arm sẽ tự disarm.
Tăng ga trước hạn sẽ hủy đếm. Sau khi hết hạn, tay điều khiển báo
`IDLE TIMEOUT` và cần gạt SA OFF rồi ON với ga ≤2% để arm lại.
`DRONE_ENABLE_BENCH_IDLE_DISARM` bật mặc định. Đặt tùy chọn CMake này thành
`OFF` với build bay tự do vì firmware chưa xác nhận được trạng thái hạ cánh.

| CH7/SB     | CH6/SC        | CH8/SD                         | Chế độ                                  |
| ---------- | ------------- | ------------------------------ | --------------------------------------- |
| HIGH       | Bất kỳ        | HIGH / STREAM                  | `FLASH_ERASE`, chờ xác nhận xóa SPI NOR |
| HIGH       | MIDDLE        | LOW / OFF hoặc MIDDLE / RECORD | `IMU_CALIB`, hiệu chuẩn gyro và accel   |
| HIGH       | LOW hoặc HIGH | LOW / OFF hoặc MIDDLE / RECORD | `ESC_CALIB`, hiệu chuẩn ESC             |
| Không HIGH | Bất kỳ        | HIGH / STREAM                  | `BOOT_SELECT`, PWM tắt, chờ đổi SD      |
| Không HIGH | Bất kỳ        | LOW / OFF hoặc MIDDLE / RECORD | `FLIGHT`                                |

Xem [cấu hình radio](../Jumper-T20/README.md) cho ánh xạ kênh. Trong
FLIGHT, SC chọn bộ điều khiển, SB HIGH kích hoạt failsafe và SD MIDDLE yêu
cầu [ghi telemetry vào SPI NOR](../../protocol/flash-journal.md).

Để xuất flash, nạp ảnh bảo trì bằng `make fw-flash PRESET=Export` rồi dùng
[`make flash-export`](../../tools/rtt/README.md#xuất-telemetry-đã-ghi-trong-flash).
Ảnh này chạy từ `main()` và giao tiếp qua RTT. Nạp lại ảnh bay trước khi ghi
telemetry hoặc chạy chế độ bay.

## Xóa telemetry trên flash ngoài

Có thể xóa qua [RTT Viewer](../../tools/rtt/README.md#xuất-telemetry-đã-ghi-trong-flash)
sau khi đã lưu và xác minh bản export. Để xóa trực tiếp từ tay điều khiển,
dùng chế độ `FLASH_ERASE`.

`FLASH_ERASE` xóa toàn bộ W25Q128FV trên SPI2. Đặt **SD HIGH, SB HIGH**
rồi reset với RC hợp lệ. Firmware giữ PWM tắt và báo `ERASE WAIT` qua
CRSF. Hạ ga về ≤2%, đưa SA về LOW sau khi vào mode, rồi gạt SA lên HIGH
và giữ đủ 3 giây với SD và SB vẫn HIGH. Khởi động với SA sẵn ở HIGH
vẫn phải thực hiện lại cạnh LOW → HIGH.

Nếu mất RC hoặc điều kiện xác nhận thay đổi trong lúc giữ SA, đưa SA về
LOW rồi xác nhận lại. Sau khi xác nhận, firmware báo `ERASING` trong lúc
xóa và đọc kiểm tra toàn bộ chip. Chờ `ERASE DONE` trước khi reset về chế
độ ghi. Nếu báo `ERASE ERR` hoặc mất nguồn giữa chừng, vào lại chế độ xóa
để xác nhận và chạy lại. Dữ liệu hiệu chuẩn IMU nằm trong flash nội STM32.

## Hiệu chuẩn ESC

Tháo cánh và xác nhận ESC hỗ trợ hiệu chuẩn endpoint bằng PWM 400 Hz. Chọn
ESC_CALIB bằng cách đặt SB ở HIGH và SC ở LOW khi khởi động với RC hợp lệ. Firmware bắt đầu
ở 1000 µs, sau đó phát 2000 µs nếu vẫn nhận được SB HIGH. Nếu ESC học endpoint
lúc cấp nguồn, cấp nguồn flight controller và receiver riêng trước, xác nhận
đang phát max rồi mới cấp nguồn ESC. Theo hướng dẫn ESC, đưa SB về OFF để phát
min. Firmware giữ min 3 giây rồi chờ reset. DONE chỉ xác nhận trình tự xung,
không xác nhận ESC đã học thành công.

Mất RC hoặc giữ max đến 30 giây sẽ hủy phiên và đưa xung về min. Lỗi PWM sẽ
tắt output. Các thời gian thử nằm trong `App/Inc/esc_calibration.h`. Timer vẫn
phát xung nếu CPU bị kẹt, nên timeout phần mềm không bảo vệ trường hợp đó.
Cần kiểm chứng xung, timing và kết quả trên board/ESC.

## Hiệu chuẩn gyro và accelerometer

1. Tháo cánh và đặt khung đứng yên. Trên tay điều khiển, để SA ở OFF, ga ở
   mức thấp nhất, SB ở HIGH và SC ở giữa. Giữ các vị trí này khi reset bo.
2. Khi T20 hiện `IMU GYRO` trên ô `FM`, giữ khung đứng yên. Sau 1 giây chờ
   IMU và RC, firmware cần ít nhất 2.280 mẫu liên tiếp trong cửa sổ 3 giây
   ở ODR 800 Hz. Build có RTT và `DRONE_LOG_LEVEL=INFO` còn ghi log `imu_calib`.
3. Khi `FM` hiện dạng `X+ 0/6`, firmware đã chuyển sang đo accel. Nhãn gợi ý
   một mặt còn thiếu và cho biết số mặt đã nhận. Dùng bảng bên dưới để đặt
   khung và giữ yên đến khi số đếm tăng. Có thể xoay qua sáu mặt theo thứ tự
   tùy ý. Dùng mặt chuẩn hoặc bộ gá để đặt mỗi trục FRD thẳng đứng đúng chiều.
   Độ nghiêng của khung lúc đo sẽ ảnh hưởng trực tiếp đến hệ số lệch chéo trục.
   Mỗi cửa sổ cần ít nhất 760 mẫu trong 1 giây. Firmware dùng ngưỡng khoảng
   10° để nhận diện mặt và kiểm tra gyro cùng độ dao động của accel.
   RTT báo các mặt còn thiếu. Mặt đã nhận được tính một lần. Giữ nguyên công
   tắc và ga khi xoay khung.
4. Khi `FM` hiện `IMU DONE`, firmware đã lưu hệ số gyro và accel vào flash.
   Trả SB và SD về LOW rồi reset để vào `FLIGHT`. Log `imu` báo đã nạp hệ số
   gyro và accel từ flash.
   Có thể kiểm tra gyro đã bù trong tab **Rate · FRD** khi khung đứng yên.

Firmware chỉ đọc bản ghi hiệu chuẩn v3. Nếu đang dùng bản ghi v1 hoặc v2,
chạy lại `IMU_CALIB` sau khi cập nhật để lưu bias gyro và ma trận hiệu chỉnh
accel từ quy trình sáu mặt.

| Mặt đo | Cách đặt khung FRD         |
| ------ | -------------------------- |
| `X+`   | Mũi hướng lên              |
| `X-`   | Mũi hướng xuống            |
| `Y+`   | Cạnh phải hướng lên        |
| `Y-`   | Cạnh phải hướng xuống      |
| `Z+`   | Khung lật úp               |
| `Z-`   | Khung nằm ngang đúng chiều |

| Nhãn FM    | Lý do dừng hiệu chuẩn                                     |
| ---------- | --------------------------------------------------------- |
| `IMU RC`   | Mất lệnh RC hợp lệ.                                       |
| `IMU STOP` | Đổi tổ hợp công tắc, bật arm hoặc ga ngoài miền cho phép. |
| `IMU INIT` | Khởi tạo IMU thất bại.                                    |
| `IMU READ` | Đọc IMU thất bại.                                         |
| `IMU TIME` | Hết thời gian đo gyro hoặc accel.                         |
| `ACC ERR`  | Dữ liệu mặt đo hoặc hệ số accel ngoài giới hạn.           |
| `SAVE ERR` | Lưu hệ số vào flash thất bại.                             |

Chuyển động hoặc khoảng cách giữa hai mẫu vượt 20 ms làm thu lại cửa sổ
đang đo. Sau giây chờ đầu tiên, mất RC, đổi tổ hợp công tắc, bật arm hoặc
ga trên 2% sẽ hủy phiên. Bước gyro có 20 giây tính từ lúc khởi tạo IMU xong.
Bước accel có 120 giây sau khi đo xong gyro. Firmware ghi một lần sau khi đủ
sáu mặt và giữ bộ hệ số cũ nếu hủy hoặc hết hạn giữa chừng.

Khi đang thu mẫu trong `IMU_CALIB`, RTT kênh 1 gửi cùng gói IMU raw như trong
`FLIGHT`, kể cả lúc khung đang xoay hoặc đặt lệch. Xem tab **IMU** hoặc ghi
`imu_raw_*.csv` để kiểm tra dấu và độ lớn từng trục. Tab **IMU** hiển thị số
đo raw đã đổi sang FRD và đơn vị SI.

Để build và nạp bản Debug có RTT và chọn mode, chạy từ gốc repo:

```sh
cd firmware/drone
cube cmake --preset Debug -DDRONE_DEBUG_FORCE_FLIGHT=OFF
cube cmake --build --preset Debug
cube programmer -c port=SWD freq=1000 reset=SWrst -w build/Debug/drone.elf -v -rst
```

`IMU_CALIB` giữ PWM tắt đến reset, kể cả khi hiệu chuẩn hoàn tất hoặc lỗi.
Kết quả cuối trên `FM` cũng giữ đến reset.
Cần đặt khung cố định trong suốt cửa sổ đo vì IMU sáu trục có thể nhầm chuyển
động yaw rất chậm với bias.

Firmware dành hai trang flash cuối (`0x0807F000` đến `0x0807FFFF`) cho hiệu
chuẩn IMU. Bản v2 dài 52 byte, gồm mã IMU, ánh xạ trục, dải đo, phiên bản,
số thứ tự, ba bias gyro, ba bias accel, ba hệ số tỉ lệ accel và CRC.
`FLIGHT` vẫn đọc được bản v1 chỉ có bias gyro.
Firmware ghi trang còn lại, đọc kiểm tra rồi đánh dấu hoàn tất. Nếu việc ghi
bị gián đoạn, bản hợp lệ trước đó vẫn có thể được nạp. Khi nạp firmware, dùng
quy trình chỉ xóa các trang của ảnh chương trình để giữ hệ số. Lệnh
`make fw-erase` xóa toàn chip và cả hệ số đã lưu.

## IMU và receiver

IMU được cấu hình 800 Hz, accel ±16 g và gyro ±2000 °/s. App đổi mẫu sang SI
và hệ thân FRD (X tiến, Y phải, Z xuống), theo ánh xạ sensor `[-X, +Y, -Z]`;
cần xác nhận lại theo cách lắp thực tế. `t_read_us` là uptime MCU sau khi đọc
thành công và tràn uint32 sau khoảng 71,6 phút. Xem API trong
[app_imu.h](App/Inc/app_imu.h) và driver [BMI160](Drivers/BMI160/README.md),
[BMI270](Drivers/BMI270/README.md).

Trong FLIGHT, App nạp bản hiệu chuẩn tương thích mới nhất trong flash. App
trừ bias gyro trước Mahony và PID tốc độ góc. Với bản v2, App trừ bias accel
rồi nhân hệ số tỉ lệ từng trục trước Mahony. Khi flash chưa có bản phù hợp, BMI160 dùng
`[-0.001541997, -0.004106014, -0.005377476]` rad/s theo thứ tự XYZ trong FRD
và BMI270 dùng bias bằng 0. Bộ mặc định BMI160 là trung bình của 2.020 mẫu trong 5 giây
cuối phiên `telemetry_20261119_023330` tại `experiments/raw/telemetry/`, từ
sequence 8258 đến 10277. Đổi số đếm sang FRD và rad/s theo
[định dạng telemetry](../../protocol/telemetry.md) trước khi lấy trung bình.
Bản ghi IMU raw giữ nguyên số đếm theo trục cảm biến. Tab IMU của RTT hiển
thị giá trị đổi đơn vị từ số đếm đó.

USART2 nhận CRSF qua DMA1 Channel 6. Nối TX receiver vào PA3, RX receiver vào
PA2 và chung GND. `connected` phản ánh frame RC hợp lệ trong 100 ms gần nhất.
Command dùng CH1–CH4 cho roll/pitch/throttle/yaw, CH5 arm, CH6 mode,
CH7 calib và CH8 telemetry. Endpoint và chiều stick cần xác nhận trên radio và
board; hợp đồng giải mã nằm trong [app_command.h](App/Inc/app_command.h).
Cần đo timing receiver trên board, đặc biệt khi đọc SPI gặp lỗi.

Runtime gửi trạng thái qua CRSF Flight Mode (`0x21`) mỗi 500 ms. Trên Jumper
T20, đặt `Telem Ratio` của ELRS thành `Std` và dùng **Telemetry → Discover New**
trong EdgeTX để tìm cảm biến `FM`. Trong FLIGHT, `FM` hiển thị `DISARMED`,
`ARMED` hoặc nguyên nhân failsafe mà App ghi nhận:

| Nhãn       | Ý nghĩa                                                          |
| ---------- | ---------------------------------------------------------------- |
| `IMU INIT` | Khởi tạo IMU thất bại. Kết quả khởi tạo giữ đến reset.           |
| `FS IMU`   | IMU đã khởi tạo nhưng chưa có mẫu mới, mẫu quá cũ hoặc đọc lỗi.  |
| `FS RC`    | Lệnh RC hết hạn hoặc không hợp lệ.                               |
| `FS PWM`   | Khởi tạo hoặc cập nhật PWM thất bại.                             |
| `FS CTRL`  | Bộ điều khiển chưa sẵn sàng hoặc tính toán thất bại.             |
| `FAILSAFE` | Lý do bảo vệ chưa có nhãn riêng, như yêu cầu calib trong FLIGHT. |

Ở các mode khác, nhãn gồm `WAIT RC` lúc chờ chọn mode, `ESC READY`, `ESC MAX`,
`ESC MIN`, `ESC DONE`, `ESC ABORT` hoặc `ESC PWM ERR` trong ESC_CALIB,
và `FLASH N/A` khi chọn xuất flash. `ESC DONE` xác nhận trình tự xung đã kết thúc.
`FM` hoạt động độc lập với RTT và công tắc SD. Khi EdgeTX báo mất telemetry,
giá trị `FM` có thể là nhãn cũ.

Trong FLIGHT, Runtime gửi CRSF Attitude (`0x1E`) tối đa 5 Hz khi ước lượng
Mahony còn mới (tuổi mẫu tối đa 20 ms), ưu tiên khung `FM` khi đến lịch.
Dùng **Telemetry → Discover New** trên T20 rồi chọn `Roll`, `Ptch` và `Yaw`
trong **Display → Nums**. EdgeTX hiển thị các góc theo radian. Roll dương khi
bên phải hạ xuống, pitch dương khi mũi ngẩng lên. Yaw là góc tương đối với
hướng lúc Mahony khởi tạo. Thử nghiêng drone trên bàn khi đã tháo cánh để
đối chiếu chiều và đơn vị.

## Điều khiển tư thế

| CH6/SC      | Roll và pitch                                      | Yaw             |
| ----------- | -------------------------------------------------- | --------------- |
| LOW         | Cần điều khiển đặt tốc độ góc thân trực tiếp       | Tốc độ góc thân |
| MIDDLE/HIGH | Cần điều khiển đặt góc trong ±25°, giữa cần đặt 0° | Tốc độ góc thân |

Mahony chạy danh định 200 Hz. Mỗi lần cập nhật dùng gyro trung bình theo
thời gian của bốn khoảng mẫu liên tiếp, cùng accel mới nhất nếu độ lớn mẫu
trong khoảng 0,8–1,2 g. `dt` lấy từ chênh lệch timestamp đọc trên MCU. Vòng
attitude chạy danh định 100 Hz, dùng sai số độ nghiêng để tạo tốc độ góc
quanh X/Y thân với gain P ban đầu 4 s⁻¹. Vòng rate giữ setpoint X/Y giữa hai
lần cập nhật attitude và nhận lệnh yaw trực tiếp. Lịch rate giữ khoảng cách
tối thiểu 2 ms giữa các mẫu đã dùng, tương ứng 400 Hz khi IMU ra mẫu đều ở
800 Hz.

Chế độ attitude vào failsafe nếu quaternion mất hiệu lực hoặc cũ hơn 20 ms.
Khôi phục tư thế hợp lệ rồi đưa SA về OFF trước khi arm lại. Gain, tần số thực
tế và đáp ứng cần kiểm chứng trên board/bộ gá.

## PWM ESC

TIM4 phát PWM 400 Hz với tick 1 µs. Theo mixer Quad X, nhìn từ trên:

| Motor | Vị trí và chiều quay | Chân           |
| ----- | -------------------- | -------------- |
| M1    | Trước phải, CCW      | PB8 / TIM4_CH3 |
| M2    | Sau phải, CW         | PB9 / TIM4_CH4 |
| M3    | Sau trái, CCW        | PB6 / TIM4_CH1 |
| M4    | Trước trái, CW       | PB7 / TIM4_CH2 |

Khi chưa armed hoặc ga ≤2%, App gửi 1000 µs; lỗi ghi PWM tắt tín hiệu và khóa
đến lần khởi tạo App tiếp theo. Mất tín hiệu và xung min có thể khiến ESC phản
ứng khác nhau. Dải thrust → ESC tuyến tính 0–1 → 1000–2000 µs chưa được đo
và hiệu chuẩn. Xác nhận thứ tự, chiều quay, xung và hành vi ESC trên bộ gá.
API nằm trong [bsp_motor_pwm.h](BSP/Inc/bsp_motor_pwm.h).

## Kiểm thử host

Các bộ test có thể chạy riêng từ gốc repository; ví dụ:

```sh
cmake -S firmware/drone/tests/command -B /tmp/drone-command-tests
cmake --build /tmp/drone-command-tests
ctest --test-dir /tmp/drone-command-tests --output-on-failure
```

Thay `command` bằng `imu`, `imu_calibration`, `crsf`, `pwm` hoặc `esc_calibration` để chọn bộ
tương ứng. Các test mô phỏng không xác nhận I/O hoặc timing trên board.
