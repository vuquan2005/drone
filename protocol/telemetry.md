# Telemetry binary v1

CH0 dùng log text `[ms][E/W/I/D][tag] message\n`; CH1 dùng frame binary
đặc tả dưới đây. Cách chạy viewer và ghi phiên xem [công cụ host](../tools/rtt/README.md).

## Frame

Tất cả số nhiều byte dùng little-endian. Không gửi trực tiếp layout `struct`
C, không có padding hoặc byte kết thúc newline.

| Offset | Số byte | Trường         | Quy ước                                                  |
| ------ | ------- | -------------- | -------------------------------------------------------- |
| 0      | 4       | magic          | ASCII `DRT!` = `44 52 54 21`                             |
| 4      | 1       | version        | `1`                                                      |
| 5      | 1       | type           | `1`: IMU SI cũ; `2`: thống kê RTT; `3`: RC; `4`: IMU raw |
| 6      | 2       | payload_length | uint16; SI = 36, stats = 40, RC = 48, IMU raw = 32       |
| 8      | N       | payload        | Theo version và type                                     |
| 8+N    | 4       | crc32          | CRC của toàn bộ header và payload, gồm magic             |

CRC-32/ISO-HDLC: reflected polynomial `0xEDB88320`, init `0xFFFFFFFF`,
xorout `0xFFFFFFFF`, không xor dữ liệu đầu vào ngoài thuật toán chuẩn.
Chuỗi ASCII `123456789` có CRC `0xCBF43926`; phía Python dùng `zlib.crc32()`.
CRC được ghi little-endian. CRC phát hiện lỗi ngẫu nhiên, không xác thực dữ liệu.

Host giới hạn payload 256 byte, tối đa frame 268 byte. Dò magic, kiểm tra
length và CRC trước khi dùng payload. Length không hợp lệ hoặc CRC sai:
bỏ một byte, tìm magic tiếp. Magic có thể nằm trong payload; chỉ magic chưa
đủ để nhận diện frame. Nếu chưa đủ byte thì giữ phần còn lại đến lần nhận
sau; một header giả có length hợp lệ có thể làm chậm đồng bộ tới tối đa một
frame giới hạn. Khi nối lại TCP, xóa phần frame dở. Frame version/type chưa
biết nhưng CRC hợp lệ được bỏ nguyên frame và đếm unknown.

## IMU v1, type 1

Định dạng SI cũ, chỉ giữ để đọc các bản ghi trước đây. Firmware hiện gửi
IMU raw type 4; viewer không tự suy ra hệ trục của bản ghi type 1.

Payload 36 byte, frame 48 byte. Bảng offset dưới đây tính từ đầu payload:

| Offset | Kiểu   | Tên               | Ý nghĩa / đơn vị                             |
| ------ | ------ | ----------------- | -------------------------------------------- |
| 0      | uint32 | sequence          | Số lần MCU đọc thành công; mẫu đầu = 1       |
| 4      | uint32 | t_read_us         | Uptime MCU tại lúc đọc thành công, µs        |
| 8      | uint32 | sensor_time_ticks | Clock cảm biến 24 bit, 8 bit cao phải bằng 0 |
| 12     | int32  | ax_mmps2          | Gia tốc X, mm/s²                             |
| 16     | int32  | ay_mmps2          | Gia tốc Y, mm/s²                             |
| 20     | int32  | az_mmps2          | Gia tốc Z, mm/s²                             |
| 24     | int32  | gx_mradps         | Vận tốc góc X, mrad/s                        |
| 28     | int32  | gy_mradps         | Vận tốc góc Y, mrad/s                        |
| 32     | int32  | gz_mradps         | Vận tốc góc Z, mrad/s                        |

Số nguyên có dấu dùng bù hai. Encoder nhân mẫu SI float với 1000, cắt phần
thập phân về phía 0.
Từ chối NaN, vô cực hoặc kết quả ngoài int32; không clamp.
Độ phân giải vẫn là 1 mm/s² và 1 mrad/s; hiệu chuẩn trong firmware dùng mẫu SI
trước encode để tránh mất độ phân giải.
Các bản ghi type 1 ban đầu dùng trực tiếp trục cảm biến; firmware sau thay đổi
ánh xạ trục dùng FLU. Type 1 không chứa mã hệ trục để tự phân biệt; cần biết
firmware dùng lúc ghi. Viewer giữ nguyên dấu và chỉ đổi milli-unit sang SI.
Timestamp được chụp
sau đọc BSP, không phải thời điểm cảm biến acquisition hoặc host nhận.
Giữ nguyên clock cảm biến, không mở rộng tràn hoặc đồng bộ hai clock.

