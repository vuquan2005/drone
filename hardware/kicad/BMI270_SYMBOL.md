# Symbol module BMI270 V1.3

Thư viện: BMI270_DevKit.kicad_sym
Symbol: BMI270_DevKit:BMI270_DevKit_V1_3

## Thêm vào KiCad
1. Mở Symbol Editor → Preferences → Manage Symbol Libraries.
2. Chọn Project Specific hoặc Global → Add existing library.
3. Chọn BMI270_DevKit.kicad_sym, lưu lại.
4. Trong Schematic Editor, nhấn A rồi tìm BMI270_DevKit_V1_3.

File sym-lib-table chỉ dùng trực tiếp khi thư viện nằm cùng thư mục gốc project.
Không ghi đè sym-lib-table đã có; dùng Manage Symbol Libraries để thêm.

## Quy ước chân
Nhìn mặt có chữ như ảnh tham chiếu, VCC ở góc trên bên phải.
Đây là số chân tự quy ước, không phải pinout chính thức của nhà sản xuất.

| Số | Nhãn | Vị trí trên ảnh |
|---|---|---|
| 1 | VCC | Phải |
| 2 | GND | Phải |
| 3 | AD0/MISO | Phải |
| 4 | SDA/MOSI | Phải |
| 5 | SCL/SCLK | Phải |
| 6 | CS | Phải |
| 7 | INT1 | Phải |
| 8 | SDX | Trái |
| 9 | SCX | Trái |
| 10 | INT2 | Trái |

Hàng phải: 1–7 từ trên xuống. Hàng trái: 8–10 từ trên xuống.
AD0 dùng chữ số 0; các nhãn còn lại theo ảnh.
Chân 3 và 4 có hướng tín hiệu khác nhau tùy chế độ giao tiếp, được đặt bidirectional.
SDX và SCX cũng dùng bidirectional để thể hiện vai trò phụ thuộc cấu hình.

Chỉ tạo symbol module, chưa gán footprint. Khi tạo footprint, phải đánh số pad
theo bảng này hoặc sửa lại số chân symbol tương ứng. Ảnh không đủ để xác nhận
kích thước, khoảng cách hai hàng chân, điện áp VCC hoặc mạch bên trong module.
Không dùng số chân IC BMI270 thay cho số chân module.
