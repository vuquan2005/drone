# Công cụ hỗ trợ

Chạy các lệnh từ gốc repository; `make help` liệt kê đầy đủ.

| Lệnh                                        | Tác dụng                                                          |
| ------------------------------------------- | ----------------------------------------------------------------- |
| `make telemetry`                            | Mở viewer RTT và ghi phiên dữ liệu                                |
| `make format` / `make lint`                 | Format / lint các tệp Git theo dõi                                |
| `make sync-papers` / `make sync-datasheets` | Trích xuất PDF sang Markdown và ảnh                               |
| `make report`                               | Build PDF; xem [hướng dẫn báo cáo](../report/AGENTS.md#biên-dịch) |
| `make clean`                                | Xóa build firmware/báo cáo, gồm PDF đã build                      |

## Cài đặt

Cần Make, Git, Python 3.10+ và [uv](https://docs.astral.sh/uv/):

```sh
uv sync --locked
uv run pre-commit install --install-hooks
```

Dependency quản lý tại `pyproject.toml` và `uv.lock`; môi trường nằm trong
`.venv/`. Nhóm `dev` cài mặc định, nhóm GUI `rtt` chọn bằng `--group rtt`.
Khi đổi dependency, chạy `uv lock` và xem diff; giữ lockfile trong Git.
Lần đầu cần mạng để tải môi trường/công cụ.

| Phạm vi                                | Phụ thuộc                                                         |
| -------------------------------------- | ----------------------------------------------------------------- |
| Capture, decoder và session writer RTT | Thư viện chuẩn Python                                             |
| Viewer và test GUI                     | Nhóm `rtt`: NumPy, PySide6-Essentials, pyqtgraph                  |
| Đọc RTT từ board                       | OpenOCD đang phục vụ RTT                                          |
| Trích xuất PDF                         | Poppler: `pdftotext`, `pdfimages`                                 |
| Format/lint                            | pre-commit; LaTeX cần `latexindent`, `chktex` từ TeX distribution |

Chạy bằng `uv run ...`, hoặc `uv run make lint` / `uv run make format`.
Các lệnh tổng hợp chỉ xử lý tệp Git theo dõi. Khi commit, nếu formatter sửa
file thì xem diff, stage lại rồi commit; dùng `make lint` để kiểm tra riêng.

## Các file RTT

Luồng dữ liệu: firmware → RTT RAM → OpenOCD qua SWD → TCP → công cụ host.
Cách nạp/chạy OpenOCD xem [hướng dẫn debug](../firmware/drone/DEBUG.md#dùng-từ-máy-tính).
Công cụ host cần OpenOCD đang chạy; `make telemetry` chỉ mở viewer.

| File trong `tools/rtt/` | Chức năng                                             |
| ----------------------- | ----------------------------------------------------- |
| `capture_raw.py`        | Chuyển nguyên byte từ một TCP port ra stdout          |
| `records.py`            | Decoder log/frame, sequence/timestamp và lịch sử plot |
| `view.py`               | Hiển thị LOG/DATA, đồ thị và điều khiển recording     |
| `session_writer.py`     | Worker ghi CSV, log và raw tùy chọn                   |

LOG channel 0 dùng port 9090; DATA channel 1 dùng port 9091 trong cả launch
VS Code và OpenOCD độc lập. Terminal LOG của extension dùng port động riêng.
CH1 dùng [telemetry binary v1](../protocol/telemetry.md).

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
├── rtt_stats_20261113_151530.csv
├── events_20261113_151530.log
└── data_20261113_151530.hex   # nếu chọn Save raw hex
```

- IMU CSV giữ số nguyên trên wire và đơn vị trong header; plot đổi sang SI.
- Stats CSV giữ snapshot MCU theo [schema type 2](../protocol/telemetry.md#rtt-statistics-v1-type-2).
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
IMU/stats/log/raw. Lỗi mở/ghi file báo **Recording ERROR** và ngừng ghi;
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
writer drops là các chỉ số khác nhau; xem [ngữ nghĩa counter](../protocol/telemetry.md#rtt-statistics-v1-type-2).

## Kiểm thử RTT

Parser và writer không cần Qt/board; test GUI dùng TCP localhost giả lập:

```sh
uv run python -m unittest discover -s tools/tests -p test_rtt_records.py -v
uv run python -m unittest discover -s tools/tests -p test_rtt_session_writer.py -v
QT_QPA_PLATFORM=offscreen uv run --group rtt python -m unittest discover -s tools/tests -p test_rtt_view.py -v
```

## Đồng bộ PDF

```sh
uv run tools/sync_reference_pdfs.py --dry-run
make sync-papers
uv run tools/sync_reference_pdfs.py --collection datasheets --dry-run
make sync-datasheets
# Trích xuất riêng để đối chiếu
uv run tools/sync_reference_pdfs.py docs/references/papers/huy2024-262.pdf --output-dir /tmp/papers_md
```

Đầu ra mặc định: `docs/references/papers/papers_md/` và
`docs/references/datasheets/datasheets_md/`. Script bảo vệ đầu ra sửa thủ công
và thư mục có nội dung nhưng không có manifest. Manifest chỉ lưu cục bộ;
sau clone, dùng `--output-dir` khác để tạo bản đối chiếu.

Kiểm tra lại PDF gốc: file thiếu lớp văn bản cần OCR; sơ đồ vector, công
thức và bảng có thể trích xuất không đầy đủ. Xem tùy chọn bằng `--help`.
