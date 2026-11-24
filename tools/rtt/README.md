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

Cắm ST-LINK qua SWD và dừng phiên debug đang dùng cùng ST-LINK.
Tắt tay điều khiển rồi reset board để firmware Debug hoặc Release chờ RC.
Bấm **Flash Export…** trong cửa sổ `make telemetry` để mở GUI xuất flash.
Cũng có thể mở trực tiếp từ gốc repo:

```sh
make flash-export
```

Sau khi xuất xong, đóng OpenOCD, gạt SB và SD xuống LOW rồi reset để trở
lại chế độ bay. Thao tác xóa telemetry trong Viewer tác động lên chip
ngoài W25Q128FV.

Bấm **Start OpenOCD**. Viewer gửi lệnh lấy danh sách phiên qua RTT.
Nếu board còn chờ RC, lệnh này chọn chế độ xuất và firmware giữ PWM tắt.
Viewer xác nhận kết nối xuất qua phản hồi có ID khớp với yêu cầu.
Nếu receiver vẫn gửi RC hợp lệ, có thể vào chế độ xuất bằng cách đặt
SD HIGH, SB LOW hoặc MIDDLE rồi reset với tay điều khiển đang kết nối.
Danh sách hiển thị ID, số trang và trạng thái của từng phiên. Dùng **Scan sessions** để
làm mới, rồi chọn phiên và bấm **Export selected** để tải khoảng trang của phiên
đó. Viewer lưu CSV, thông tin phiên và ảnh `session.bin`.
Dùng **Export flash** khi cần sao lưu toàn chip vào `flash.bin` trước khi xóa.

Cửa sổ quản lý OpenOCD với
GDB ở `127.0.0.1:3333` và RTT ở cổng 9092/9093. Khi cần kết nối GDB,
dùng `target extended-remote :3333` và cho CPU chạy trước khi export.
**Stop OpenOCD** hoặc đóng cửa sổ sẽ dừng server đã mở từ công cụ.
Có thể chọn executable và cổng bằng
`FLASH_EXPORT_ARGS="--openocd /đường/dẫn --gdb-port 0"`, trong đó `0`
tắt GDB. Dùng `--log-port` và `--data-port` để đổi cổng RTT của công cụ.

Mỗi lượt xuất được lưu vào `experiments/raw/export/flash_export_<thời điểm>`.
Đổi thư mục xuất bằng `make flash-export FLASH_EXPORT_ARGS="--export-dir /đường/dẫn"`.

Trong lúc xuất, cửa sổ hiển thị lượt thử, lúc nhận `BEGIN` và số trang
khác `0xFF` đã nhận. **STM32 log (RTT 0)** hiển thị mode, số trang chip đã
quét và kết quả phía board khi firmware bật log mức INFO. Hai bộ đếm
có thể khác nhau vì trang trống được lược khỏi kênh DATA. Nếu hết 15 giây
chưa nhận `BEGIN` đúng ID, thiếu trang hoặc CRC sai, cửa sổ thử lại tối đa
năm lượt tính cả lượt đầu.
Các trang qua kiểm tra CRC từ những lần có cùng
thông tin END được ghép theo địa chỉ. Ảnh chỉ hoàn tất khi đủ số trang
và khớp CRC của board. **Cancel export** dừng truyền về máy.
Board ghi `export_end` khi đã đưa gói cuối vào bộ đệm RTT. Viewer báo
`Flash saved` khi đã nhận đủ dữ liệu, kiểm tra CRC và lưu ảnh.

`flash.bin` là ảnh 16 MiB của chip, giữ cả dữ liệu cũ ở các trang journal
không nhận diện được. `export.json` tóm tắt kết quả và `export.log` ghi
thời điểm, số trang, lỗi CRC và kết quả từng lần đọc. Tab **Export / erase log**
hiển thị nội dung file trong lúc chạy. Khi hoàn tất, danh sách hiển thị tổng
số session, số session gián đoạn và trạng thái từng session. Chọn một mục rồi
bấm **Open session** để mở dữ liệu đã lưu trong Viewer offline. Lượt xuất
chưa xác minh giữ `flash.bin.part` và ghi nguyên nhân vào `export.log`.

Sau khi xuất thành công, có thể bấm **Erase flash** và xác nhận
để giải phóng toàn bộ W25Q128FV. Giữ board ở chế độ export và chờ Viewer
báo đã xóa và xác minh. Viewer kiểm tra
lại kích thước và CRC của `flash.bin` trước khi gửi lệnh. Board chỉ nhận lệnh
cho yêu cầu export vừa hoàn tất trong lần khởi động hiện tại. `export.log` ghi
thời điểm gửi lệnh, thời điểm board xác nhận và kết quả xóa theo ID yêu cầu.
Nếu kết quả chưa được xác nhận, kiểm tra tab **STM32 log (RTT 0)**.
Sau khi xóa và xác minh toàn chip, firmware ghi mốc `EMPTY` trong sector cuối
để Flight có thể bỏ lượt quét toàn chip ngay sau erase. Vì vậy, một bản export
ngay sau erase có thể chứa trang mốc và vẫn có 0 phiên.

Có thể dùng CLI sau khi board đã vào FLASH_EXPORT bằng SD HIGH hoặc
qua Viewer. Nếu dùng Viewer để chọn chế độ, dừng OpenOCD trong Viewer trước
khi mở phiên OpenOCD riêng và giữ board chạy. Mở OpenOCD
với [cấu hình RTT độc lập](../../firmware/drone/DEBUG.md#dùng-từ-máy-tính),
giữ cổng 9090/9091 dành cho CLI và chạy từ gốc repo:

```sh
python3 tools/rtt/flash_export.py
```

CLI dùng cùng thư mục mặc định với viewer. Để lưu vào nơi khác, truyền
đường dẫn tới thư mục mới. Có thể thay số lần đọc tối đa bằng
`--attempts` và cấu hình kết nối qua `--host`, `--control-port` và
`--data-port`.

Chế độ FLASH_EXPORT hỗ trợ xuất trực tiếp theo ID phiên (số trang `BEGIN`) hoặc
xuất phiên liền trước một ID đã biết. CLI tạo `session.bin` theo địa chỉ
flash gốc cùng CSV và `session.json` của phiên đã chọn.

```sh
python3 tools/rtt/flash_export.py --session 180
python3 tools/rtt/flash_export.py --previous 180
```

Nếu một phiên thiếu `END`, firmware dò đến khi gặp một phiên khác hoặc
chạm cuối flash. Lệnh xóa chip yêu cầu một lượt export toàn flash.

## Xem và ghi dữ liệu

Chạy `make telemetry` để xem và ghi telemetry live. Thư mục ghi được chọn
trong **Recording options → Save folder**, mặc định là
`experiments/raw/telemetry`.

Viewer hiển thị log có filter, IMU, receiver và rate/mixer. IMU hiển thị
m/s² và rad/s trong hệ thân FRD. Với firmware mới, receiver hiển thị bốn
trục CH1–CH4 trên thang danh định 172–1811, với tâm hình cần gạt ở 992.
Nhãn hiển thị giá trị raw CRSF cùng trạng thái cần gạt SA/SB/SC/SD.
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
