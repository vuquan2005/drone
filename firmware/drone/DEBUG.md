# Khung debug

ST-LINK/SWD + GDB dùng để nạp, đặt breakpoint và xem biến; OpenOCD + RTT
đọc buffer RAM khi CPU chạy, chuyển log và telemetry ra TCP.

## Dùng từ máy tính

| Cấu hình VS Code         | Điểm dừng ban đầu           | RTT TCP             |
| ------------------------ | --------------------------- | ------------------- |
| **Drone: ST-LINK GDB**   | `main`                      | Không cấu hình RTT  |
| **Drone: OpenOCD + RTT** | `app_process`, sau init RTT | LOG 9090, DATA 9091 |

Hai cấu hình dùng ELF Debug và build bằng `make fw-build PRESET=Debug`.
Cấu hình OpenOCD + RTT còn tự mở viewer bằng task nền `telemetry: view`. Chạy từ gốc repository:

1. Cài Cortex-Debug và OpenOCD >= 0.12 hỗ trợ ST-LINK/RTT.
2. Chọn **Drone: OpenOCD + RTT**, nhấn F5 để build/nạp.
3. Nhấn Continue khi dừng tại `app_process`; log hiện ở **RTT LOG (CH0)**.
4. Viewer tự mở để xem IMU, RC và ghi phiên; không cần chạy `make telemetry`.
   Sau khi dừng debug, viewer vẫn mở để xem/lưu dữ liệu và chờ nối lại.
   Đóng cửa sổ viewer để kết thúc task.

Nếu chỉ đọc RTT, đóng phiên debug sau khi nạp rồi chạy:

```sh
openocd -f firmware/drone/debug/rtt.cfg
```

Cấu hình độc lập cũng mở LOG 9090 và DATA 9091. Không mở hai debug server
cùng dùng ST-LINK. Cách đọc byte thô, cài GUI và ghi dữ liệu nằm trong
[công cụ máy tính](../../tools/rtt/README.md).

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
cũ. CH1 gồm IMU raw type 4, thống kê RTT type 2 khoảng mỗi giây và snapshot
RC type 3 mỗi 50 ms (16 kênh raw và trạng thái kết nối). Layout,
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

## Kiểm tra luồng rate/mixer

Cả Debug và Release bật `DRONE_ENABLE_RATE_CONTROL`: `app_init()` tự cấu hình P = 0,1,
I = D = 0, giới hạn mỗi trục 2 rad/s và chu kỳ 2000 µs (500 Hz). Đây là
cấu hình thử trên bộ gá, chưa tune cho bay; telemetry mặc định chỉ bật ở Debug.
Có thể gọi lại `app_configure_rate()` trong foreground với `app_rate_config_t`: PID từng trục,
`max_rate_radps` và `period_us`. Không có bộ gain mặc định đã tune cho bay.
`period_us = 2000` giới hạn rate ở 500 Hz dù IMU cung cấp mẫu ở 1 kHz.
Nếu IMU chậm hơn, rate chờ mẫu mới; không tính lặp lại cùng mẫu hoặc chạy bù.
`dt` dùng timestamp đọc của hai mẫu thực sự được controller tiêu thụ.

`process_imu()` đọc mẫu và telemetry; `process_rate()` kiểm tra RC, lỗi/tuổi
mẫu IMU và chu kỳ rate riêng. Mẫu quá 20 ms hoặc khoảng cách giữa hai bước PID
quá 20 ms làm reset trạng thái. Đổi cấu hình cũng reset PID và residual.
Accel và gyro SI trong App dùng FRD, ánh xạ gá `[-X, +Y, -Z]`.
Controller nhận trực tiếp FRD; telemetry raw giữ nguyên counts với `axis_map=2`.
Khi giảm mẫu đưa vào rate, cần cấu hình/kiểm chứng lọc gyro thích hợp;
luồng hiện tại lấy mẫu mới nhất, chưa thêm bộ lọc chống alias.

