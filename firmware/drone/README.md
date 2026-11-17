# Firmware drone

Ứng dụng hiện khởi tạo và poll IMU (BMI160 hoặc BMI270) qua SPI1/CS PA4, đổi mẫu đo sang SI.
Đã nhận 16 kênh RC qua CRSF và xuất PWM ESC qua TIM4; chưa có estimator,
vòng điều khiển, mixer hay arm/failsafe.
Xem [cấu hình chân, build và debug](../README.md) để bắt đầu.
Khung RTT hai kênh và Live Watch được mô tả tại [DEBUG.md](DEBUG.md).

## Mã nguồn

| Thư mục / tệp                  | Nội dung                                                                     |
| ------------------------------ | ---------------------------------------------------------------------------- |
| `Core/`, `drone.ioc`, `cmake/` | Khởi tạo và cấu hình do CubeMX quản lý                                       |
| `App/`                         | Điều phối ứng dụng, polling IMU và CRSF                                      |
| `Control/` (dự kiến)           | Lọc, quaternion, estimator, PID, allocator và mô hình motor; API độc lập HAL |
| `BSP/`                         | Ánh xạ board, SPI/CS, UART receiver, PWM và delay DWT                        |
| `Drivers/`                     | Bosch Sensor API, STM32 HAL và CMSIS                                         |
| `tests/imu/`                   | Kiểm thử trên máy tính với HAL mô phỏng                                      |
| `CMakeLists.txt`               | Source/include cho mã tự viết                                                |

Luồng gọi: `main` → `runtime` → `app` → `app_imu` → `bsp_imu` → Bosch API → callback HAL.
CubeMX khởi tạo HAL, clock và ngoại vi trước `runtime_init()`; vòng lặp gọi
`runtime_process()`. Runtime sở hữu receiver, chỉ khởi tạo App khi chọn FLIGHT
và chuyển command đã giải mã vào `app_process(command)`. [Kiến trúc](ARCHITECTURE.md) giải thích ranh giới module và
chuỗi điều khiển dự kiến.

## Chọn chế độ sau reset

Runtime chờ RC hợp lệ, CH5/SA arm OFF và ga ≤2% trước khi chốt mode.
Trong lúc chờ, PWM bị tắt; IMU và App chưa khởi tạo.

| CH7/SB     | CH8/SD                         | Chế độ vận hành           |
| ---------- | ------------------------------ | ------------------------- |
| HIGH       | Bất kỳ                         | ESC_CALIB: hiệu chuẩn ESC |
| Không HIGH | HIGH / STREAM                  | FLASH_EXPORT: khung chờ   |
| Không HIGH | LOW / OFF hoặc MIDDLE / RECORD | FLIGHT: chạy App          |

Theo [cấu hình Jumper T20](../Jumper-T20/README.md), CH6/SC dành cho chế độ
bay, CH7/SB yêu cầu hiệu chuẩn ESC và CH8/SD yêu cầu tắt/ghi/stream telemetry.
SC không chọn xuất flash hoặc chặn khởi động FLIGHT. Vị trí SC được chuyển
vào App mỗi lượt; chưa có ánh xạ vị trí sang các thuật toán bay khác nhau.

Đặt SB/SD trước khi cấp nguồn/reset. Chế độ vận hành chốt theo snapshot hợp lệ
đầu tiên thỏa điều kiện và giữ đến reset. Trong FLIGHT, đổi SC/SD tiếp tục cập
nhật command; mất RC do arm/failsafe xử lý. SB HIGH trong FLIGHT chặn motor,
không tự chuyển sang hiệu chuẩn.

