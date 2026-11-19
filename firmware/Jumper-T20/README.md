# Jumper T20

Bản sao cấu hình tay điều khiển Jumper T20 cho model **DA-Drone**.
Hai tệp YAML ghi phiên bản `2.11.0`:

- [`radio.yml`](radio.yml): cấu hình chung, hiệu chuẩn cần điều khiển và công tắc.
- [`model04.yml`](model04.yml): input, mixer và cấu hình module CRSF của model.

| Kênh | Chức năng                  | Nguồn |
| ---- | -------------------------- | ----- |
| CH1  | Roll                       | Ail   |
| CH2  | Pitch                      | Ele   |
| CH3  | Throttle                   | Thr   |
| CH4  | Yaw                        | Rud   |
| CH5  | ARM                        | SA    |
| CH6  | Mode - chế độ bay          | SC    |
| CH7  | Calib - ESC hoặc IMU       | SB    |
| CH8  | Telem - ghi/xuất telemetry | SD    |

Khi khởi động, SB HIGH cùng SC ở giữa chọn hiệu chuẩn IMU. Với SB HIGH và
SC ở hai vị trí còn lại, firmware chọn hiệu chuẩn ESC. Khi SB ở LOW, SD
HIGH yêu cầu chế độ xuất telemetry lúc khởi động. Trong chế độ bay, SC là
yêu cầu chế độ bay và SD chọn tắt, ghi hoặc xuất telemetry theo
[bảng chọn chế độ của firmware](../drone/README.md#chọn-chế-độ-sau-reset).

Các mixer dùng trọng số 100%, offset 0. Cả hai mục module trong model
đều đặt `TYPE_CROSSFIRE`, 16 kênh, `failsafeMode: NOT_SET`.
ARM, Mode, Calib và Telem là tên kênh trong cấu hình tay điều khiển; hành vi tương ứng
phụ thuộc firmware nhận.

Khi dùng trên tay điều khiển khác, sao lưu cấu hình hiện tại và hiệu chuẩn
lại cần điều khiển; `radio.yml` chứa dữ liệu hiệu chuẩn riêng của thiết bị.
