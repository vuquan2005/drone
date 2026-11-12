# Telemetry binary v1

CH0 dùng log text `[ms][E/W/I/D][tag] message\n`; CH1 dùng frame binary
đặc tả dưới đây. Cách chạy viewer và ghi phiên xem [công cụ host](../tools/README.md).

## Frame

Tất cả số nhiều byte dùng little-endian. Không gửi trực tiếp layout `struct`
C, không có padding hoặc byte kết thúc newline.

| Offset | Số byte | Trường         | Quy ước                                             |
| ------ | ------- | -------------- | --------------------------------------------------- |
| 0      | 4       | magic          | ASCII `DRT!` = `44 52 54 21`                        |
| 4      | 1       | version        | `1`                                                 |
| 5      | 1       | type           | `1`: IMU; `2`: thống kê RTT                         |
| 6      | 2       | payload_length | uint16, không tính header/CRC; IMU = 36, stats = 40 |
| 8      | N       | payload        | Theo version và type                                |
| 8+N    | 4       | crc32          | CRC của toàn bộ header và payload, gồm magic        |

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
thập phân về phía 0. Từ chối NaN, vô cực hoặc kết quả ngoài int32; không clamp.
Trục theo package cảm biến, chưa hiệu chuẩn/xoay trục. Timestamp được chụp
sau đọc BSP, không phải thời điểm cảm biến acquisition hoặc host nhận.
Giữ nguyên clock cảm biến, không mở rộng tràn hoặc đồng bộ hai clock.

Sequence và timestamp MCU tràn modulo 2^32; clock cảm biến tràn modulo
2^24. Sequence tăng kể cả khi frame bị RTT từ chối, nhưng không đếm mẫu cảm
biến bị ghi đè trước khi MCU đọc. Một writer foreground, không retry;
thiếu chỗ thì bỏ nguyên frame mới và tăng counter đầy CH1. Tốc độ danh định
400 Hz tương ứng 19200 byte/s cho IMU; cần đo khả năng truyền trên board.

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
| 20     | rtt_data_full     | Frame CH1 bị BSP bỏ vì đầy, gồm IMU và stats                                |
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

## Mã nguồn và kiểm thử

- Encoder: `firmware/drone/App/Src/app_telemetry.c`; API `app_telemetry_write_imu()`, compile option `DRONE_ENABLE_TELEMETRY`.
- Framing và decoder: `tools/rtt/records.py`; không phụ thuộc UI.
- Fixture chung: [IMU](fixtures/imu_v1.hex), [thống kê RTT](fixtures/rtt_stats_v1.hex).
  Test C so output byte-for-byte; Python kiểm tra framing, CRC và giá trị
  trường theo cùng fixture.

Thêm loại dữ liệu mới bằng type mới cùng payload được tài liệu hóa; chỉ tăng
version khi thay đổi quy tắc không tương thích. Không đổi nghĩa/kiểu/đơn vị
của type đã có mà giữ nguyên version. Sequence riêng theo từng loại khi mở
rộng. CH1 có thể xen kẽ các type khác tần số; host phân loại theo type.

Chưa có boot ID; host không thể luôn phân biệt reset với tràn/gián đoạn dài.