FLASH_EXPORT chưa triển khai chức năng và không khởi chạy PWM. Xem chế độ bằng
`runtime_get_mode()`; `runtime_mode_supported()` trả true cho FLIGHT/ESC_CALIB và false cho FLASH_EXPORT, không biểu diễn sức khỏe hay quyền arm. Command dùng chung qua
`runtime_get_command()`.
Runtime khởi tạo RTT/log/telemetry trước khi chọn mode; khi bật log INFO,
RTT báo đang chờ và mode đã chọn (`supported=0` cho FLASH_EXPORT).
Trong FLIGHT, `app_get_status()` trả `imu_ready` (mẫu không quá 20 ms),
`motor_ready` và `arm_state`. Hai cờ ready không tự cấp quyền arm.

## Hiệu chuẩn ESC

Module `esc_calibration` sở hữu PWM riêng; không khởi tạo App bay, IMU hoặc PID.
Sau khi chọn ESC_CALIB, init bắt đầu PWM ở min. Lượt runtime kế tiếp dùng command
RC mới: SB còn HIGH, arm OFF và ga ≤2% thì chuyển cả bốn ESC sang max.
Nếu SB đã tắt trước lượt này, phiên bị hủy và giữ min.

| Trạng thái | Output / chuyển trạng thái                         |
| ---------- | -------------------------------------------------- |
| READY      | Min 1000 µs; chờ command mới để phát max           |
| MAX        | Max 2000 µs; SB OFF hợp lệ chuyển MIN              |
| MIN        | Giữ min 3 giây rồi DONE; bật SB lại không phát max |
| DONE       | Giữ min, chờ reset                                 |
| ABORT_MIN  | Giữ min, khóa đến reset                            |
| PWM_ERROR  | Tắt output, khóa đến reset                         |

Trong READY/MAX/MIN, mất RC, bật arm hoặc ga >2% hủy phiên về ABORT_MIN.
MAX quá 30 giây cũng hủy phiên; timeout ưu tiên hơn SB OFF nếu cùng một lượt.
Lỗi init/ghi PWM tắt output. Xem `esc_calibration_get_state()` và
`esc_calibration_get_reason()` hoặc log `esc_calib` để phân biệt nguyên nhân.
DONE chỉ báo hoàn thành trình tự xung, firmware không đọc phản hồi ESC.

`ESC_CALIBRATION_MAX_HOLD_MS` và `ESC_CALIBRATION_MIN_HOLD_MS` trong
`App/Inc/esc_calibration.h` là mặc định thử trên bàn, cần chỉnh theo ESC.
PWM vẫn dùng 400 Hz hiện tại; chưa xác nhận ESC hỗ trợ tần số này khi calib.
Không có delay blocking hoặc cấp phát động. Timer vẫn phát xung nếu CPU bị
kẹt; timeout phần mềm không bảo vệ trường hợp vòng lặp ngừng chạy.

Trước khi thử: tháo cánh, xác nhận loại ESC hỗ trợ calib endpoint bằng PWM.
Nếu ESC học endpoint lúc cấp nguồn, cấp nguồn flight controller và receiver
riêng trước, SB HIGH/arm OFF/ga thấp; xác nhận đang MAX rồi mới cấp nguồn ESC.
Nghe tín hiệu theo hướng dẫn ESC, tắt SB để phát min; hoàn tất thì reset.
Không tự chuyển từ calib sang flight trong cùng phiên khởi động.

Kiểm thử host: `cmake -S firmware/drone/tests/esc_calibration -B /tmp/drone-calib-tests`,
`cmake --build /tmp/drone-calib-tests` và
`ctest --test-dir /tmp/drone-calib-tests --output-on-failure`.
Chưa kiểm chứng xung thực tế, timing RC hoặc kết quả calib trên board/ESC.

## Kiểm tra IMU

Cấu hình ban đầu: accel/gyro 400 Hz, ±16 g và ±2000 °/s. BMI160 dùng normal
mode; BMI270 tắt advanced power save. Chưa có FIFO, ngắt DRDY, hiệu chuẩn bias hoặc chuyển trục sang thân drone.
IMU và định thời chưa được kiểm chứng trên board. Chip ID mong đợi:
BMI160 `0xD1`, BMI270 `0x24`. Wiring giữ SPI1 và CS PA4; xác nhận chân module BMI160 trước khi nối.
Ánh xạ trục trong `app_imu.c` được giữ nguyên; phải xác nhận lại theo cách lắp
module thực tế trước khi dùng mẫu SI cho điều khiển.

