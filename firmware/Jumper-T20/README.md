# Jumper T20

Bản sao cấu hình tay điều khiển Jumper T20 cho model **DA-Drone**.
Hai tệp YAML ghi phiên bản `2.11.0`:

- [`radio.yml`](radio.yml): cấu hình chung, hiệu chuẩn cần điều khiển và công tắc.
- [`model04.yml`](model04.yml): input, mixer và cấu hình module CRSF của model.

| Kênh | Chức năng | Nguồn |
| --- | --- | --- |
| CH1 | Roll | Ail |
| CH2 | Pitch | Ele |
| CH3 | Throttle | Thr |
| CH4 | Yaw | Rud |
| CH5 | ARM | SA |
| CH6 | Mode | SC |
| CH7 | Calib - ESC | SB |
| CH8 | Telem - ghi/xuất telemetry | SD |

SB dùng yêu cầu hiệu chuẩn ESC. SD chọn một trong ba trạng thái: tắt,
xuất telemetry hoặc ghi telemetry; không xuất và ghi đồng thời.
Ánh xạ vị trí công tắc sang từng trạng thái cần được quy định trong firmware nhận.

Các mixer dùng trọng số 100%, offset 0. Cả hai mục module trong model
đều đặt `TYPE_CROSSFIRE`, 16 kênh, `failsafeMode: NOT_SET`.
ARM, Mode, Calib và Telem là tên kênh trong cấu hình tay điều khiển; hành vi tương ứng
phụ thuộc firmware nhận.

Khi dùng trên tay điều khiển khác, sao lưu cấu hình hiện tại và hiệu chuẩn
lại cần điều khiển; `radio.yml` chứa dữ liệu hiệu chuẩn riêng của thiết bị.
