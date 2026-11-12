# Khung debug

ST-LINK/SWD + GDB dùng để nạp, đặt breakpoint và xem biến; OpenOCD + RTT
đọc buffer RAM khi CPU chạy, chuyển log và telemetry ra TCP.

## Dùng từ máy tính

| Cấu hình VS Code         | Điểm dừng ban đầu           | RTT TCP             |
| ------------------------ | --------------------------- | ------------------- |
| **Drone: ST-LINK GDB**   | `main`                      | Không cấu hình RTT  |
| **Drone: OpenOCD + RTT** | `app_process`, sau init RTT | LOG 9090, DATA 9091 |

Hai cấu hình dùng ELF Debug và task `firmware: build Debug`
(`make fw-build PRESET=Debug`). Chạy từ gốc repository:

1. Cài Cortex-Debug và OpenOCD >= 0.12 hỗ trợ ST-LINK/RTT.
2. Chọn **Drone: OpenOCD + RTT**, nhấn F5 để build/nạp.
3. Nhấn Continue khi dừng tại `app_process`; log hiện ở **RTT LOG (CH0)**.
4. Chạy `make telemetry` ở terminal khác để xem đồ thị và ghi phiên.

Nếu chỉ đọc RTT, đóng phiên debug sau khi nạp rồi chạy:

```sh
openocd -f firmware/drone/debug/rtt.cfg
```