Driver Bosch tại [Drivers/BMI160](Drivers/BMI160/README.md) và
[Drivers/BMI270](Drivers/BMI270/README.md). Mọi lời gọi
init/read BSP phải tuần tự trong foreground vì callback Bosch không có context.

Xem biến `imu` trong `App/Src/app.c` bằng debugger:

| Trường                                         | Ý nghĩa                                                                         |
| ---------------------------------------------- | ------------------------------------------------------------------------------- |
| `status`                                       | `BSP_IMU_OK`, `BSP_IMU_NO_DATA` hoặc mã lỗi                                     |
| `sample_valid`                                 | Lần poll vừa rồi có mẫu mới hợp lệ                                              |
| `sample.accel_mps2[3]`, `sample.gyro_radps[3]` | X/Y/Z theo package cảm biến; m/s² và rad/s                                      |
| `sample.sensor_time_ticks`                     | Timestamp cảm biến 24-bit, chưa mở rộng khi tràn                                |
| `sample.t_read_us`                             | Uptime MCU sau lần đọc thành công, µs, tràn uint32; không phải acquisition time |
| `sample_count`, `read_error_count`             | Số mẫu hợp lệ và lỗi đọc                                                        |
| `sensor.driver_error`                          | Mã lỗi Bosch gần nhất                                                           |

Chỉ cập nhật mẫu khi cả accel và gyro sẵn sàng. Khi chưa có mẫu mới hoặc lỗi,
dữ liệu cũ được giữ nhưng `sample_valid = false`; lần poll sau tiếp tục đọc.
Sau lỗi khởi tạo, có thể gọi lại `app_init()` để khởi tạo lại ứng dụng.

## PWM ESC

`bsp_motor_pwm` dùng TIM4 đã được CubeMX cấu hình: clock timer 72 MHz,
prescaler 71, ARR 2499, tick 1 µs, chu kỳ 2,5 ms (400 Hz), PWM1 active high.
API dùng thứ tự motor M1–M4: trước-phải CCW, sau-phải CW,
sau-trái CCW, trước-trái CW (nhìn từ trên). Ánh xạ board nằm trong BSP:
M1 → PB8 / TIM4_CH3, M2 → PB9 / TIM4_CH4,
M3 → PB6 / TIM4_CH1, M4 → PB7 / TIM4_CH2.

`app_init()` khởi động cả bốn kênh ở 1000 µs trước khi khởi tạo IMU/receiver.
Xem `pwm_ready` trong `App/Src/app.c` bằng debugger để biết kết quả. Nếu một
kênh khởi động lỗi, BSP tắt cả bốn đầu ra và từ chối lệnh ghi; gọi lại
`app_init()` để thử lại. `app_process()` chỉ xuất mixer khi ARMED và ga >2%;
còn lại gửi 1000 µs. Lỗi ghi PWM tắt đầu ra cho đến lần App init tiếp theo.

`app_motor_set_all()` nhận lệnh ESC fixed-point `norm_q15_t` (`int32_t`,
1,0 = 32768), đổi sang microsecond hoàn toàn bằng số nguyên. Mixer nhận
float ở cửa vào rồi trả lực đẩy và phản hồi fixed-point; xem
[Control/README.md](Control/README.md). Hiện ánh xạ thrust → ESC tuyến tính
để thử bộ gá; chưa có đường đặc tính motor/ESC đã đo và hiệu chuẩn.