Sequence và timestamp MCU tràn modulo 2^32; clock cảm biến tràn modulo
2^24. Sequence tăng kể cả khi frame bị RTT từ chối, nhưng không đếm mẫu cảm
biến bị ghi đè trước khi MCU đọc. Một writer foreground, không retry;
thiếu chỗ thì bỏ nguyên frame mới và tăng counter đầy CH1. Tốc độ danh định
400 Hz tương ứng 19200 byte/s cho IMU; cần đo khả năng truyền trên board.

## IMU raw v1, type 4

Payload 32 byte, frame 44 byte. Đây là số đếm BMI270 nguyên gốc, chưa đổi đơn
vị, hiệu chuẩn, lọc hoặc ánh xạ hệ trục. Offset tính từ đầu payload:

| Offset | Kiểu     | Trường                 | Ý nghĩa                                                 |
| ------ | -------- | ---------------------- | ------------------------------------------------------- |
| 0      | uint32   | sequence               | Bộ đếm đọc IMU thành công                               |
| 4      | uint32   | t_read_us              | Uptime MCU sau đọc BSP, µs; không phải acquisition time |
| 8      | uint32   | sensor_time_ticks      | Clock cảm biến 24 bit, 8 bit cao bằng 0                 |
| 12     | int16[3] | ax_raw, ay_raw, az_raw | Số đếm accel XYZ theo trục cảm biến                     |
| 18     | int16[3] | gx_raw, gy_raw, gz_raw | Số đếm gyro XYZ theo trục cảm biến                      |
| 24     | uint8    | accel_range_g          | Biên dương full scale, hiện tại 16 g                    |
| 25     | uint8    | axis_map               | `1`: cảm biến → thân FLU bằng `[X, -Y, -Z]`             |
| 26     | uint16   | gyro_range_dps         | Biên dương full scale, hiện tại 2000 °/s                |
| 28     | uint16   | accel_odr_hz           | ODR cấu hình accel, hiện tại 400 Hz                     |
| 30     | uint16   | gyro_odr_hz            | ODR cấu hình gyro, hiện tại 400 Hz                      |

Số int16 dùng bù hai, bao gồm đủ miền [-32768, 32767]. Firmware lấy trực tiếp
counts từ cùng lần đọc tạo mẫu SI trong App, không dựng lại raw từ float.
Metadata lặp trong từng frame để host nối giữa phiên vẫn tự chuyển đổi được.
Các trường range/ODR phản ánh cấu hình cố định BSP; nếu đổi cấu hình sensor
phải cập nhật các hằng BSP tương ứng.

Bên nhận đổi `accel = raw * accel_range_g * 9.80665 / 32768` (m/s²),
`gyro = raw * gyro_range_dps * pi / (180 * 32768)` (rad/s), rồi áp dụng
`[X, -Y, -Z]` cho cả hai vector để hiển thị FLU (X tiến, Y trái, Z lên).
Đổi sang kiểu rộng/float trước khi đảo dấu để giữ đúng raw -32768.
Vận tốc góc dương theo quy tắc bàn tay phải; khi đứng yên với Z hướng lên,
accel Z xấp xỉ +g. CSV raw giữ nguyên counts và metadata, không đảo dấu.

Decoder từ chối sai kích thước, sensor time ngoài 24 bit, accel range ngoài
{2, 4, 8, 16}, gyro range ngoài {125, 250, 500, 1000, 2000}, ODR bằng 0 hoặc
axis_map chưa hỗ trợ. Quy tắc timestamp, sequence, wrap và drop như type 1.
400 Hz tương ứng 17600 byte/s, chưa tính stats/RC/log.

## RTT statistics v1, type 2

Payload 40 byte, frame 52 byte. Tất cả trường uint32 little-endian, offset từ
đầu payload; các counter tràn modulo 2^32 và reset khi `app_init()`:

| Offset | Trường            | Ý nghĩa                                                                     |
| ------ | ----------------- | --------------------------------------------------------------------------- |
| 0      | sequence          | Số lần thử gửi thống kê, đầu tiên = 1; tăng dù frame bị bỏ                  |
| 4      | uptime_ms         | Uptime MCU lúc chụp thống kê, ms, không phải timestamp IMU                  |
| 8      | imu_samples       | Tổng số lần đọc IMU thành công, cũng là sequence IMU mới nhất               |
| 12     | imu_read_errors   | Tổng lỗi đọc driver IMU; không đếm NO_DATA hoặc lỗi init                    |
| 16     | rtt_log_full      | Bản ghi CH0 bị BSP bỏ vì đầy                                                |
| 20     | rtt_data_full     | Frame CH1 bị BSP bỏ vì đầy, gồm IMU, stats và RC                            |
| 24     | imu_tx_full       | Chỉ frame IMU bị bỏ vì đầy                                                  |
| 28     | stats_tx_full     | Chỉ frame thống kê bị bỏ vì đầy                                             |
| 32     | imu_tx_rejected   | IMU bị từ chối vì nguyên nhân khác FULL (kể cả dữ liệu encode không hợp lệ) |
| 36     | stats_tx_rejected | Stats bị từ chối vì nguyên nhân khác FULL                                   |

