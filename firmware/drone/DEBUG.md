# Khung debug

## Các phần đang có

- **ST-LINK/SWD + GDB:** nạp, Pause, breakpoint, stepping và xem biến.
- **OpenOCD + RTT:** đọc buffer RAM khi CPU chạy, không cần breakpoint để in.
- **Live Watch:** đọc biến định kỳ, đã bật 4 lần/giây trong launch ST và Cortex-Debug.
- **`bsp_rtt`:** transport hai kênh không chặn, không phụ thuộc IMU/RC/PWM.
- **`tools/rtt/capture_raw.py`:** bộ đọc TCP tối thiểu, điểm bắt đầu cho công cụ máy tính.

| Channel            | Mục đích                                   | Buffer    |
| ------------------ | ------------------------------------------ | --------- |
| `BSP_RTT_LOG` / 0  | Log văn bản, sự kiện và lỗi                | 2048 byte |
| `BSP_RTT_DATA` / 1 | Bản ghi số liệu cho plot/phân tích sau này | 4096 byte |

Kích thước buffer được đặt tập trung tại `BSP/Inc/SEGGER_RTT_Conf.h`:
`BUFFER_SIZE_UP` cho log, `BSP_RTT_DATA_BUFFER_SIZE` cho data.

Preset Debug bật log khởi tạo/lỗi ở channel 0 và bản ghi cho từng mẫu IMU
đọc thành công ở channel 1. Không có giao diện plot hoặc ghi SD. Logic thử
motor riêng vẫn ở branch `test/imu-rc-pwm`.

## Dùng từ firmware

`Core/main` khởi tạo ngoại vi rồi gọi App. App sở hữu dữ liệu và quyết định
gửi gì, lúc nào. Khi App gọi write, `bsp_rtt` kiểm tra yêu cầu và đếm bản ghi
bị từ chối vì thiếu chỗ; thư viện SEGGER quản lý buffer vòng trong RAM.
OpenOCD trên máy tính đọc RAM qua ST-LINK/SWD rồi chuyển dữ liệu ra TCP.
Terminal/script nhận byte từ TCP để hiển thị hoặc giải mã. Trong firmware
hiện tại App xuất mẫu mới; mẫu cũ không được gửi lại khi NO_DATA/lỗi.

```c
#include "bsp_rtt.h"

/* bsp_rtt_init() đã được gọi trong app_init(). Ví dụ ở foreground: */
const char message[] = "Sensor initialized\n";
bsp_rtt_result_t result =
    bsp_rtt_write(BSP_RTT_LOG, message, sizeof(message) - 1U);
const char record[] = "1000,1,2,3\n"; /* Ví dụ minh họa, chưa phải schema. */
result = bsp_rtt_write(BSP_RTT_DATA, record, sizeof(record) - 1U);
```

API nhận byte và độ dài, dùng được cho văn bản hoặc nhị phân; không tự thêm
NUL/newline, timestamp hay framing. App quyết định khi nào xuất, đơn vị,
định dạng và sequence. Khi thêm dữ liệu raw cần phân
biệt giá trị register cảm biến với dữ liệu đã đổi đơn vị/lọc/hiệu chuẩn.

Mỗi lần ghi là một bản ghi: đủ chỗ thì copy toàn bộ, thiếu chỗ thì trả về
`BSP_RTT_FULL`, không chờ host và không ghi một phần. Hai buffer độc lập. Chỉ một
writer foreground; không gọi từ ISR. Format/copy và truy cập SWD vẫn có chi
phí; RTT không thay thế phép đo timing trên board.

Preset **Debug** đặt `DRONE_ENABLE_RTT=ON`; **Release** đặt `OFF` và không
link thư viện/buffer RTT. Khi tắt, init là no-op và write trả `BSP_RTT_DISABLED`.
Tùy chọn này chỉ điều khiển transport, không điều khiển vòng điều khiển bay.

## Channel 0: timestamp, level và tag

`app_log` nằm ở App, phía trên BSP. Khởi tạo bằng `app_log_init(HAL_GetTick)`
sau RTT, rồi gọi các macro ở foreground:

