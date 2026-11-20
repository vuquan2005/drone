# Telemetry binary v1

CH0 dùng log text `[ms][E/W/I/D][tag] message\n`; CH1 dùng frame binary
đặc tả dưới đây. Cách chạy viewer và ghi phiên xem [công cụ host](../tools/rtt/README.md).

## Frame

Tất cả số nhiều byte dùng little-endian. Không gửi trực tiếp layout `struct`
C, không có padding hoặc byte kết thúc newline.

| Offset | Số byte | Trường         | Quy ước                                                                                        |
| ------ | ------- | -------------- | ---------------------------------------------------------------------------------------------- |
| 0      | 4       | magic          | ASCII `DRT!` = `44 52 54 21`                                                                   |
| 4      | 1       | version        | `1`                                                                                            |
| 5      | 1       | type           | `1` ngừng dùng, `2` thống kê RTT, `3` RC, `4` IMU raw, `5` rate, `6` tư thế, `7` vòng attitude |
| 6      | 2       | payload_length | uint16. Độ dài theo type 2–7 lần lượt là 40, 48, 32, 80, 24, 56 byte                           |
| 8      | N       | payload        | Theo version và type                                                                           |
| 8+N    | 4       | crc32          | CRC của toàn bộ header và payload, gồm magic                                                   |

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

## Mã type 1 đã ngừng dùng

Type 1 (IMU SI cũ) đã bỏ hỗ trợ. Không tái sử dụng mã này. Frame type 1 có
CRC hợp lệ được bỏ qua và đếm unknown; viewer không đọc `imu_*.csv` cũ.
Các log gốc được giữ nguyên; có thể dùng phiên bản công cụ trước thay đổi này
nếu cần xem lại dữ liệu lịch sử.

## IMU raw v1, type 4

Payload 32 byte, frame 44 byte. Đây là số đếm IMU nguyên gốc, chưa đổi đơn
vị, hiệu chuẩn, lọc hoặc ánh xạ hệ trục. Offset tính từ đầu payload:

| Offset | Kiểu     | Trường                 | Ý nghĩa                                                          |
| ------ | -------- | ---------------------- | ---------------------------------------------------------------- |
| 0      | uint32   | sequence               | Bộ đếm đọc IMU thành công                                        |
| 4      | uint32   | t_read_us              | Uptime MCU sau đọc BSP, µs; không phải acquisition time          |
| 8      | uint32   | sensor_time_ticks      | Clock cảm biến 24 bit, 8 bit cao bằng 0                          |
| 12     | int16[3] | ax_raw, ay_raw, az_raw | Số đếm accel XYZ theo trục cảm biến                              |
| 18     | int16[3] | gx_raw, gy_raw, gz_raw | Số đếm gyro XYZ theo trục cảm biến                               |
| 24     | uint8    | accel_range_g          | Biên dương full scale, hiện tại 16 g                             |
| 25     | uint8    | axis_map               | `1`: gá cũ → FLU `[X, -Y, -Z]`; `2`: gá mới → FRD `[-X, +Y, -Z]` |
| 26     | uint16   | gyro_range_dps         | Biên dương full scale, hiện tại 2000 °/s                         |
| 28     | uint16   | accel_odr_hz           | ODR cấu hình accel, hiện tại 800 Hz                              |
| 30     | uint16   | gyro_odr_hz            | ODR cấu hình gyro, hiện tại 800 Hz                               |

Số int16 dùng bù hai, bao gồm đủ miền [-32768, 32767]. Firmware lấy trực tiếp
counts từ cùng lần đọc tạo mẫu SI trong App, không dựng lại raw từ float.
Metadata lặp trong từng frame để host nối giữa phiên vẫn tự chuyển đổi được.
Các trường range/ODR phản ánh cấu hình cố định BSP; nếu đổi cấu hình sensor
phải cập nhật các hằng BSP tương ứng.