App thử gửi mỗi khoảng 1000 ms trong foreground, kể cả khi IMU lỗi hoặc
NO_DATA; không retry/gửi bù. Lịch dùng hiệu uint32 qua tick wrap; poll bị
chặn cũng làm thống kê trễ.

Counter được chụp trước chính lần gửi stats đó: nếu frame này thất bại, lần
stats sau sẽ phản ánh lỗi. Counter tích lũy nên mất một frame stats không
làm mất tổng lịch sử. `rtt_data_full` không đồng nghĩa số mẫu IMU bị bỏ;
để đối chiếu gap IMU, dùng chênh lệch `imu_tx_full`, không cộng lại với
`rtt_data_full`. Counter không đo mẫu phần cứng chưa kịp đọc hoặc dữ liệu
mất trên host. Khi chưa có snapshot làm baseline, chưa quy được chính xác
counter tích lũy vào một khoảng CSV; các frame stats cũng có thể bị mất.

## Receiver v1, type 3

Payload 48 byte, frame 60 byte. Offset tính từ đầu payload:

| Offset | Kiểu       | Tên           | Ý nghĩa                                                    |
| ------ | ---------- | ------------- | ---------------------------------------------------------- |
| 0      | uint32     | sequence      | Số lần thử gửi RC từ init, đầu = 1, tăng cả khi bị từ chối |
| 4      | uint32     | uptime_ms     | HAL tick sau khi lấy snapshot RC, ms                       |
| 8      | uint32     | last_frame_ms | HAL tick lúc nhận frame RC hợp lệ gần nhất, ms             |
| 12     | uint32     | connected     | 0 hoặc 1; trạng thái freshness từ receiver                 |
| 16     | uint16[16] | channels      | CH1–CH16, raw CRSF 11 bit (0–2047)                         |

App thử gửi snapshot mỗi 50 ms (20 Hz, khoảng 1200 byte/s), độc lập IMU;
không retry hoặc gửi bù. Đây là snapshot để quan sát, không ghi mọi frame RC.
Sequence và timestamp tràn modulo 2^32. `connected` yêu cầu frame hợp lệ
mới hơn 100 ms, và không có gián đoạn transport chưa phục hồi; không phải
radio LQ hoặc trạng thái motor failsafe. Khi mất kết nối giữ nguyên kênh
cũ; lúc khởi động kênh bằng 0 và connected = 0. `last_frame_ms = 0` cũng có
thể là timestamp hợp lệ; tuổi frame chỉ có ý nghĩa sau khi đã nhận RC.

Không gán sẵn roll/pitch/throttle/yaw: mapping phụ thuộc cấu hình tay điều
khiển. Không đổi raw sang µs vì đây không phải phép đo độ rộng xung PWM.
Viewer làm mờ kênh khi disconnected; nếu không có snapshot mới trong 0,5 s
hoặc TCP DATA ngắt, báo telemetry stale và giữ giá trị cuối để tham khảo.
Counter type 2 giữ schema cũ; RC làm tăng tổng `rtt_data_full` khi đầy,
không tăng `imu_tx_full` hoặc `stats_tx_full`. Gap sequence RC trong CSV
cho biết snapshot bị thiếu, không đo số frame UART/radio mất.

## Mã nguồn và kiểm thử

- Encoder: `firmware/drone/App/Src/app_telemetry.c`; API `app_telemetry_write_imu()`, compile option `DRONE_ENABLE_TELEMETRY`.
- Framing và decoder: `tools/rtt/records.py`; không phụ thuộc UI.
- Fixture chung: [IMU SI cũ](fixtures/imu_v1.hex), [IMU raw](fixtures/imu_raw_v1.hex), [thống kê RTT](fixtures/rtt_stats_v1.hex), [RC](fixtures/rc_v1.hex).
  Test C so output byte-for-byte; Python kiểm tra framing, CRC và giá trị
  trường theo cùng fixture.

Thêm loại dữ liệu mới bằng type mới cùng payload được tài liệu hóa; chỉ tăng
version khi thay đổi quy tắc không tương thích. Không đổi nghĩa/kiểu/đơn vị
của type đã có mà giữ nguyên version. Sequence riêng theo từng loại khi mở
rộng. CH1 có thể xen kẽ các type khác tần số; host phân loại theo type.

Chưa có boot ID; host không thể luôn phân biệt reset với tràn/gián đoạn dài.
