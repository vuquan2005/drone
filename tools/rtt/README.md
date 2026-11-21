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

## Xuất telemetry đã ghi trong flash

Dùng firmware bật RTT, reset board với **SD HIGH + SB LOW** và đợi tay
điều khiển báo `EXPORT WAIT`. Mở `make telemetry`, dừng ghi live nếu
đang ghi, rồi bấm **Export flash**. Viewer dùng thư mục trong
**Recording options → Save folder** và tạo thư mục con `flash_export_*`.
Trong lúc xuất, viewer tạm dừng kết nối live và hiển thị số trang đã
nhận. **Cancel export** hủy lần truyền về máy.

`flash.bin` là ảnh 16 MiB của chip, giữ cả dữ liệu cũ ở các trang journal
không nhận diện được. `export.json` tóm tắt kết quả. Dùng **Open session**
và chọn một thư mục `session_*` bên trong để xem CSV và cấu hình đã lưu.
Nếu ngắt kết nối giữa chừng, file còn tên `flash.bin.part`. Bấm
**Export flash** lần nữa để đọc lại từ đầu vào một thư mục mới.

Có thể dùng CLI khi cần tự động hóa hoặc chạy không có GUI. Giữ OpenOCD
đang chạy với cấu hình RTT, đóng viewer và chạy từ gốc repo:

```sh
python3 tools/rtt/flash_export.py experiments/raw/telemetry/flash-export-01
```

CLI mặc định dùng cổng OpenOCD 9090 và 9091. Có thể đổi bằng `--host`,
`--control-port` và `--data-port`.

## Xem và ghi dữ liệu

Viewer hiển thị log có filter, IMU, receiver và rate/mixer. IMU hiển thị
m/s² và rad/s trong hệ thân FRD. Với firmware mới, receiver hiển thị bốn
trục CH1–CH4 dạng raw CRSF 0–2047 cùng trạng thái cần gạt SA/SB/SC/SD.
Bản ghi RC cũ vẫn hiển thị đủ CH1–CH16. `connected` phản ánh tuổi frame RC
hợp lệ, còn `telemetry stale`
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
ignore), gồm `imu_raw_*.csv`, `receiver_compact_*.csv` (RC mới), `rc_*.csv` (RC cũ),
`rate_*.csv`, `rtt_stats_*.csv`,
`attitude_*.csv`, `attitude_control_*.csv`, `events_*.log` và `data_*.hex` nếu bật
raw. Viewer đọc cả type 4 và type 9. IMU CSV giữ một dòng mỗi mẫu, gồm
counts gốc, timestamp, range, ODR và mã ánh xạ trục. Với type 9, viewer điền
cấu hình cố định theo đặc tả. Raw hex giữ byte DATA trước decode. Log được ghi kể cả
khi bị filter trên UI. Queue ghi đầy hoặc lỗi ghi có thể làm phiên thiếu dữ
liệu; kiểm tra writer drops và trạng thái recording trước khi phân tích.

Tab **Configuration** hiển thị cấu hình IMU và các vòng điều khiển. Viewer
yêu cầu snapshot qua cổng LOG khi hai kết nối RTT sẵn sàng. Firmware trả
snapshot qua cổng DATA. Viewer lưu một dòng mỗi segment trong `config.jsonl`
khi bật recording, kể cả khi snapshot đã đến trước lúc bấm ghi. Dùng trường
`segment` để ghép với CSV và `config_sha256` để đối chiếu giữa các phiên.
Nếu phiên thiếu snapshot, tab này sẽ báo thiếu cấu hình khi mở lại.

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
đối chiếu `imu_tx_full` cùng writer drops. Với type 9, mỗi lần RTT bỏ một
frame có thể làm thiếu tối đa 8 mẫu. Chi tiết frame, đơn vị, ánh xạ trục
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