```c
APP_LOG_INFO("imu", "initialized id=0x%02x", chip_id);
APP_LOG_ERROR("imu.spi", "read failed status=%d", status);
```

Định dạng một sự kiện trên một dòng:

```text
[1250][I][imu] initialized id=0x24
[2300][E][imu.spi] read failed status=5
```

Timestamp là uptime MCU theo ms tại lúc tạo log, uint32 có tràn, không phải
thời điểm thu mẫu trước đó. Level gồm ERROR/WARN/INFO/DEBUG. Tag/scope là tên
module hoặc nhóm chức năng, dài 1–16 ký tự ASCII chữ/số hoặc `._-/`.
Không có registry hoặc scope động.

Mỗi dòng tối đa 255 byte gồm newline; không cấp phát động. Message quá dài
được cắt với `...[truncated]`, CR/LF bên trong thành khoảng trắng. Gửi cả
dòng bằng một lần write; buffer đầy thì BSP đếm và bỏ bản ghi, không tự log
thêm lỗi gửi. Một writer foreground, không dùng từ ISR.

Firmware dùng newlib-nano và chưa bật printf số thực; các lời gọi tích hợp
dùng số nguyên hoặc milli-unit thay vì `%f`.

`DRONE_LOG_LEVEL` chọn OFF/ERROR/WARN/INFO/DEBUG khi configure. Debug mặc định
INFO, Release OFF. Dùng macro `APP_LOG_*` để mức bị tắt không đánh giá đối
số hoặc format. `app_log_write()` là hàm nền trả kết quả RTT, không có cam
kết loại đánh giá đối số nếu gọi trực tiếp. Preset/task build có thể áp lại
giá trị mặc định; dùng CMakeUserPresets khi cần cấu hình riêng.

App chỉ log khởi tạo và chuyển trạng thái lỗi/phục hồi IMU. NO_DATA là trạng
thái bình thường; lỗi lặp ở cùng trạng thái không được in mỗi vòng poll.

## Channel 1: mẫu IMU có timestamp

`app_imu_poll()` chụp `t_read_us` ngay sau lần đọc BSP thành công, trước khi
đổi đơn vị, và lưu cùng mẫu. NO_DATA/lỗi giữ nguyên dữ liệu và timestamp cũ.
`app_telemetry` chỉ mã hóa các trường đã lưu; không lấy thời gian lúc gửi.

Mỗi mẫu mới được gửi một lần, không chủ động giảm mẫu. Tốc độ danh định của
cảm biến là 400 Hz; số mẫu thực tế phụ thuộc polling/timing. CH1 dùng frame
binary 48 byte: header 8 byte (`DRT!`, version, type, length), payload IMU
36 byte và CRC32 4 byte. Không format CSV hoặc gửi nguyên struct C.
CH1 còn xuất thống kê RTT type 2 khoảng mỗi giây, 52 byte/frame, gồm counter
đầy theo kênh, IMU/stats bị bỏ riêng và lỗi đọc IMU. Counter tích lũy được
chụp trước khi thử gửi; không retry khi đầy.
Xem [schema và fixture chung](../../protocol/telemetry.md).

- `sequence`: số lần đọc thành công từ khi init IMU, mẫu đầu là 1;
  vẫn tăng khi RTT bỏ bản ghi, tràn modulo 2^32. Không đếm mẫu phần cứng đã
  ghi đè trước khi MCU đọc.
- `t_read_us`: uptime MCU tại lúc đọc, đơn vị µs, tràn modulo 2^32 (khoảng
  71,6 phút). Không đồng nghĩa thời điểm cảm biến lấy mẫu chính xác.
- `sensor_time_ticks`: clock cảm biến 24 bit nguyên gốc đọc kèm dữ liệu,
  chưa mở rộng khi tràn, không mặc định coi là timestamp acquisition.
- Accel dùng mm/s², gyro dùng mrad/s, cắt phần thập phân về phía 0; chia
  1000 để về m/s² và rad/s. Trục XYZ theo package, chưa hiệu chuẩn/xoay trục.

