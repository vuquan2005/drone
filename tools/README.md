# Công cụ hỗ trợ

Chạy các lệnh sau từ thư mục gốc repository:

| Lệnh                   | Tác dụng                                                          |
| ---------------------- | ----------------------------------------------------------------- |
| `make telemetry`       | Mở viewer RTT và điều khiển ghi phiên dữ liệu                     |
| `make help`            | Xem danh sách lệnh                                                |
| `make report`          | Build PDF báo cáo; xem [hướng dẫn](../report/AGENTS.md#biên-dịch) |
| `make format`          | Định dạng các tệp được Git theo dõi                               |
| `make lint`            | Kiểm tra Markdown, CMake và LaTeX                                 |
| `make sync-papers`     | Đồng bộ PDF bài báo sang Markdown                                 |
| `make sync-datasheets` | Đồng bộ PDF datasheet sang Markdown                               |
| `make clean`           | Xóa thư mục build của firmware và báo cáo, gồm cả PDF đã build    |

## Cài đặt

Cần Make, Git, Python và [uv](https://docs.astral.sh/uv/). Cài pre-commit và hook cho mỗi bản clone:

```sh
uv sync --locked
uv run pre-commit install --install-hooks
```

Project dùng Python 3.10+, dependency quản lý trong `pyproject.toml` và
`uv.lock`; `uv sync` tạo môi trường `.venv/`. Nhóm `dev` (pre-commit) cài mặc
định, nhóm GUI `rtt` chỉ cài khi chọn `--group rtt`. Giữ `uv.lock` trong Git;
khi đổi dependency, chạy `uv lock` và xem diff lockfile. Chạy công cụ bằng
`uv run ...`, hoặc `uv run make lint` / `uv run make format`.

Lần đầu cần mạng để tải môi trường và công cụ. Riêng `latexindent` và ChkTeX cần được cài cùng TeX distribution. Các lệnh đồng bộ PDF cần thêm Poppler (`pdftotext` và `pdfimages`; gói `poppler-utils` trên Ubuntu/Debian và Fedora).

`make format` và `make lint` xử lý các tệp Git đang theo dõi; tệp mới cần được `git add` trước. Khi commit, formatter chạy trên tệp đã stage; nếu formatter sửa tệp, xem lại thay đổi, stage và commit lại. Chạy `make lint` riêng khi cần kiểm tra.

## Phụ thuộc tools và pyproject.toml

| Phạm vi                                | Dependency Python                                                   | Công cụ bên ngoài Python                                                                                                 |
| -------------------------------------- | ------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `rtt/capture_raw.py`, `rtt/records.py` | Thư viện chuẩn                                                      | OpenOCD đang phục vụ RTT khi đọc board                                                                                   |
| `rtt/view.py`, test GUI                | `numpy`, `PySide6` từ `PySide6-Essentials`, `pyqtgraph`; nhóm `rtt` | OpenOCD khi đọc board; test dùng TCP giả lập                                                                             |
| `sync_reference_pdfs.py`, test PDF     | Thư viện chuẩn                                                      | Poppler: `pdftotext`, `pdfimages`                                                                                        |
| `format_latex.py`                      | Thư viện chuẩn                                                      | `latexindent` từ TeX distribution                                                                                        |
| Format/lint qua hook                   | `pre-commit`; nhóm `dev` mặc định                                   | Hook tự quản lý Prettier, Black, clang-format, cmake-format và markdownlint; LaTeX cần `latexindent`, `chktex` trên PATH |

Đã đối chiếu import của toàn bộ file Python trong `tools/`, gồm tests:
không thiếu dependency Python trong `pyproject.toml`. NumPy được khai báo
trực tiếp vì GUI import trực tiếp. Các dependency bắc cầu nằm trong
`uv.lock`; không chép chúng vào danh sách trực tiếp hoặc sửa lockfile bằng tay.
Không cần thêm Poppler/TeX vào pip: đây là executable do hệ điều hành quản lý.

## Các file RTT

Các công cụ nằm trong `tools/rtt/`. `view.py` nhận byte từ TCP và gọi `records.py`
để xử lý DATA. Phụ thuộc mã một chiều: `view.py` import `records.py`;
`records.py` không import hoặc gọi UI. `capture_raw.py` là công cụ độc lập.

| File                        | Chức năng                                                                                                                   | Dependency Python                 |
| --------------------------- | --------------------------------------------------------------------------------------------------------------------------- | --------------------------------- |
| `rtt/capture_raw.py`        | Nối một TCP port, chuyển nguyên byte ra stdout; dùng xem nhanh hoặc redirect vào file                                       | Thư viện chuẩn                    |
| `rtt/records.py`            | Module ghép dòng TCP và parse IMU binary v1; đổi milli-unit sang SI, xử lý timestamp/sequence tràn, gap và lịch sử giới hạn | Thư viện chuẩn                    |
| `rtt/view.py`               | Cửa sổ nhận LOG và DATA đồng thời, lọc log, vẽ accel/gyro và báo trạng thái                                                 | Nhóm `rtt`, dùng `rtt/records.py` |
| `rtt/session_writer.py`     | Worker ghi CSV, text log và raw tùy chọn; queue giới hạn                                                                    | Thư viện chuẩn                    |
| `tests/test_rtt_records.py` | Kiểm thử framing, dữ liệu lỗi, gap, wrap/reset và giới hạn lịch sử                                                          | Thư viện chuẩn                    |
| `tests/test_rtt_view.py`    | Kiểm thử GUI với hai TCP server localhost giả lập, reconnect và burst 4000 mẫu                                              | Nhóm `rtt`                        |

Luồng dữ liệu: firmware → ring buffer RTT → OpenOCD → TCP → `rtt/capture_raw.py`
hoặc `rtt/view.py` → `rtt/records.py` (DATA). Các script không trực tiếp mở
ST-LINK, nạp firmware hoặc tự chạy OpenOCD. `rtt/records.py` là module dùng
chung, không phải lệnh cần chạy riêng.

Đọc thô không cần nhóm GUI:

```sh
uv run tools/rtt/capture_raw.py --port 9091 > /tmp/imu.bin
uv run tools/rtt/capture_raw.py --port 9090 > /tmp/drone-rtt.log
```

Cả cấu hình **Drone: OpenOCD + RTT** lẫn OpenOCD độc lập đều mở LOG channel 0
ở port 9090 và DATA channel 1 ở port 9091. Extension còn mở terminal LOG
riêng ở port động; script dùng hai port cố định, không dùng port terminal đó.

Firmware Debug xuất frame IMU binary v1 (48 byte) ở channel 1. Bộ đọc thô giữ nguyên byte; module parser và GUI giải mã theo
[schema binary](../protocol/telemetry.md). Dữ liệu thu hiện không phải CSV. Xem
[định dạng và nguồn timestamp](../firmware/drone/DEBUG.md#channel-1-mẫu-imu-có-timestamp).

## Xem log và đồ thị RTT

`rtt/view.py` là cửa sổ đọc hai kênh từ OpenOCD đang chạy; không mở probe,
không flash/reset board. `uv` cài PySide6-Essentials, pyqtgraph và NumPy từ nhóm `rtt`
trong `pyproject.toml`, dùng cùng môi trường `.venv/`:

```sh
uv run --group rtt tools/rtt/view.py
```

Chạy **Drone: OpenOCD + RTT** rồi Continue. Cấu hình mở port 9090
cho LOG (channel 0), 9091 cho DATA (channel 1); OpenOCD độc lập cũng dùng
hai port này. Nếu đang chạy phiên debug cũ, cần khởi động lại phiên để có
port log mới. Có thể đổi địa chỉ/cổng:

```sh
uv run --group rtt tools/rtt/view.py --host 127.0.0.1 --log-port 9090 --data-port 9091
```

- Log tô màu E/W/I/D, lọc đúng mức hoặc tìm một phần tag; tối đa 2000 dòng.
  Chọn All để xem cả dòng không đúng định dạng log. Clear chỉ xóa log ở UI.
- Hai plot accel XYZ (m/s²), gyro XYZ (rad/s); giữ 10 giây theo timestamp
  MCU, tối đa 20000 điểm và vẽ 25 lần/giây. Không thay đổi tần số firmware.
- Status báo trạng thái riêng từng kết nối, số mẫu nhận trong một giây host,
  tổng bản ghi, sequence thiếu, dòng lỗi/không biết và lần nghi reset.
  Khoảng sequence thiếu được ngắt nét plot; chưa đọc counter đầy của MCU nên
  không thể quy mọi khoảng trống cho RTT. Tốc độ host có thể dao động do TCP
  gom dữ liệu, không phải phép đo chính xác tần số lấy mẫu.
- Tự nối lại mỗi 2 giây. Mỗi lần nối DATA bắt đầu một miền thời gian mới,
  xóa plot và không đếm gap giữa hai kết nối; tổng counter vẫn giữ.
  Trong cùng kết nối, bộ đếm 32 bit tràn được xử lý modulo; lùi sequence
  hoặc thời gian hơn nửa miền được coi là nghi reset và bắt đầu lại plot.
  Reset rất sớm hoặc ngắt dữ liệu quá lâu không thể nhận diện chắc chắn nếu
  firmware chưa có boot ID. Không có dữ liệu thì giữ plot cuối.
- Type 2 cập nhật thống kê MCU (khoảng 1 Hz), có tuổi snapshot để thấy khi
  dữ liệu thống kê bị trễ; counter này khác các lỗi/drops phía host.
- CH0 ghép dòng newline, dòng quá 1024 byte bị bỏ. CH1 tìm magic, kiểm tra
  length và CRC32; báo frame errors riêng. Frame hỏng bị bỏ, tự tìm lại đầu
  frame; type/version chưa hỗ trợ được đếm unknown. Payload tối đa 256 byte.
  GUI mới yêu cầu firmware binary mới, không giải mã CSV `IMU1` cũ.

Plot giữ lịch sử giới hạn; recording lưu riêng toàn bộ bản ghi được nhận và enqueue thành công trong phiên. Bộ đọc thô
`rtt/capture_raw.py` vẫn dùng độc lập như trước. Kiểm thử parser không cần Qt/board:

```sh
uv run python -m unittest discover -s tools/tests -p test_rtt_records.py -v
```

Kiểm thử GUI offscreen bằng hai TCP server localhost giả lập (cần cho phép
mở socket localhost), gồm reconnect và burst 4000 mẫu, không cần board:

```sh
QT_QPA_PLATFORM=offscreen uv run --group rtt python -m unittest discover -s tools/tests -p test_rtt_view.py -v
```

## Ghi phiên CSV, text log và raw

Mở viewer sau khi OpenOCD đang chạy:

```sh
make telemetry
```

Chọn thư mục gốc rồi **Start recording**, **Stop recording** để kết thúc.
Mặc định lưu dưới `experiments/raw/telemetry/` (đã được Git ignore). Mỗi lần
Start tạo thư mục mới; cả thư mục và tên file dùng giờ địa phương dạng gọn
`YYYYMMDD_HHMMSS`. Nếu trùng giây, thêm `_01`, `_02`,… vào cả thư mục và tên
file; không ghi đè phiên cũ. Múi giờ lấy từ hệ điều hành máy chạy viewer;
không đổi timestamp MCU trong CSV hoặc đổi tên các phiên cũ. Ví dụ:

```text
telemetry_20261113_151530/
├── imu_20261113_151530.csv
├── rtt_stats_20261113_151530.csv
├── events_20261113_151530.log
└── data_20261113_151530.hex   # nếu chọn Save raw hex
```

Mở và tự bắt đầu ghi CSV/log, kèm raw tùy chọn, hoặc đổi thư mục/cổng:

```sh
make telemetry TELEMETRY_ARGS="--record --raw"
make telemetry TELEMETRY_ARGS="--record --output-dir /tmp/drone-sessions --log-port 9090 --data-port 9091"
```

`make telemetry` chỉ mở công cụ host, không nạp/reset MCU hoặc tự chạy
OpenOCD. Dùng `UV=/path/to/uv` nếu cần chỉ định executable khác.

`session_writer.py` độc lập Qt, socket và decoder. Viewer chuyển bản ghi cho
writer khi nhận, trước filter log và không phụ thuộc nhịp vẽ/lịch sử 10 giây:

- CSV chứa số nguyên gốc trên wire, có header ghi đơn vị; không lưu giá trị
  float SI của plot để tránh mất khả năng tái lập dữ liệu.
- `rtt_stats_*.csv` chứa snapshot counter đầy CH0/CH1, số frame IMU/stats
  bị bỏ/từ chối và tổng mẫu/lỗi đọc IMU; có sequence stats và uptime ms.
  Xem [schema type 2](../protocol/telemetry.md#rtt-statistics-v1-type-2).
- `events_*.log` chứa mọi dòng log nhận được trong khi ghi, kể cả dòng bị
  filter trên UI; giữ timestamp MCU, E/W/I/D và tag, kết thúc bằng LF.
- `data_*.hex` biểu diễn toàn bộ byte DATA trước giải mã dưới dạng text hex,
  tối đa 16 byte/dòng, kể cả frame hỏng/chưa đủ. Đây là plain hex, không phải
  Intel HEX; newline chỉ để dễ đọc, không phải ranh giới frame. Dùng
  `bytes.fromhex(text)` để khôi phục byte gốc. Capture có thể bắt đầu giữa
  frame. Không có dữ liệu trước lúc Start. Text hex chiếm khoảng 3 lần binary.
  Công cụ `capture_raw.py` riêng vẫn xuất byte binary nguyên gốc ra stdout.

Header CSV:

```text
segment,sequence,t_read_us,sensor_time_ticks,ax_mmps2,ay_mmps2,az_mmps2,gx_mradps,gy_mradps,gz_mradps
```

`segment` bắt đầu từ 1 ở kết nối DATA đầu tiên của viewer, tăng khi nối lại
DATA hoặc nghi reset. Nếu không reconnect/reset thì mọi dòng đều mang 1;
đây không phải type message hoặc số thứ tự mẫu. Start/Stop recording không
tự tăng segment. Không điền mẫu giả vào gap; CSV chỉ chứa mẫu hợp lệ, không
quy sequence thiếu thành counter RTT đầy. Log và DATA là hai luồng riêng,
không bảo đảm thứ tự sự kiện liên kênh. Giới hạn nhận diện reset vẫn như decoder.

Hiện type 1 (IMU) và type 2 (RTT statistics) có decoder và CSV riêng. Frame
type/version khác được đếm unknown, không ghi vào hai CSV trên; raw hex vẫn
giữ byte của chúng nếu được bật.
Khi thêm type mới cần bổ sung schema/decoder và writer tương ứng; dự kiến
mỗi type có CSV riêng, ví dụ `attitude_<timestamp>.csv`, cùng thư mục phiên.

Worker có queue tối đa 4096 mục, ghi file có buffer và flush ít nhất mỗi
khoảng một giây khi worker tiến triển. Queue đầy bỏ mục mới và hiển thị riêng
writer drops IMU rows / stats rows / LOG lines / raw chunks; khác với missing sequence.
Lỗi mở/ghi file hoặc hết dung lượng hiện ở trạng thái Recording ERROR và
ngừng ghi; UI tiếp tục nhận/vẽ. File có thể chưa đầy đủ khi lỗi ghi xảy ra.

Stop chờ worker tiêu thụ queue rồi flush/đóng file, không chặn event loop.
Đóng cửa sổ cũng chờ worker hoàn tất. Nếu ổ đĩa bị treo, cửa sổ có thể còn
chờ nhưng vẫn xử lý sự kiện. Không fsync từng mẫu; kill process/mất điện có
thể làm mất phần chưa ghi bền. File chỉ phản ánh dữ liệu nhận trong khoảng
recording và không được phục hồi lại dữ liệu RTT đã bỏ.

Kiểm thử writer không cần Qt hoặc board:

```sh
uv run python -m unittest discover -s tools/tests -p test_rtt_session_writer.py -v
```

## Đọc timing từ CSV IMU

Trong cùng segment, dùng hiệu timestamp giữa các sequence liên tiếp để xem
jitter đọc MCU. Gap sequence phải được tách khỏi thống kê jitter; so counter
`imu_tx_full` trong `rtt_stats_*.csv` để kiểm tra frame bị RTT bỏ vì đầy.
Timestamp MCU không đo trực tiếp jitter acquisition hoặc thời gian giao dịch
SPI. Counter tích lũy cần baseline phù hợp; không so qua reset hoặc segment khác.

## Đồng bộ PDF

```sh
# Xem trước hoặc đồng bộ bài báo
uv run tools/sync_reference_pdfs.py --dry-run
make sync-papers

# Xem trước hoặc đồng bộ datasheet
uv run tools/sync_reference_pdfs.py --collection datasheets --dry-run
make sync-datasheets

# Trích xuất một PDF vào thư mục riêng để đối chiếu
uv run tools/sync_reference_pdfs.py docs/references/papers/huy2024-262.pdf --output-dir /tmp/papers_md
```

Đầu ra mặc định nằm trong `docs/references/papers/papers_md/` hoặc `docs/references/datasheets/datasheets_md/`. Script giữ nguyên thư mục đã có nội dung nhưng không có manifest và bảo vệ các tệp đầu ra đã sửa thủ công. Manifest chỉ lưu trạng thái cục bộ, không được đưa vào Git; sau khi clone, dùng `--output-dir` khác nếu muốn tạo bản trích xuất để đối chiếu.

Văn bản và ảnh được trích xuất tự động, không thay thế việc kiểm tra PDF gốc. PDF không có lớp văn bản cần OCR; sơ đồ vector, công thức và bảng có thể không được trích xuất đầy đủ. Xem thêm tùy chọn bằng `uv run tools/sync_reference_pdfs.py --help`.