Đọc `app_get_rate_result()` sau `app_process()` để lấy kết quả tính toán;
kiểm tra giá trị trả về trước khi sử dụng. Giữa các kỳ rate, kết quả hợp lệ
trước đó được giữ lại. `app_safety` kiểm tra arm/failsafe trước khi cho phép
ghi kết quả mixer ra ESC; `arm_requested` riêng lẻ không phải quyền chạy motor.

### Thử trên board

1. Tháo cánh, build/nạp preset Debug bằng `make fw-flash`, rồi mở
   `make telemetry` theo hướng dẫn kết nối RTT ở trên.
2. Kiểm tra log `rate` báo `configured=1`, mở tab **Rate · FRD**.
   Bật bộ phát/thu: `valid=True` khi có RC hợp lệ và IMU mới; `dt` kỳ vọng
   khoảng 2000 µs hoặc lớn hơn nếu vòng foreground/IMU không theo kịp.
3. Đưa cần roll/pitch/yaw qua hai đầu: setpoint tương ứng khoảng ±2 rad/s.
   Đặt cần giữa và xoay khung: gyro FRD dương phải tạo PID âm trên cùng trục.
   Đứng yên có thể thấy gyro bias vì chưa hiệu chuẩn.
4. Tắt bộ phát hoặc ngắt RC: bản tin phải báo `valid=False`, PID/motor bằng 0.
   Lỗi IMU hoặc mẫu quá 20 ms cũng reset kết quả. Giá trị 0 này là trạng thái
   tính toán; App đồng thời đưa PWM về 1000 µs.
5. Bấm **Start recording** để lưu `rate_*.csv` cùng IMU, RC và log trong
   thư mục phiên tại `experiments/`. Telemetry rate lấy snapshot 20 Hz,
   không giữ mọi bước PID 500 Hz; chưa dùng để kết luận jitter/timing toàn bộ.

Tab Rate có sẵn sáu plot trước khi nhận tín hiệu: setpoint/gyro từng trục FRD,
PID, mixer và residual; trace chỉ xuất hiện khi có snapshot hợp lệ. Snapshot
không hợp lệ tạo khoảng trống, không vẽ giá trị 0 như dữ liệu thật.

### Arm/failsafe và thử ESC

- PB6/PB7/PB8/PB9 nối M1/M2/M3/M4 theo thứ tự mixer đã xác nhận.
- Tháo cánh trước khi cấp nguồn ESC. Ga <=2%, CH7 không ở HIGH, RC/IMU khỏe,
  đưa CH5 OFF rồi ON để arm. ON từ lúc boot hoặc vừa phục hồi không arm.
- Khi armed nhưng ga <=2%, PID được reset và cả bốn ESC nhận 1000 µs.
  Tăng ga mới xuất mixer; không có idle quay tự động.
- OFF disarm ngay trong vòng foreground; mất RC (timeout 100 ms), lỗi IMU,
  mẫu quá 20 ms, lỗi tính rate hoặc yêu cầu hiệu chuẩn ESC gây FAILSAFE.
  Failsafe gửi 1000 µs và cần OFF → ON lại. Nếu ghi PWM lỗi thì tắt tín hiệu,
  khóa PWM đến `app_init()`; không tự khởi động lại output.
- Ánh xạ thrust → ESC hiện tuyến tính 0..1 → 1000..2000 µs, chưa hiệu chuẩn.
  PID P=0,1 là gain thử. Cần đo chiều motor, xung, trễ failsafe và đáp ứng trên
  bộ gá trước khi dùng cánh. Điều khiển/failsafe chạy trong foreground, chưa có
  watchdog xử lý CPU treo; không suy ra bảo vệ khi vòng lặp ngừng chạy.

`app_get_arm_state()` trả DISARMED=0, ARMED=1, FAILSAFE=2; RTT log tag `arm`
ghi chuyển trạng thái liên quan ARMED và lỗi ghi PWM. CH6 hiện chưa chọn mode
bay; CH7 HIGH chỉ chặn arm/đưa vào failsafe, chưa chạy hiệu chuẩn ESC.
