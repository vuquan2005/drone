# Khung debug

## Các phần đang có

- **ST-LINK/SWD + GDB:** nạp, Pause, breakpoint, stepping và xem biến.
- **OpenOCD + RTT:** đọc buffer RAM khi CPU chạy, không cần breakpoint để in.
- **Live Watch:** đọc biến định kỳ, đã bật 4 lần/giây trong launch ST và Cortex-Debug.
- **`bsp_rtt`:** transport hai kênh không chặn, không phụ thuộc IMU/RC/PWM.
- **`tools/rtt_read.py`:** bộ đọc TCP tối thiểu, điểm bắt đầu cho công cụ máy tính.

| Channel            | Mục đích                                   | Buffer    |
| ------------------ | ------------------------------------------ | --------- |
| `BSP_RTT_LOG` / 0  | Log văn bản, sự kiện và lỗi                | 2048 byte |
| `BSP_RTT_DATA` / 1 | Bản ghi số liệu cho plot/phân tích sau này | 4096 byte |

Kích thước buffer được đặt tập trung tại `BSP/Inc/SEGGER_RTT_Conf.h`:
`BUFFER_SIZE_UP` cho log, `BSP_RTT_DATA_BUFFER_SIZE` cho data.

Ứng dụng hiện chỉ gọi `bsp_rtt_init()` khi khởi tạo. Chưa có log định kỳ,
snapshot chung, stream IMU, giao diện plot hoặc ghi SD. Các bản thử đầy đủ
vẫn ở branch `test/imu-rc-pwm`.

## Dùng từ firmware

```c
#include "bsp_rtt.h"

/* bsp_rtt_init() đã được gọi trong app_init(). Ví dụ ở foreground: */
const char message[] = "Sensor initialized\n";
bool queued = bsp_rtt_write(BSP_RTT_LOG, message, sizeof(message) - 1U);
const char record[] = "1000,1,2,3\n"; /* Ví dụ minh họa, chưa phải schema. */
queued = bsp_rtt_write(BSP_RTT_DATA, record, sizeof(record) - 1U);
```

API nhận byte và độ dài, dùng được cho văn bản hoặc nhị phân; không tự thêm
NUL/newline, timestamp hay framing. App quyết định khi nào xuất, đơn vị,
định dạng, sequence và cách đếm bản ghi bị bỏ. Khi thêm dữ liệu raw cần phân
biệt giá trị register cảm biến với dữ liệu đã đổi đơn vị/lọc/hiệu chuẩn.

Mỗi lần ghi là một bản ghi: đủ chỗ thì copy toàn bộ, thiếu chỗ thì trả về
`false`, không chờ host và không ghi một phần. Hai buffer độc lập. Chỉ một
writer foreground; không gọi từ ISR. Format/copy và truy cập SWD vẫn có chi
phí; RTT không thay thế phép đo timing trên board.

Preset **Debug** đặt `DRONE_ENABLE_RTT=ON`; **Release** đặt `OFF` và không
link thư viện/buffer RTT. Khi tắt, init là no-op và write luôn trả `false`.
Tùy chọn này chỉ điều khiển transport, không điều khiển vòng điều khiển bay.

## Dùng từ máy tính

1. Mở gốc repository, cài Cortex-Debug và OpenOCD >= 0.12 hỗ trợ ST-LINK/RTT.
2. Chọn **STM32: Debug + RTT (OpenOCD)**, nhấn F5 để build/nạp Debug.
3. Nhấn Continue khi dừng tại `app_process`. Log channel 0 hiện ở terminal **Log**.
4. Đọc channel 1 bằng `python3 tools/rtt_read.py --port 9091`.

Chưa có nơi gọi write thì hai kênh sẽ không có dữ liệu; đó là trạng thái bình
thường của bộ khung. Script chỉ chuyển byte, không tự tạo mẫu để vẽ.
Không mở hai debug server dùng cùng ST-LINK.

Nếu chỉ đọc RTT không dùng GDB, đóng phiên debug sau khi nạp rồi chạy:

```sh
openocd -f firmware/drone/debug/rtt.cfg
```

OpenOCD độc lập mở log tại 9090 và data tại 9091. Chạy reader ở terminal khác,
chọn port tương ứng. Xem [công cụ máy tính](../../tools/README.md#khung-đọc-rtt).

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
đầy, ghi nguyên bản ghi, hai kênh độc lập và ring wrap. Không xác nhận timing
hoặc truyền SWD với phần cứng.
