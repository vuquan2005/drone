# Debug và thử firmware

ST-LINK/SWD và GDB dùng để nạp, đặt breakpoint và xem biến. OpenOCD đọc RTT
qua SWD và chuyển log/telemetry tới công cụ trên máy tính.

## Dùng từ máy tính

Mở gốc repository trong VS Code và cài các extension/công cụ tương ứng:

| Cấu hình                 | Yêu cầu                                                             | Điểm dừng đầu | RTT                 |
| ------------------------ | ------------------------------------------------------------------- | ------------- | ------------------- |
| **Drone: ST-LINK GDB**   | STM32CubeIDE extension và ST-LINK GDB Server                        | `main`        | Không               |
| **Drone: OpenOCD + RTT** | Cortex-Debug, OpenOCD ≥ 0.12 và [viewer](../../tools/rtt/README.md) | `app_process` | LOG 9090, DATA 9091 |

Cả hai cấu hình build và nạp preset Debug khi nhấn F5. Với OpenOCD + RTT,
viewer tự mở; nhấn Continue tại `app_process` để nhận dữ liệu. Viewer vẫn mở
và chờ kết nối lại sau khi dừng debug; đóng cửa sổ để kết thúc task.

Sau khi nạp, có thể đọc RTT bằng OpenOCD độc lập và mở viewer riêng:

```sh
openocd -f firmware/drone/debug/rtt.cfg
```

Không chạy hai debug server cùng dùng ST-LINK. Xem cách mở viewer, ghi phiên
và đọc dữ liệu thô tại [tools/rtt/README.md](../../tools/rtt/README.md).
Nếu không kết nối, kiểm tra nguồn, SWD, quyền USB và probe; nếu không có dữ
liệu IMU, kiểm tra log và bảo đảm CPU đã Continue.

## Cấu hình build

| Tùy chọn                   | Debug mặc định | Release mặc định |
| -------------------------- | -------------- | ---------------- |
| `DRONE_ENABLE_RTT`         | ON             | OFF              |
| `DRONE_ENABLE_TELEMETRY`   | ON             | OFF              |
| `DRONE_LOG_LEVEL`          | INFO           | OFF              |
| `DRONE_DEBUG_FORCE_FLIGHT` | ON             | OFF              |

Telemetry cần RTT. Debug mặc định vào FLIGHT không qua chọn mode bằng RC;
tắt `DRONE_DEBUG_FORCE_FLIGHT` khi cần thử chọn mode hoặc calib. Dùng
`CMakeUserPresets.json` cho cấu hình riêng và kiểm tra preset/task có thể áp
lại mặc định. Chi tiết chọn mode nằm trong [README](README.md#chọn-chế-độ-sau-reset).

## RTT và Live Watch

Runtime khởi tạo log/telemetry trước khi chọn mode. Channel 0 mang log văn bản;
channel 1 mang telemetry nhị phân. Ghi RTT không chặn: khi buffer đầy bản ghi
mới bị bỏ. API chỉ dùng trong foreground; xem [bsp_rtt.h](BSP/Inc/bsp_rtt.h),
[app_log.h](App/Inc/app_log.h) và [đặc tả telemetry](../../protocol/telemetry.md).
Timestamp IMU là thời điểm MCU đọc, không phải thời điểm cảm biến lấy mẫu.

Khi bật telemetry, firmware đưa mọi mẫu IMU đọc thành công vào nhóm RTT trong
FLIGHT và IMU_CALIB. Mỗi nhóm chứa tối đa 8 mẫu và được gửi sau khoảng 10 ms
nếu chưa đủ mẫu. Nhóm đủ 8 mẫu dài 141 byte, tương ứng 14.100 B/s ở 800 mẫu/s.

Mức `INFO` ghi trạng thái chọn mode, cấu hình IMU do driver yêu cầu, hệ số
hiệu chuẩn được áp dụng và cấu hình các vòng điều khiển. Các trường có hậu tố
`_x1e6` lưu giá trị đã nhân một triệu, ví dụ `kp_s_per_rad_x1e6=100000` là
`0,1 s/rad`. Để giữ các dòng đầu phiên trong `events_*.log`, bật
**Start recording** trước khi reset board.

Live Watch có thể đọc biến global/static, ví dụ `'app.c'::imu.sample_count`;
các trường không được chụp đồng thời. `dropped_full` trong `bsp_rtt.c` đếm
bản ghi bị bỏ do buffer đầy, không đo mẫu cảm biến bị bỏ lỡ hoặc mất trên host.
Xem quy tắc counter trong đặc tả telemetry để đối chiếu gap.

## Thử trên board

**Tháo cánh trước khi cấp nguồn ESC.** Kiểm tra wiring và chiều motor theo
[README](README.md#pwm-esc). Gain mặc định P=0,1, I=D=0, giới hạn 2 rad/s,
chu kỳ rate 2000 µs là cấu hình thử bộ gá, chưa tune để bay. Rate dùng mẫu
IMU mới nhất; chưa có lọc chống alias khi giảm tốc độ mẫu. Cần đo I/O, timing,
đáp ứng và hiệu chuẩn trước khi dùng cánh.

1. Build/nạp Debug, mở RTT và kiểm tra log `rate` có `configured` và các dòng
   `axis_config`. Bật radio/receiver, mở tab **Rate · FRD** và kiểm tra RC, IMU.
2. Đưa từng cần roll/pitch/yaw qua hai đầu: setpoint tương ứng khoảng
   ±2 rad/s. Đặt cần giữa và xoay khung: gyro FRD dương phải tạo PID âm trên
   cùng trục. Bias có thể còn do chưa hiệu chuẩn.
3. Với ga ≤2%, CH7 không HIGH và RC/IMU khỏe, đưa CH5 OFF rồi ON để arm.
   ON từ lúc boot hoặc khi vừa phục hồi không arm. Khi armed và ga ≤2%, PID
   reset và ESC nhận 1000 µs; tăng ga mới xuất mixer.
4. Tắt radio hoặc ngắt RC và kiểm tra failsafe: PWM về 1000 µs. RC timeout
   100 ms, lỗi IMU/mẫu quá 20 ms, lỗi rate hoặc yêu cầu calib cũng chặn motor.
   Cần tín hiệu khỏe và OFF → ON lại sau failsafe. Lỗi ghi PWM tắt tín hiệu
   và khóa đến lần khởi tạo App tiếp theo.
5. Ghi phiên trong viewer nếu cần phân tích. Snapshot rate chỉ 20 Hz, không
   ghi mọi bước PID và không đủ để kết luận jitter toàn vòng điều khiển.

Điều khiển và failsafe chạy trong foreground, chưa có watchdog xử lý CPU treo.
Đo thực tế xung và độ trễ failsafe; không suy ra bảo vệ khi vòng lặp ngừng chạy.

## Kiểm thử host

Ví dụ kiểm tra RTT; thay `rtt` bằng `logging` hoặc `time` để chọn bộ khác:

```sh
cmake -S firmware/drone/tests/rtt -B /tmp/drone-rtt-tests -G Ninja
cmake --build /tmp/drone-rtt-tests
ctest --test-dir /tmp/drone-rtt-tests --output-on-failure
```

Test host không xác nhận SWD, nguồn thời gian hay chi phí RTT trên board.
