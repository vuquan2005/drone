# Công cụ hỗ trợ

Chạy các lệnh sau từ thư mục gốc repository:

| Lệnh                   | Tác dụng                                                          |
| ---------------------- | ----------------------------------------------------------------- |
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

## Khung đọc RTT

`rtt_read.py` chỉ nối vào OpenOCD đang chạy và chuyển nguyên byte ra stdout,
không cần thư viện ngoài, không mở ST-LINK hoặc tự tạo dữ liệu:

```sh
uv run tools/rtt_read.py --port 9091
```

Port 9091 là channel dữ liệu trong cấu hình Debug + RTT. Khi chạy OpenOCD
độc lập, port 9090 là log và 9091 là dữ liệu. Có thể chuyển stdout sang tệp
hoặc công cụ khác; stdout có thể chứa dữ liệu nhị phân tùy ứng dụng.

Debug mặc định xuất các dòng CSV `IMU1` ở channel 1 với sequence,
`t_read_us`, clock cảm biến và accel/gyro. Script giữ nguyên dữ liệu, chưa có
GUI, decoder, CSV logger hoặc dependency ngoài thư viện chuẩn. Xem
[định dạng và nguồn timestamp](../firmware/drone/DEBUG.md#channel-1-mẫu-imu-có-timestamp)
trước khi thêm parser/plot; mỗi lần recv có thể chứa một phần hoặc nhiều dòng.

## Xem log và đồ thị RTT

`rtt_view.py` là cửa sổ đọc hai kênh từ OpenOCD đang chạy; không mở probe,
không flash/reset board. `uv` cài PySide6-Essentials, pyqtgraph và NumPy từ nhóm `rtt`
trong `pyproject.toml`, dùng cùng môi trường `.venv/`:

```sh
uv run --group rtt tools/rtt_view.py
```

Chạy **STM32: Debug + RTT (OpenOCD)** rồi Continue. Cấu hình mở port 9090
cho LOG (channel 0), 9091 cho DATA (channel 1); OpenOCD độc lập cũng dùng
hai port này. Nếu đang chạy phiên debug cũ, cần khởi động lại phiên để có
port log mới. Có thể đổi địa chỉ/cổng:

```sh
uv run --group rtt tools/rtt_view.py --host 127.0.0.1 --log-port 9090 --data-port 9091
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
- Dòng TCP được ghép tới newline, dòng quá 1024 byte bị bỏ. Định dạng khác
  `IMU1` được đếm unknown để sau này thêm decoder, chưa vẽ các loại khác.

UI chỉ giữ lịch sử giới hạn, chưa ghi file hoặc thêm dashboard. Bộ đọc thô
`rtt_read.py` vẫn dùng độc lập như trước. Kiểm thử parser không cần Qt/board:

```sh
uv run python -m unittest discover -s tools/tests -p test_rtt_records.py -v
```

Kiểm thử GUI offscreen bằng hai TCP server localhost giả lập (cần cho phép
mở socket localhost), gồm reconnect và burst 4000 mẫu, không cần board:

```sh
QT_QPA_PLATFORM=offscreen uv run --group rtt python -m unittest discover -s tools/tests -p test_rtt_view.py -v
```

## Đồng bộ PDF

```sh
# Xem trước hoặc đồng bộ bài báo
python3 tools/sync_reference_pdfs.py --dry-run
make sync-papers

# Xem trước hoặc đồng bộ datasheet
python3 tools/sync_reference_pdfs.py --collection datasheets --dry-run
make sync-datasheets

# Trích xuất một PDF vào thư mục riêng để đối chiếu
python3 tools/sync_reference_pdfs.py docs/references/papers/huy2024-262.pdf --output-dir /tmp/papers_md
```

Đầu ra mặc định nằm trong `docs/references/papers/papers_md/` hoặc `docs/references/datasheets/datasheets_md/`. Script giữ nguyên thư mục đã có nội dung nhưng không có manifest và bảo vệ các tệp đầu ra đã sửa thủ công. Manifest chỉ lưu trạng thái cục bộ, không được đưa vào Git; sau khi clone, dùng `--output-dir` khác nếu muốn tạo bản trích xuất để đối chiếu.

Văn bản và ảnh được trích xuất tự động, không thay thế việc kiểm tra PDF gốc. PDF không có lớp văn bản cần OCR; sơ đồ vector, công thức và bảng có thể không được trích xuất đầy đủ. Xem thêm tùy chọn bằng `python3 tools/sync_reference_pdfs.py --help`.
