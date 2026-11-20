# Công cụ RTT

Viewer nhận log và telemetry theo đường firmware → RTT RAM → OpenOCD/SWD →
TCP. Cần Python 3.10+, uv, OpenOCD và các dependency GUI. Chạy từ gốc repo:

```sh
uv sync --locked --group rtt
make telemetry
```

Cách nạp firmware và chạy OpenOCD xem [debug](../../firmware/drone/DEBUG.md#dùng-từ-máy-tính).
Launch VS Code **Drone: OpenOCD + RTT** tự mở viewer; nhấn Continue tại
`app_process` để nhận dữ liệu. Viewer chờ và tự nối lại nếu RTT chưa sẵn sàng.
LOG channel 0 mặc định dùng TCP 9090, DATA channel 1 dùng 9091. Có thể đổi:

```sh
make telemetry TELEMETRY_ARGS="--host 127.0.0.1 --log-port 9090 --data-port 9091"
```

Đọc byte thô không cần dependency GUI:

```sh
uv run tools/rtt/capture_raw.py --port 9091 > /tmp/imu.bin
uv run tools/rtt/capture_raw.py --port 9090 > /tmp/drone-rtt.log
```

## Xem và ghi dữ liệu

Viewer hiển thị log có filter, IMU, receiver và rate/mixer. IMU hiển thị
m/s² và rad/s trong hệ thân FRD; receiver hiển thị CH1–CH16 dạng raw CRSF
0–2047. `connected` phản ánh tuổi frame RC hợp lệ, còn `telemetry stale`
chỉ báo snapshot cũ hoặc TCP DATA ngắt. Tab **Rate · FRD** nhận snapshot
20 Hz, không phải mọi bước PID. Tab **Attitude** hiển thị mô hình quadrotor với ba trục thân và đồ thị roll, pitch
và yaw tương đối từ quaternion Mahony (tối đa 20 Hz). Bản ghi
`attitude_*.csv` lưu quaternion gốc để xem lại. **Diagnostics** hiển thị counter host, MCU
và lỗi ghi; tốc độ nhận host không phải phép đo chính xác tần số lấy mẫu.

Tab **Attitude Control** hiển thị góc đặt và góc ước lượng roll/pitch, sai số
nghiêng X/Y trong hệ thân và tốc độ góc lệnh trước giới hạn rate. Với SC
MIDDLE/HIGH, firmware thử gửi một bản tin cho mỗi lần tính vòng attitude
(danh định 100 Hz). Đồ thị dùng thời gian MCU, ngắt nét khi thiếu số thứ tự
hoặc khoảng cách giữa hai bản tin vượt 50 ms. Bản ghi `attitude_control_*.csv` hỗ trợ xem
lại trong viewer.

Mở **Recording options**, chọn thư mục và **Start recording**; có thể bật
**Save raw hex**. Hoặc chạy:

```sh
make telemetry TELEMETRY_ARGS="--record --raw"
make telemetry TELEMETRY_ARGS="--record --output-dir /tmp/drone-sessions"
```

Mặc định mỗi phiên được lưu riêng trong `experiments/raw/telemetry/` (Git
ignore), gồm `imu_raw_*.csv`, `rc_*.csv`, `rate_*.csv`, `rtt_stats_*.csv`,
`attitude_*.csv`, `attitude_control_*.csv`, `events_*.log` và `data_*.hex` nếu bật
raw. IMU CSV giữ counts gốc cùng range,
ODR và mã ánh xạ trục; raw hex giữ byte DATA trước decode. Log được ghi kể cả
khi bị filter trên UI. Queue ghi đầy hoặc lỗi ghi có thể làm phiên thiếu dữ
liệu; kiểm tra writer drops và trạng thái recording trước khi phân tích.

Chọn **Open session…** và thư mục phiên để xem dữ liệu đã lưu, **Segment** để
chọn đoạn, hoặc **Return to live** để nối lại. Viewer không sửa file nguồn;
không mở phiên khi đang ghi. Log hiển thị tối đa 2000 dòng cuối và không đồng
bộ theo segment; các đồ thị không có trục thời gian chung. File chỉ có log
chưa được hỗ trợ.

## Diễn giải dữ liệu

Segment tăng khi DATA nối lại hoặc nghi reset; không so gap qua ranh giới này.
Thiếu boot ID nên không phải mọi reset đều được phát hiện. Trong cùng segment,
hiệu `t_read_us` modulo 2^32 giữa sequence liên tiếp cho biết khoảng cách đọc
MCU, không trực tiếp đo jitter lấy mẫu cảm biến. Tách gap khỏi thống kê và
đối chiếu `imu_tx_full` cùng writer drops. Chi tiết frame, đơn vị, ánh xạ trục
và counter nằm trong [đặc tả telemetry](../../protocol/telemetry.md).

Viewer không hỗ trợ telemetry IMU type 1 hoặc CSV `IMU1` cũ; dùng phiên bản
công cụ trước đó nếu cần xem lại. Type chưa hỗ trợ được đếm là unknown.

## Kiểm thử

Parser và writer không cần Qt/board; test GUI dùng TCP localhost giả lập:

```sh
uv run python -m unittest discover -s tools/tests -p test_rtt_records.py -v
uv run python -m unittest discover -s tools/tests -p test_rtt_telemetry.py -v
uv run python -m unittest discover -s tools/tests -p test_rtt_session_writer.py -v
QT_QPA_PLATFORM=offscreen uv run --group rtt python -m unittest discover -s tools/tests -p test_rtt_view.py -v
```