Bên nhận đổi `accel = raw * accel_range_g * 9.80665 / 32768` (m/s²),
`gyro = raw * gyro_range_dps * pi / (180 * 32768)` (rad/s), rồi áp dụng
ánh xạ tương ứng cho cả hai vector. Firmware hiện gửi `axis_map=2`, đổi
sensor → thân FRD bằng `[-X, +Y, -Z]`. FRD có X tiến, Y phải, Z xuống.
Viewer luôn hiển thị type 4 theo FRD: map 2 dùng `[-X, +Y, -Z]`; map 1 cũ
được đổi tiếp từ FLU sang FRD, tương đương `[+X, +Y, +Z]` từ sensor.
Giữ nguyên nghĩa map 1 để đọc log cũ; không áp cách gá mới cho log cũ.
Đổi sang kiểu rộng/float trước khi đảo dấu để giữ đúng raw -32768.
Vận tốc góc dương theo quy tắc bàn tay phải: roll nghiêng phải, pitch ngẩng
lên, yaw quay phải khi nhìn từ trên. Khi đứng yên đúng chiều, accel Z ≈ −g.
CSV raw giữ nguyên counts và metadata, không đảo dấu.

Decoder từ chối sai kích thước, sensor time ngoài 24 bit, accel range ngoài
{2, 4, 8, 16}, gyro range ngoài {125, 250, 500, 1000, 2000}, ODR bằng 0 hoặc
axis_map chưa hỗ trợ. Timestamp được chụp sau đọc BSP, không phải thời điểm
cảm biến acquisition hoặc host nhận. Clock cảm biến không được mở rộng tràn
hoặc đồng bộ với clock MCU.

Sequence và timestamp MCU tràn modulo 2^32; clock cảm biến tràn modulo 2^24.
Sequence tăng kể cả khi frame bị RTT từ chối, nhưng không đếm mẫu cảm biến
bị ghi đè trước khi MCU đọc. Một writer foreground, không retry; thiếu chỗ
thì bỏ nguyên frame mới và tăng counter đầy CH1.
800 Hz tương ứng 35.200 byte/s, chưa tính stats/RC/log.

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
- Fixture chung: [IMU raw gá cũ](fixtures/imu_raw_v1.hex), [IMU raw FRD 400 Hz](fixtures/imu_raw_frd_v1.hex), [IMU raw FRD 800 Hz](fixtures/imu_raw_frd_800hz_v1.hex), [thống kê RTT](fixtures/rtt_stats_v1.hex), [RC](fixtures/rc_v1.hex).
  Test C so output byte-for-byte; Python kiểm tra framing, CRC và giá trị
  trường theo cùng fixture.

Thêm loại dữ liệu mới bằng type mới cùng payload được tài liệu hóa; chỉ tăng
version khi thay đổi quy tắc không tương thích. Không đổi nghĩa/kiểu/đơn vị
của type đã có mà giữ nguyên version. Sequence riêng theo từng loại khi mở
rộng. CH1 có thể xen kẽ các type khác tần số; host phân loại theo type.

Chưa có boot ID; host không thể luôn phân biệt reset với tràn/gián đoạn dài.

## Rate preview v1 — type 5

Snapshot tính toán gửi tối đa 20 Hz khi rate đã được cấu hình; không điều khiển
ESC. Header `DRT!`, version 1, type 5, payload 80 byte, CRC32 như các bản tin
khác; tổng 92 byte. Số nguyên và IEEE-754 binary32 đều little-endian.

| Offset payload | Kiểu       | Nội dung                                                        |
| -------------- | ---------- | --------------------------------------------------------------- |
| 0              | uint32     | Sequence lần gửi thử, gồm lần RTT từ chối; reset khi App init   |
| 4              | uint32     | Timestamp đọc IMU cuối được controller tiêu thụ, µs modulo 2^32 |
| 8              | uint32     | dt giữa hai mẫu controller tiêu thụ, µs                         |
| 12             | uint32     | Flags: bit 0 configured, bit 1 valid; bit còn lại bằng 0        |
| 16             | float32[3] | Setpoint roll/pitch/yaw body FRD, rad/s                         |
| 28             | float32[3] | Gyro roll/pitch/yaw body FRD, rad/s                             |
| 40             | float32[3] | Output PID chuẩn hóa, [-1,1]                                    |
| 52             | float32[4] | M1–M4 lực đẩy chuẩn hóa, [0,1]                                  |
| 68             | float32[3] | Residual requested-minus-achieved roll/pitch/yaw                |

