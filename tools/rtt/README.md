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
VS Code và OpenOCD độc lập. Terminal LOG của extension dùng port động riêng.
CH1 dùng [telemetry binary v1](../../protocol/telemetry.md).

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
  tra mapping. Không gán sẵn chức năng kênh và không đổi raw thành µs.
  `connected` phản ánh frame RC hợp lệ mới hơn 100 ms; khi disconnected
  giữ số cuối và làm mờ thanh. `telemetry stale` nghĩa là không có snapshot
  mới trong 0,5 s hoặc TCP DATA ngắt, chưa kết luận được trạng thái RC.
  Cần nạp firmware mới có telemetry type 3; Debug bật telemetry mặc định.
- Status hiển thị kết nối, tốc độ nhận host, gap/lỗi frame và thống kê MCU
  type 2 kèm tuổi snapshot. Tốc độ nhận host chịu ảnh hưởng TCP, không phải
  phép đo chính xác tần số lấy mẫu.
- Tự nối lại mỗi 2 giây. Nối lại DATA hoặc nghi reset bắt đầu segment mới,
  xóa plot và không đếm gap qua ranh giới; tổng counter host vẫn giữ.
  Không có dữ liệu thì giữ plot cuối.

Decoder xử lý tràn 32 bit theo modulo; lùi sequence/thời gian hơn nửa miền
được coi là nghi reset. Chưa có boot ID nên reset sớm hoặc gián đoạn dài có
thể không nhận diện chắc chắn. Frame lỗi bị bỏ và tìm lại đầu frame;
type/version chưa hỗ trợ được đếm unknown. Không hỗ trợ CSV `IMU1` cũ.

## Ghi phiên CSV, text log và raw

Chọn thư mục gốc rồi **Start recording** / **Stop recording**, hoặc chạy:

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
├── rc_20261113_151530.csv
├── rtt_stats_20261113_151530.csv
├── events_20261113_151530.log
└── data_20261113_151530.hex   # nếu chọn Save raw hex
```

- IMU CSV giữ số nguyên trên wire và đơn vị trong header; plot đổi sang SI.
- RC CSV giữ `segment,sequence,uptime_ms,last_frame_ms,connected,ch1_raw,…,ch16_raw`;
  là snapshot mỗi 50 ms, gồm cả trạng thái disconnected, không ghi mọi frame RC.
- Stats CSV giữ snapshot MCU theo [schema type 2](../../protocol/telemetry.md#rtt-statistics-v1-type-2).
- Log giữ mọi dòng nhận trong lúc recording, kể cả dòng bị filter trên UI.
- Raw là text hex của toàn bộ byte DATA trước decode, kể cả frame lỗi/dở
  hoặc type chưa hỗ trợ; tối đa 16 byte/dòng. Khôi phục bằng
  `bytes.fromhex(text)`. `capture_raw.py` xuất binary nguyên gốc.

Header IMU CSV:

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
