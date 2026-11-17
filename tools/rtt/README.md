# Công cụ RTT

Chạy các lệnh từ gốc repository. Cần Make, Python 3.10+, uv và OpenOCD
đang phục vụ RTT; nhóm dependency GUI là `rtt`:

```sh
uv sync --locked --group rtt
```

Capture, decoder, trạng thái telemetry và session writer chỉ dùng thư viện
chuẩn Python; viewer/test GUI dùng NumPy, PySide6-Essentials và pyqtgraph.

## Luồng dữ liệu

Luồng dữ liệu: firmware → RTT RAM → OpenOCD qua SWD → TCP → công cụ host.
Cách nạp/chạy OpenOCD xem [hướng dẫn debug](../../firmware/drone/DEBUG.md#dùng-từ-máy-tính).
Cấu hình VS Code **Drone: OpenOCD + RTT** tự mở viewer trong lúc khởi động
debug. Khi dừng tại `app_process`, nhấn Continue để nhận dữ liệu. Viewer
giữ cửa sổ sau khi dừng debug và tự nối lại khi RTT có sẵn. Đóng cửa sổ
để kết thúc task; có thể mở thủ công bằng `make telemetry`.

Chỉ cấu hình OpenOCD + RTT tự mở viewer; ST-LINK GDB không cung cấp RTT TCP.
Task nền chờ dòng `RTT viewer ready` sau khi Qt khởi tạo, nên không chặn
khởi động debugger trong suốt thời gian viewer chạy.

| File trong `tools/rtt/` | Chức năng                                                                            |
| ----------------------- | ------------------------------------------------------------------------------------ |
| `capture_raw.py`        | Chuyển nguyên byte từ một TCP port ra stdout                                         |
| `records.py`            | Framing log/binary, schema wire và lịch sử IMU                                       |
| `stream.py`             | Kết nối TCP Qt, nhận theo batch và tự nối lại                                        |
| `telemetry.py`          | Xác thực payload, snapshot RC/stats và segment khi nối lại/reset; không phụ thuộc Qt |
| `panels.py`             | Widget đồ thị IMU, thanh RC, thống kê và log có filter                               |
| `view.py`               | Điểm chạy CLI, ghép cửa sổ và điều phối stream/recording                             |
| `session_writer.py`     | Worker ghi CSV, log và raw tùy chọn                                                  |

LOG channel 0 dùng port 9090; DATA channel 1 dùng port 9091 trong cả launch
VS Code và OpenOCD độc lập. GUI hiển thị cả log và plot; không mở thêm terminal
RTT của extension. CH1 dùng [telemetry binary v1](../../protocol/telemetry.md).

Launch giữ RTT setup của Cortex-Debug với danh sách decoder rỗng. Vì không còn
terminal decoder để kích hoạt retry của extension, OpenOCD thử `rtt start` khi
CPU halt, gồm điểm dừng `app_process` sau khởi tạo RTT. Giữ `runToEntryPoint`
này để khởi động luồng dữ liệu trước khi Continue.

Đọc thô không cần nhóm GUI:

```sh
uv run tools/rtt/capture_raw.py --port 9091 > /tmp/imu.bin
uv run tools/rtt/capture_raw.py --port 9090 > /tmp/drone-rtt.log
```

## Xem log và đồ thị RTT

Sau khi chạy OpenOCD và Continue firmware:

```sh
make telemetry
# Đổi địa chỉ/cổng khi cần
make telemetry TELEMETRY_ARGS="--host 127.0.0.1 --log-port 9090 --data-port 9091"
```

Có thể gọi trực tiếp `uv run --group rtt tools/rtt/view.py`;
dùng `UV=/path/to/uv` để chỉ định executable cho Make.

- Log tô màu E/W/I/D, lọc mức/tag, giữ tối đa 2000 dòng; Clear chỉ xóa trên UI.
- Plot accel (m/s²) và gyro (rad/s) giữ 10 giây theo thời gian MCU, tối đa
  20000 điểm, vẽ 25 lần/giây. Gap sequence được ngắt nét.
- Tab **Receiver · RC** hiển thị CH1–CH16 dạng thanh và số raw CRSF
  (0–2047), cập nhật danh định 20 Hz. Di chuyển từng cần/công tắc để kiểm
  tra mapping. Không đổi raw thành µs.
  `connected` phản ánh frame RC hợp lệ mới hơn 100 ms; khi disconnected
  giữ số cuối và làm mờ thanh. `telemetry stale` nghĩa là không có snapshot
  mới trong 0,5 s hoặc TCP DATA ngắt, chưa kết luận được trạng thái RC.
  Cần nạp firmware mới có telemetry type 3; Debug bật telemetry mặc định.
- Thanh trạng thái hiển thị kết nối, tốc độ nhận host.
  Mở **Diagnostics** để xem counter host, thống kê MCU type 2 kèm tuổi
  snapshot và queue/writer drops khi ghi phiên. Tốc độ nhận host chịu ảnh hưởng TCP, không phải
  phép đo chính xác tần số lấy mẫu.
- Tự nối lại mỗi 2 giây. Nối lại DATA hoặc nghi reset bắt đầu segment mới,
  xóa plot và không đếm gap qua ranh giới; tổng counter host vẫn giữ.
  Không có dữ liệu thì giữ plot cuối.

Decoder xử lý tràn 32 bit theo modulo; lùi sequence/thời gian hơn nửa miền
được coi là nghi reset. Chưa có boot ID nên reset sớm hoặc gián đoạn dài có
thể không nhận diện chắc chắn. Frame lỗi bị bỏ và tìm lại đầu frame;
type/version chưa hỗ trợ được đếm unknown. Không hỗ trợ CSV `IMU1` cũ.

## Ghi phiên CSV, text log và raw

Mở **Recording options** để chọn thư mục gốc và bật **Save raw hex** khi cần,
rồi **Start recording** / **Stop recording**, hoặc chạy:

```sh
make telemetry TELEMETRY_ARGS="--record --raw"
make telemetry TELEMETRY_ARGS="--record --output-dir /tmp/drone-sessions"
```

Mặc định lưu tại `experiments/raw/telemetry/` (Git ignore). Mỗi Start tạo
phiên mới theo giờ địa phương `YYYYMMDD_HHMMSS`; trùng giây thêm `_01`,
`_02`,… vào thư mục và tên file, không ghi đè:

```text
telemetry_20261113_151530/
├── imu_20261113_151530.csv
├── imu_raw_20261113_151530.csv
├── rc_20261113_151530.csv
├── rtt_stats_20261113_151530.csv
├── events_20261113_151530.log
└── data_20261113_151530.hex   # nếu chọn Save raw hex
```

- `imu_raw_*.csv` giữ nguyên số đếm XYZ cảm biến từ type 4, kèm range, ODR và
  mã ánh xạ trục trong từng dòng. Plot tự đổi sang m/s², rad/s và hệ thân FLU
  bằng `[X, -Y, -Z]`; CSV raw không đổi dấu và không làm tròn.
- `imu_*.csv` (không gồm `imu_raw_`) dành cho type 1 cũ; giữ milli-unit như
  trước. Phiên chỉ có type 4 sẽ để file type 1 chỉ có header và ngược lại.
  Viewer đọc được cả hai loại; type 1 thiếu metadata trục nên giữ dấu gốc.
- RC CSV giữ `segment,sequence,uptime_ms,last_frame_ms,connected,ch1_raw,…,ch16_raw`;
  là snapshot mỗi 50 ms, gồm cả trạng thái disconnected, không ghi mọi frame RC.
- Stats CSV giữ snapshot MCU theo [schema type 2](../../protocol/telemetry.md#rtt-statistics-v1-type-2).
- Log giữ mọi dòng nhận trong lúc recording, kể cả dòng bị filter trên UI.
- Raw là text hex của toàn bộ byte DATA trước decode, kể cả frame lỗi/dở
  hoặc type chưa hỗ trợ; tối đa 16 byte/dòng. Khôi phục bằng
  `bytes.fromhex(text)`. `capture_raw.py` xuất binary nguyên gốc.

Header IMU raw CSV (type 4):

```text
segment,sequence,t_read_us,sensor_time_ticks,ax_raw,ay_raw,az_raw,gx_raw,gy_raw,gz_raw,accel_range_g,axis_map,gyro_range_dps,accel_odr_hz,gyro_odr_hz
```

`axis_map=1` chỉ phép đổi từ XYZ cảm biến sang FLU bằng `[X, -Y, -Z]`.
Range là biên dương full scale; công thức chuyển đổi và các trường nằm trong
[đặc tả type 4](../../protocol/telemetry.md#imu-raw-v1-type-4). Độ phân giải
được giữ nguyên từ cảm biến; không dùng cấu hình ngầm định trên máy nhận.

Header IMU CSV cũ (type 1):

```text
segment,sequence,t_read_us,sensor_time_ticks,ax_mmps2,ay_mmps2,az_mmps2,gx_mradps,gy_mradps,gz_mradps
```

`segment` bắt đầu từ 1 ở kết nối DATA đầu tiên của viewer, tăng khi nối lại
hoặc nghi reset; Start/Stop recording không tăng segment. CSV chỉ chứa mẫu
hợp lệ, không điền mẫu giả vào gap. LOG và DATA không bảo đảm thứ tự liên kênh.

Recording độc lập filter và lịch sử plot, chỉ lưu dữ liệu nhận từ lúc Start.
Queue giới hạn 4096 mục; đầy thì bỏ mục mới và báo writer drops riêng theo
IMU/stats/RC/log/raw. Lỗi mở/ghi file báo **Recording ERROR** và ngừng ghi;
UI tiếp tục nhận/vẽ. File có thể chưa đầy đủ khi lỗi xảy ra.

Stop hoặc đóng cửa sổ chờ worker ghi hết queue, flush và đóng file;
ổ đĩa treo có thể kéo dài thời gian chờ. Không fsync từng mẫu; kill process
hoặc mất điện có thể mất phần chưa ghi bền. Recording không phục hồi dữ liệu
đã bị bỏ trước đó.

## Đọc timing từ CSV IMU

Trong cùng segment, dùng hiệu `t_read_us` modulo 2^32 giữa các sequence
liên tiếp để xem jitter đọc MCU; tách gap khỏi thống kê jitter. Timestamp
này không đo trực tiếp jitter acquisition hoặc thời gian giao dịch SPI.

Đối chiếu gap với chênh lệch `imu_tx_full` trong stats CSV, dùng snapshot
baseline phù hợp và không so qua reset/segment. Gap sequence, RTT full và
writer drops là các chỉ số khác nhau; xem [ngữ nghĩa counter](../../protocol/telemetry.md#rtt-statistics-v1-type-2).

## Kiểm thử RTT

Parser và writer không cần Qt/board; test GUI dùng TCP localhost giả lập:

```sh
uv run python -m unittest discover -s tools/tests -p test_rtt_records.py -v
uv run python -m unittest discover -s tools/tests -p test_rtt_telemetry.py -v
uv run python -m unittest discover -s tools/tests -p test_rtt_session_writer.py -v
QT_QPA_PLATFORM=offscreen uv run --group rtt python -m unittest discover -s tools/tests -p test_rtt_view.py -v
```

Tab **Rate · FRD** hiển thị snapshot setpoint/gyro (rad/s), output PID,
lực đẩy mixer M1–M4, residual và dt của firmware Debug có bật
`DRONE_ENABLE_RATE_CONTROL`. Snapshot gửi ở 20 Hz; PID thử chạy 500 Hz.
Khi recording, dữ liệu lưu ở `rate_*.csv`; `flags & 2` biểu thị kết quả hợp lệ.
Khung plot hiển thị ngay khi mở tab; trace đợi bản tin hợp lệ.
Firmware đã có arm/failsafe và xuất ESC khi armed. Quy trình trên board và gain thử:
[DEBUG.md](../../firmware/drone/DEBUG.md#thử-trên-board).

## Mở phiên đã lưu

Chọn **Open session…** rồi thư mục `telemetry_*`. Viewer đọc CSV ở luồng nền,
ngắt TCP khi mở thành công và chuyển sang chế độ xem tĩnh. Chọn **Segment**
để xem toàn bộ đồ thị IMU/Rate của đoạn đó; có thể zoom/pan. **Return to live**
xóa dữ liệu đang xem và nối lại TCP. Không mở phiên khi đang ghi hoặc flush;
không ghi phiên trong chế độ xem dữ liệu cũ. File nguồn không bị thay đổi.

RC và thống kê MCU hiển thị snapshot cuối segment, không phải trạng thái
thiết bị hiện tại. Log hiển thị tối đa 2000 dòng cuối toàn phiên và vẫn lọc
được; log không đồng bộ theo segment. Mỗi luồng đồ thị có gốc thời gian riêng:
IMU dùng `t_read_us`, Rate dùng `t_sample_us` MCU thay cho thời gian nhận host
ở chế độ live. Timestamp được xử lý tràn 32 bit; không nối trace qua gap hoặc
segment. Không suy ra căn chỉnh thời gian giữa các luồng.

Chỉ cần có một loại CSV hợp lệ; file vắng mặt hoặc chỉ có header được bỏ qua.
Header/payload không hợp lệ sẽ báo tên file và dòng, giữ chế độ đang xem.
Bản đầu giới hạn 1.000.000 dòng CSV mỗi phiên; chưa có phát lại theo thời gian,
con trỏ snapshot hoặc nhập file raw hex. Thư mục chỉ có log chưa được hỗ trợ.

## Receiver thích ứng

Receiver tự đổi bố cục theo kích thước vùng tab: từ 850 × 280 px, bốn kênh
đầu hiển thị thành hai ô joystick thay cho thanh. Theo mapping
[Jumper T20 DA-Drone](../../firmware/Jumper-T20/README.md), ô trái là Yaw CH4
ngang / Throttle CH3 dọc; ô phải là Roll CH1 ngang / Pitch CH2 dọc.
Phải/trên tăng raw; tâm ô là 1023,5 theo miền 0–2047, không phải điểm giữa đã
hiệu chuẩn của bộ thu. Khi vùng tab nhỏ hơn, cả 16 kênh trở về thanh.
Nhãn raw và tuổi snapshot nằm dưới các thanh, tự ẩn khi vùng tab hẹp hơn 600 px hoặc thấp hơn
300 px.

Các trục dọc Rate giữ giá trị gốc rad/s hoặc normalized, không tự thêm hệ số SI.
