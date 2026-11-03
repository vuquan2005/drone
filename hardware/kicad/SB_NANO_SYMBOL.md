# Symbol SB Nano 2G4RX

Tạo theo nhãn trên ảnh người dùng cung cấp, chưa xác minh bằng schematic nhà sản xuất.

## Quy ước số chân
Nhìn mặt có linh kiện và chữ như ảnh, bốn pad ở cạnh dưới, từ trái sang phải:

| Số chân | Nhãn | Loại chân trong symbol |
|---|---|---|
| 1 | RX | Input |
| 2 | TX | Output |
| 3 | 5V | Power input |
| 4 | GND | Power input |

Số chân là quy ước tự đặt; phải khớp với số pad footprint khi thiết kế PCB.
RX/TX được hiểu theo phía module: RX nhận, TX phát. Ảnh không xác nhận mức điện áp logic.
Nhãn nguồn 5V không có nghĩa chân tín hiệu RX/TX chịu được 5V.

Symbol có thân đứng, cả bốn chân ở cạnh dưới theo ảnh: RX – TX – 5V – GND.
Chưa gán footprint vì ảnh không cung cấp kích thước và khoảng cách pad.
Đầu nối anten không được biểu diễn thành chân schematic ngoài vì đây là symbol của cả module.

## Thêm vào KiCad
Symbol Editor → Preferences → Manage Symbol Libraries → Add existing library.
Chọn symbols/SB_Nano_2G4RX_Physical.kicad_sym và lưu, sau đó nhấn A trong schematic để tìm SB_Nano_2G4RX_Physical.