Giữa các bước rate, snapshot giữ kết quả trước đó. Khi reset do RC/IMU lỗi,
động học, timestamp và output bằng 0, valid tắt nhưng configured giữ nguyên.
Timestamp có thể lùi về 0 khi reset: host chỉ dùng sequence để nhận diện
bản tin trùng/lùi. Tuổi snapshot trên GUI tính theo thời gian host nhận;
không thay thế kiểm tra độ mới IMU trong firmware. Các lần mất bản tin thuộc
counter DATA chung; chưa có counter drop riêng type 5.

Fixture dùng chung C/Python: `protocol/fixtures/rate_v1.hex`. Viewer hiển thị
snapshot trong tab Rate · FRD và ghi `rate_*.csv` khi người dùng bật recording.

## Tư thế Mahony v1, type 6

Payload 24 byte, frame 36 byte, cùng header và CRC32 với các type khác.
Firmware gửi tối đa 20 Hz trên CH1 khi ước lượng còn mới.

| Offset | Kiểu        | Nội dung                                                                                 |
| ------ | ----------- | ---------------------------------------------------------------------------------------- |
| 0      | uint32      | Sequence riêng, tăng theo mỗi lần thử gửi                                                |
| 4      | uint32      | Timestamp đọc mẫu dùng để ước lượng, µs modulo 2^32                                      |
| 8      | float32 × 4 | Quaternion đơn vị theo thứ tự `w, x, y, z`, quay từ FRD sang NED với yaw ban đầu quy ước |

Viewer đổi quaternion thành roll, pitch và yaw tương đối theo độ. CSV
`attitude_*.csv` giữ nguyên quaternion và timestamp để xem lại.

## Vòng điều khiển attitude v1, type 7

Payload 56 byte, frame 68 byte, dùng cùng header và CRC32. App thử gửi trên
CH1 sau cập nhật motor cho mỗi lần tính vòng attitude với quaternion còn
mới ở SC MIDDLE/HIGH, danh định 100 Hz. Sequence tăng cả khi RTT đầy. Các
trường số dùng little-endian.

| Offset payload | Kiểu        | Nội dung                                                                        |
| -------------- | ----------- | ------------------------------------------------------------------------------- |
| 0              | uint32      | Sequence lần gửi thử                                                            |
| 4              | uint32      | Timestamp MCU tại đầu lần tính, µs modulo 2^32                                  |
| 8              | uint32      | Timestamp đọc mẫu dùng cho quaternion, µs modulo 2^32                           |
| 12             | uint32      | Bit 0 báo phép tính hợp lệ, các bit còn lại bằng 0                              |
| 16             | float32 × 4 | Quaternion FRD → NED đã dùng, theo thứ tự `w, x, y, z`                          |
| 32             | float32 × 2 | Góc đặt Euler roll/pitch, rad                                                   |
| 40             | float32 × 2 | Sai số nghiêng X/Y thân, bằng hai lần phần vector của quaternion sai số nghiêng |
| 48             | float32 × 2 | Tốc độ góc lệnh X/Y thân trước giới hạn của vòng rate, rad/s                    |

Hai trường cuối bằng 0 khi bit hợp lệ tắt. CSV giữ timestamp MCU và giá trị
gốc. Viewer chuyển quaternion và góc đặt sang độ để so sánh trên cùng trục
thời gian. Fixture dùng chung C/Python nằm tại `protocol/fixtures/attitude_control_v1.hex`.