API tại [BSP/Inc/bsp_motor_pwm.h](BSP/Inc/bsp_motor_pwm.h), chỉ gọi trong foreground
sau khởi tạo ngoại vi. Chấp nhận xung 1000–2000 µs; lệnh sai trả `false`
và giữ giá trị cũ. Xung mới áp dụng tại update timer.
`bsp_motor_pwm_stop()` ngừng phát xung; init lại bắt đầu từ 1000 µs.
Tắt tín hiệu khác với gửi ga tối thiểu; phản ứng khi mất tín hiệu phụ thuộc ESC.

Kiểm tra dải xung/tần số theo ESC, nối chung GND và tháo cánh trước khi thử.
Cần đo PB6–PB9 trên board để xác nhận chu kỳ, độ rộng và hành vi start/stop.
Kiểm thử PWM với thanh ghi và HAL mô phỏng:

```sh
cmake -S firmware/drone/tests/pwm -B /tmp/drone-pwm-tests -G Ninja
cmake --build /tmp/drone-pwm-tests
ctest --test-dir /tmp/drone-pwm-tests --output-on-failure
```

## Khi đổi wiring

Sửa `drone.ioc`, generate lại rồi cập nhật SPI handle tại
`BSP/Src/bsp_board.c` nếu đổi bus. Giữ CS output push-pull, ban đầu High,
label `IMU_CS`; SPI master, full duplex, 8-bit, MSB first, software NSS,
mode 0 hoặc 3, clock phù hợp module. Build, chạy test rồi kiểm tra chip ID
và mẫu đo trên board.

BSP dùng SPI đã được CubeMX khởi tạo và delay DWT bật sau cấu hình clock.
API truyền đồng bộ, timeout HAL 100 ms mỗi giao dịch; chưa hỗ trợ đọc FIFO dài.
Hợp đồng sử dụng API nằm trong header `App/Inc` và `BSP/Inc`.

## Chọn cảm biến

Trong [cmake/imu.cmake](cmake/imu.cmake), comment dòng đang dùng và bỏ comment
cho dòng cần chọn, giữ đúng một dòng hoạt động:

```cmake
set(DRONE_IMU BMI160)
# set(DRONE_IMU BMI270)
```

Sau đó chạy `make fw-build` từ gốc repository và flash lại firmware. CMake
chỉ biên dịch BSP và driver của cảm biến được chọn; App giữ API `bsp_imu_*`.
Cả hai thư mục driver vẫn được giữ nguyên, không cần sửa mã upstream.

Có thể ghi đè lựa chọn bằng `cube cmake --preset Debug -DDRONE_IMU=BMI270`
khi chạy từ `firmware/drone`. Giá trị `-D` được lưu trong cache và ưu tiên hơn
lựa chọn trong file; dùng `cube cmake --preset Debug -U DRONE_IMU` để trở lại
lựa chọn bằng comment. Không tự dò cảm biến lúc chạy. Cần xác nhận wiring và
hướng trục theo module thực tế mỗi khi thay cảm biến.

## Build và kiểm thử

