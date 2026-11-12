# Telemetry binary v1

CH0 giữ log text `[ms][E/W/I/D][tag] message\n`. CH1 dùng frame binary dưới
đây, thay hoàn toàn CSV `IMU1`. Cần nạp firmware và cập nhật công cụ host cùng
nhau; decoder hiện không hỗ trợ CSV cũ. Không cần thư viện serialization ngoài.

## Frame

Tất cả số nhiều byte dùng little-endian. Không gửi trực tiếp layout `struct`
C, không có padding hoặc byte kết thúc newline.

| Offset | Số byte | Trường         | Quy ước                                      |
| ------ | ------- | -------------- | -------------------------------------------- |
| 0      | 4       | magic          | ASCII `DRT!` = `44 52 54 21`                 |
| 4      | 1       | version        | `1`                                          |
| 5      | 1       | type           | `1`: IMU                                     |
| 6      | 2       | payload_length | uint16, không tính header/CRC; IMU = 36      |
| 8      | N       | payload        | Theo version và type                         |
| 8+N    | 4       | crc32          | CRC của toàn bộ header và payload, gồm magic |

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
biến bị ghi đè trước khi MCU đọc. Giữ chính sách một writer foreground,
không retry; enqueue nguyên frame một lần. Không đủ chỗ: bỏ frame mới và
tăng counter đầy CH1. Tốc độ danh định 400 Hz tương ứng 19200 byte/s cho IMU,
chưa tính overhead giao tiếp. Không cam kết probe theo kịp khi chưa đo board.

## Mã nguồn và kiểm thử

- Encoder: `firmware/drone/App/Src/app_telemetry.c`; API `app_telemetry_write_imu()`, compile option `DRONE_ENABLE_TELEMETRY`.
- Framing và decoder: `tools/rtt/records.py`; không phụ thuộc UI.
- Fixture chung: `protocol/fixtures/imu_v1.hex`, là hex của frame đã encode,
  không phải file raw dùng chạy thực tế. C so sánh output byte-for-byte;
  Python kiểm tra framing, CRC và giá trị trường với cùng fixture.
- Fixture dùng sequence 17, t_read_us 123456, sensor ticks `0x123456`,
  accel `[1250,-2500,9806]`, gyro `[1000,-2000,3000]`.

Thêm loại dữ liệu mới bằng type mới cùng payload được tài liệu hóa; chỉ tăng
version khi thay đổi quy tắc không tương thích. Không đổi nghĩa/kiểu/đơn vị
của type đã có mà giữ nguyên version. Sequence riêng theo từng loại khi mở
rộng. CH1 có thể xen kẽ các type khác tần số; host phân loại theo type.

`capture_raw.py` lưu nguyên frame vào `.bin`; file này không phải CSV. `view.py`
giải mã để vẽ SI và có Start/Stop recording CSV, log, raw tùy chọn; xem
[ghi phiên telemetry](../tools/README.md#ghi-phiên-csv-text-log-và-raw). Không có boot ID, nên
phân biệt reset với tràn/gián đoạn dài vẫn có giới hạn như trước.