Cấu hình độc lập cũng mở LOG 9090 và DATA 9091. Không mở hai debug server
cùng dùng ST-LINK. Cách đọc byte thô, cài GUI và ghi dữ liệu nằm trong
[công cụ máy tính](../../tools/README.md#các-file-rtt).

## Cấu hình build

| Tùy chọn                 | Debug mặc định | Release mặc định |
| ------------------------ | -------------- | ---------------- |
| `DRONE_ENABLE_RTT`       | ON             | OFF              |
| `DRONE_ENABLE_TELEMETRY` | ON             | OFF              |
| `DRONE_LOG_LEVEL`        | INFO           | OFF              |

Telemetry tự vô hiệu khi RTT tắt. Release mặc định không link thư viện/buffer
RTT; timestamp mẫu vẫn được lưu cho ứng dụng. Mức log có thể chọn
OFF/ERROR/WARN/INFO/DEBUG. Dùng `CMakeUserPresets.json` khi cần cấu hình riêng
và lưu ý preset/task có thể áp lại mặc định.

## Dùng từ firmware

`app_init()` khởi tạo RTT và log. App quyết định nội dung và thời điểm gửi;
BSP chỉ chuyển byte qua hai buffer độc lập:

| Channel            | Nội dung           | Buffer    |
| ------------------ | ------------------ | --------- |
| `BSP_RTT_LOG` / 0  | Log sự kiện và lỗi | 2048 byte |
| `BSP_RTT_DATA` / 1 | Telemetry binary   | 4096 byte |

Kích thước đặt tại `BSP/Inc/SEGGER_RTT_Conf.h`: `BUFFER_SIZE_UP` cho log,
`BSP_RTT_DATA_BUFFER_SIZE` cho data.

Mỗi lần ghi nhận toàn bộ bản ghi hoặc trả lỗi; đầy thì bỏ bản ghi mới,
không chờ host hoặc retry. Chỉ dùng một writer foreground, không gọi từ ISR.
API không tự thêm timestamp, framing hay newline; hợp đồng đầy đủ nằm trong
[BSP/Inc/bsp_rtt.h](BSP/Inc/bsp_rtt.h).

Telemetry gọi `bsp_rtt_prepare_write()` trước encode/CRC để bỏ sớm khi đầy.
Prepare tính một lần thử gửi, không reserve byte hoặc dùng để poll chờ chỗ:
nếu lỗi thì dừng, nếu OK thì encode và gọi write; không xen write/init khác.
IMU vẫn xác thực/scale trước prepare; đọc mẫu và tăng sequence tiếp tục khi đầy.

### Channel 0: log

Dùng macro `APP_LOG_*` để mức bị tắt không đánh giá đối số hoặc format:

```c
APP_LOG_INFO("imu", "initialized id=0x%02x", chip_id);
APP_LOG_ERROR("imu.spi", "read failed status=%d", status);
```

```text
[1250][I][imu] initialized id=0x24
[2300][E][imu.spi] read failed status=5
```

Timestamp là uptime MCU theo ms lúc tạo log, uint32 có tràn. Dòng tối đa
255 byte gồm newline; message dài được cắt với `...[truncated]`.
Dùng số nguyên hoặc milli-unit vì printf số thực chưa bật. Quy tắc tag và
format xem [App/Inc/app_log.h](App/Inc/app_log.h).

App log khởi tạo và chuyển trạng thái lỗi/phục hồi IMU; NO_DATA và lỗi lặp
cùng trạng thái không tạo log mỗi vòng poll.

### Channel 1: mẫu IMU có timestamp

Mỗi mẫu đọc thành công được thử gửi một lần; NO_DATA/lỗi không gửi lại mẫu
cũ. CH1 gồm IMU type 1 và thống kê RTT type 2 khoảng mỗi giây. Layout,
đơn vị, sequence và counter nằm trong [đặc tả telemetry](../../protocol/telemetry.md).

`app_imu_poll()` chụp `t_read_us` sau đọc BSP thành công, trước đổi đơn vị;
telemetry dùng timestamp đã lưu. Đây là thời điểm đọc MCU, không phải thời
điểm lấy mẫu cảm biến hoặc host nhận. Uptime µs tràn sau khoảng 71,6 phút.

Nguồn thời gian dùng HAL tick và SysTick countdown: yêu cầu SysTick HCLK
1 ms, clock ổn định, `SystemCoreClock` nguyên MHz và IRQ được phục vụ kịp.
Chỉ đọc ở foreground khi ngắt bật; halt, sleep hoặc suspend tick ảnh hưởng
thời gian. Xem [BSP/Inc/bsp_time.h](BSP/Inc/bsp_time.h) khi đổi clock/lịch chạy.

## Theo dõi mất dữ liệu

So kết quả ghi với `BSP_RTT_OK` (bằng 0). Chỉ `BSP_RTT_FULL` tăng counter
đầy; tham số sai, bản ghi quá lớn hoặc transport chưa sẵn sàng không tăng.

Live Watch có thể đọc `'bsp_rtt.c'::dropped_full[0]` và `[1]`. Counter đếm
bản ghi, reset khi init và tràn modulo 2^32. Chúng không đo mẫu phần cứng
bị bỏ lỡ hoặc dữ liệu mất trên host. Để đối chiếu gap IMU, dùng chênh lệch
`imu_tx_full` trong type 2 theo [quy tắc counter](../../protocol/telemetry.md#rtt-statistics-v1-type-2).

## Live Watch và reset

Live Watch đã bật 4 lần/giây. Thêm biến global/static, ví dụ
`'app.c'::imu.sample_count`; các trường không phải snapshot đồng thời.
Watch thường, Pause và breakpoint có thể cần CPU dừng.

Trong Debug Console, extension ST dùng tiền tố `>` cho lệnh GDB;
Cortex-Debug nhận lệnh trực tiếp. Khi sửa launch, tốc độ Live Watch của ST
là chuỗi `"4"`, còn Cortex-Debug là số `4`.

- `No control block found`: có thể xuất hiện trước init RTT; extension tự thử lại.
- PC ở `0x1FFFF...`: kiểm tra BOOT0 và nguyên nhân boot/reset để chạy từ Flash.
  OpenOCD dùng `SYSRESETREQ`, không yêu cầu NRST.
- Không có IMU: xem log init/lỗi CH0 và xác nhận CPU đã Continue.

## Kiểm tra transport

Chạy từ gốc repository; thay `rtt` bằng `logging` hoặc `time` để kiểm tra
log/telemetry và nguồn timestamp:

```sh
cmake -S firmware/drone/tests/rtt -B /tmp/drone-rtt-tests -G Ninja
cmake --build /tmp/drone-rtt-tests
ctest --test-dir /tmp/drone-rtt-tests --output-on-failure
```

Test host kiểm tra buffer, format, counter và timestamp logic. Cần đo trên
board để xác nhận SWD, nguồn µs và chi phí RTT trước khi kết luận timing.