Build firmware theo [hướng dẫn chung](../README.md#build).
Kiểm thử IMU không cần board hoặc toolchain ARM; chạy từ gốc repository:

```sh
cmake -S firmware/drone/tests/imu -B /tmp/drone-imu-tests -G Ninja
cmake --build /tmp/drone-imu-tests
ctest --test-dir /tmp/drone-imu-tests --output-on-failure
```

Suite IMU luôn chạy cả BMI160 và BMI270, độc lập với lựa chọn firmware.
Test dùng driver Bosch thật và HAL mô phỏng để kiểm tra khởi tạo, đọc mẫu
và xử lý lỗi; không xác nhận nối dây hoặc định thời trên board.

## Receiver ExpressLRS

USART1 nhận CRSF ở 420000 baud, 8N1: nối TX của receiver vào PA10 (RX)
STM32 và nối chung GND; cấp nguồn receiver theo thông số của thiết bị. Chọn
serial protocol CRSF trên receiver. PA9 là TX của STM32 nhưng telemetry chưa
được triển khai.

`app_receiver_get_sample()` trả về 16 kênh CRSF thô và trạng thái `connected`;
trạng thái này chỉ đúng khi đã nhận frame RC có CRC hợp lệ trong 100 ms gần
nhất. Thứ tự kênh và ý nghĩa arm/throttle phụ thuộc cấu hình tay điều khiển.
App dùng trạng thái này để failsafe khi mất RC; phục hồi không tự arm.
Cần OFF → ON với ga <=2%, IMU mới và controller/PWM sẵn sàng.

USART1 RX dùng DMA1 Channel 5 circular và ngắt IDLE/half/full. Khi nhận
liên tục ở 420 kbaud, cần phục vụ IRQ trong khoảng 0,76 ms để tránh DMA
ghi đè; mất cả vòng DMA không tự phát hiện được. Timestamp nhận dùng HAL
tick của batch, có độ trễ gom byte/IRQ và lượng tử 1 ms.

`app_receiver_poll()` giải mã trong foreground, mục tiêu không quá 2 ms
giữa các lần poll. Tràn bộ đệm hoặc lỗi UART làm bỏ dữ liệu chờ và ngắt
trạng thái kết nối đến frame RC mới. SPI IMU có thể chặn lâu khi lỗi;
bộ đệm hiện tại không bảo đảm giữ được RC trong khoảng đó. Cần đo lịch chạy
và độ trễ trên board.

Parser hỗ trợ frame RC `0x16` đủ 16 kênh, kiểm tra độ dài/CRC và timeout
3 ms theo timestamp batch. Khung khác được kiểm tra rồi bỏ qua; chưa hỗ trợ
subset `0x17`, link statistics hoặc telemetry. Kênh cũ được giữ khi mất
kết nối; `connected` phản ánh tuổi frame RC, không xác nhận chất lượng RF.
Hợp đồng API xem [App/Inc/app_receiver.h](App/Inc/app_receiver.h).

Kiểm thử CRSF và tích hợp UART với HAL mô phỏng:

```sh
cmake -S firmware/drone/tests/crsf -B /tmp/drone-crsf-tests -G Ninja
cmake --build /tmp/drone-crsf-tests
ctest --test-dir /tmp/drone-crsf-tests --output-on-failure
```

Test kiểm tra parser, DMA/buffer, timeout và phục hồi lỗi với HAL mô phỏng;
chưa xác nhận timing hoặc phần cứng.

### Ánh xạ RC thành command

`runtime_process()` lấy snapshot receiver và gọi `app_command_decode()` một lần
mỗi lượt, kể cả khi không có mẫu IMU mới hoặc tắt RTT/telemetry. Getter receiver
đã poll UART bên trong. Runtime truyền command vào App; App sao chép đầu vào,
không giữ con trỏ, không poll hoặc giải mã receiver. `runtime_get_command()`
sao chép command gần nhất; gọi sau `runtime_process()` và kiểm tra `valid`.
Runtime cũng gửi telemetry RC thô mỗi 50 ms kể cả khi đang chờ chọn mode; App chỉ gửi
telemetry IMU, rate và thống kê. RTT và bộ đếm telemetry được reset bởi runtime,
không reset khi App khởi tạo.
Getter command không tự kiểm tra tuổi dữ liệu. Snapshot lỗi hoặc mất kết nối
xóa command, không giữ throttle hoặc yêu cầu arm cũ.

Khi bật RTT và log mức INFO trở lên, channel 0 xuất một dòng tag `command`
khi arm, mode, yêu cầu hiệu chuẩn ESC hoặc chế độ telemetry thay đổi. Dòng
log chứa snapshot bốn trường đó; thay đổi stick/throttle hoặc timestamp không
phát sinh log. Frame hợp lệ đầu tiên và lần kết nối lại xuất snapshot;
mất tính hợp lệ xuất `valid=0` một lần. Log phản ánh yêu cầu RC, không phải
trạng thái armed hay xác nhận thao tác đã thực hiện. Đây là log một lần thử,
không retry khi RTT đầy.

Ánh xạ theo [Jumper T20 model04](../Jumper-T20/model04.yml):

| Kênh | Command                   | Nguồn radio |
| ---- | ------------------------- | ----------- |
| CH1  | roll                      | Ail         |
| CH2  | pitch                     | Ele         |
| CH3  | throttle                  | Thr         |
| CH4  | yaw                       | Rud         |
| CH5  | arm_requested             | SA          |
| CH6  | mode                      | SC          |
| CH7  | esc_calibration_requested | SB          |
| CH8  | telemetry_mode            | SD          |

Command dùng `norm_q15_t`: 32768 tương ứng 1. Roll/pitch/yaw nằm trong
`[-32768,32768]`; throttle trong `[0,32768]`. Toàn bộ ánh xạ dùng số nguyên
32-bit, làm tròn gần nhất; tích trung gian lớn nhất dưới 54 triệu.
Endpoint giả định là 172/992/1811, khai báo tại `App/Inc/app_command.h`,
chưa phải kết quả hiệu chuẩn tay điều khiển. Stick có deadband ±10 count,
phần còn lại được co giãn lại để vẫn đạt ±1; throttle không áp dụng deadband.
Giá trị trong 0–2047 được kẹp theo endpoint, giá trị lớn hơn 2047 ở CH1–CH8
làm command không hợp lệ. Kênh không dùng không ảnh hưởng command.

Switch LOW khi raw ≤582, HIGH khi raw ≥1402, còn lại MIDDLE. CH5 chỉ yêu cầu
arm khi HIGH; vị trí SA thực tế tương ứng HIGH cần kiểm tra qua telemetry.
CH6 chỉ lưu vị trí, chưa gán rate/angle hay chế độ bay khác. CH7 HIGH yêu cầu
hiệu chuẩn ESC; CH8 LOW/MIDDLE/HIGH yêu cầu telemetry OFF/RECORD/STREAM.
Đây là yêu cầu mức, chưa có phát hiện cạnh hoặc kiểm tra điều kiện thực hiện.
Hiệu chuẩn ESC và lựa chọn đích telemetry bằng command chưa được thực thi;
telemetry hiện vẫn theo cấu hình build. Không có
hysteresis, expo hoặc đảo chiều trong decoder. Dấu stick đi theo chiều tăng
kênh radio; cần xác nhận tương ứng FRD trước khi đưa vào điều khiển.

App chuyển command thành setpoint rad/s và gọi PID/mixer theo chu kỳ riêng.
`app_safety` quản lý DISARMED/ARMED/FAILSAFE; cần OFF → ON với ga <=2%.
RC hết hạn 100 ms, lỗi IMU/mẫu quá 20 ms, lỗi rate/PWM hoặc yêu cầu hiệu chuẩn
ESC chặn motor. Sau failsafe cần tín hiệu khỏe, OFF rồi ON lại; ON từ lúc boot
không arm. Debug và Release đều bật `DRONE_ENABLE_RATE_CONTROL`, với gain thử
P=0,1, I=D=0, giới hạn 2 rad/s, chu kỳ 2000 µs. Xem quy trình thử và giới hạn
trong [DEBUG.md](DEBUG.md#thử-trên-board).

```sh
cmake -S firmware/drone/tests/command -B /tmp/drone-command-tests
cmake --build /tmp/drone-command-tests
ctest --test-dir /tmp/drone-command-tests --output-on-failure
```

Test quét đủ 2048 mức CRSF để kiểm tra tính đơn điệu/giới hạn, endpoint,
deadband, ánh xạ kênh, switch và xóa command khi dữ liệu lỗi. Test tích hợp
trong `tests/logging` kiểm tra cập nhật command giữa các slot telemetry,
khi IMU không có mẫu mới và khi receiver mất kết nối.