MCU timestamp kết hợp HAL tick với SysTick countdown; yêu cầu SysTick HCLK
1 ms, clock ổn định, SystemCoreClock nguyên MHz và IRQ được phục vụ kịp thời.
Chỉ đọc ở foreground khi ngắt đang bật; không gọi từ ISR hoặc critical
section. Debug halt, sleep hoặc suspend tick ảnh hưởng nguồn thời gian.
Không dùng phép chia trực tiếp DWT CYCCNT để tránh reset thời gian khi cycle
counter 32 bit tràn sau khoảng 60 giây ở 72 MHz.

`DRONE_ENABLE_TELEMETRY` mặc định ON ở Debug, OFF ở Release; tự vô hiệu khi
RTT tắt. Macro `APP_TELEMETRY_IMU` bỏ cả đánh giá đối số khi trace tắt. Release
không chứa formatter/stream RTT; timestamp mẫu vẫn tồn tại cho ứng dụng dùng.

Máy tính phải ghép frame theo magic/length và kiểm tra CRC; TCP có thể
chia/gộp frame. IMU 400 Hz tạo 19,2 KB/s; vẫn có thể đầy nếu host/probe không theo kịp:
xem `dropped_full[1]` và khoảng trống sequence; timestamp vẫn giữ thời điểm
đọc mẫu, không được sửa thành thời điểm host nhận. `capture_raw.py` chỉ chuyển byte; `view.py` giải mã để vẽ.

## Phân biệt mất dữ liệu và lỗi gọi API

`bsp_rtt_write()` trả mã kết quả, không còn trả bool. Luôn so sánh với
`BSP_RTT_OK` để kiểm tra thành công; giá trị này bằng 0.

| Kết quả                    | Ý nghĩa                                       | Tăng counter buffer đầy |
| -------------------------- | --------------------------------------------- | ----------------------- |
| `BSP_RTT_OK`               | Đã ghi toàn bộ, hoặc bản ghi rỗng hợp lệ      | Không                   |
| `BSP_RTT_FULL`             | Bản ghi hợp lệ nhưng kênh thiếu chỗ trống     | Có, chỉ kênh đó         |
| `BSP_RTT_TOO_LARGE`        | Bản ghi không thể vừa ngay cả khi buffer rỗng | Không                   |
| `BSP_RTT_INVALID_ARGUMENT` | Kênh sai hoặc payload NULL với size khác 0    | Không                   |
| `BSP_RTT_NOT_INITIALIZED`  | Chưa gọi init                                 | Không                   |
| `BSP_RTT_DISABLED`         | Transport bị loại khi build                   | Không                   |

Đọc bằng `bsp_rtt_get_dropped_full(BSP_RTT_LOG)` hoặc
`bsp_rtt_get_dropped_full(BSP_RTT_DATA)`. Bộ đếm tính số bản ghi, không phải
byte, reset khi init lại và tràn modulo 2^32. Trong Live Watch có thể đọc
`'bsp_rtt.c'::dropped_full[0]` và `[1]`; không cần gọi hàm trên target.

Counter chỉ đo bản ghi bị RTT từ chối vì thiếu chỗ. Nó không đo mẫu cảm biến
bị bỏ lỡ, dữ liệu trước lúc tạo bản ghi hoặc lỗi ở phía máy tính. Khi triển
khai luồng đo, App cần gắn sequence/timestamp ngay tại nơi thu mẫu. Nếu giảm
mẫu trước khi gửi, phân biệt số thứ tự mẫu với số thứ tự bản ghi định gửi;
số thứ tự bản ghi vẫn tăng khi RTT từ chối. Timestamp phải ghi rõ là clock
cảm biến hay thời điểm đọc MCU; BSP không tự sửa payload để thêm các trường này.

## Dùng từ máy tính

| Cấu hình VS Code         | GDB server                     | Điểm dừng ban đầu           | RTT TCP                             |
| ------------------------ | ------------------------------ | --------------------------- | ----------------------------------- |
| **Drone: ST-LINK GDB**   | ST-LINK GDB server của ST      | `main`                      | Không được cấu hình trong phiên này |
| **Drone: OpenOCD + RTT** | OpenOCD, probe ST-LINK qua SWD | `app_process`, sau init RTT | LOG 9090, DATA 9091                 |

Hai cấu hình dùng cùng ELF Debug và task `firmware: build Debug` (gọi
`make fw-build PRESET=Debug`). Live Watch của extension ST yêu cầu tốc độ
kiểu chuỗi `"4"`; Cortex-Debug yêu cầu số `4`. Đây là hai schema khác nhau.
Trong Debug Console, extension ST dùng tiền tố `>` cho lệnh GDB, còn
Cortex-Debug nhận lệnh GDB trực tiếp.

1. Mở gốc repository, cài Cortex-Debug và OpenOCD >= 0.12 hỗ trợ ST-LINK/RTT.
2. Chọn **Drone: OpenOCD + RTT**, nhấn F5 để build/nạp Debug.
3. Nhấn Continue khi dừng tại `app_process`. Log channel 0 hiện ở terminal **RTT LOG (CH0)**.
4. Đọc channel 1 bằng `uv run tools/rtt/capture_raw.py --port 9091`.

Debug mặc định sẽ có log khởi tạo và frame IMU binary khi cảm biến đọc được mẫu mới.
Nếu IMU init lỗi, kiểm tra channel 0; không có mẫu cũ được phát lại để lấp chỗ.
Script chỉ chuyển byte, không tự tạo mẫu để vẽ.
Không mở hai debug server dùng cùng ST-LINK.

Nếu chỉ đọc RTT không dùng GDB, đóng phiên debug sau khi nạp rồi chạy:

```sh
openocd -f firmware/drone/debug/rtt.cfg
```

OpenOCD độc lập mở log tại 9090 và data tại 9091. Chạy reader ở terminal khác,
chọn port tương ứng. Xem [công cụ máy tính](../../tools/README.md#các-file-rtt).

## Live Watch và reset

Thêm biến global/static của module cần xem vào view **Live Watch**; ví dụ
`'app.c'::imu.sample_count` nếu debugger cần tên file để định danh biến static.
Các trường có thể được đọc qua nhiều lần cập nhật, không phải snapshot đồng
thời. Watch thường, Pause và breakpoint vẫn có thể cần CPU dừng.

RTT có thể báo `No control block found` trước khi init chạy; extension tự thử
lại. Nếu PC ở vùng system ROM `0x1FFFF...`, kiểm tra BOOT0 để board tự boot
Flash. OpenOCD dùng reset phần mềm `SYSRESETREQ`, không yêu cầu NRST.
Nếu reset vẫn đưa CPU vào ROM, cần kiểm tra nguyên nhân boot/reset trước
khi tiếp tục chạy firmware.

## Kiểm tra transport

```sh
cmake -S firmware/drone/tests/rtt -B /tmp/drone-rtt-tests -G Ninja
cmake --build /tmp/drone-rtt-tests
ctest --test-dir /tmp/drone-rtt-tests --output-on-failure
```

Test dùng thư viện RTT thật để kiểm tra byte nhị phân, tham số sai, buffer
đầy, phân loại kết quả/counter, ghi nguyên bản ghi, hai kênh độc lập và ring wrap. Không xác nhận timing
hoặc truyền SWD với phần cứng.

Kiểm tra log/trace, clock và timestamp mẫu trên host:

```sh
cmake -S firmware/drone/tests/logging -B /tmp/drone-logging-tests -G Ninja
cmake --build /tmp/drone-logging-tests
ctest --test-dir /tmp/drone-logging-tests --output-on-failure
cmake -S firmware/drone/tests/time -B /tmp/drone-time-tests -G Ninja
cmake --build /tmp/drone-time-tests
ctest --test-dir /tmp/drone-time-tests --output-on-failure
```

Các test kiểm tra format/truncation, mức lọc và đối số bị loại, backpressure,
sequence/timestamp, IRQ tick đang chờ, tick wrap, NO_DATA/lỗi và tích hợp App.
Nguồn µs và chi phí stream cần đo lại trên board trước khi dùng để kết luận timing.
