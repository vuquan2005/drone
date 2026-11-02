# STM32F103VET6 dev kit

Chọn `DevKit_STM32:STM32F103VET6_DevKit` hoặc
`DevKit_STM32:STM32F103VET6_DevKit_Headers`. Cả hai tên hiện dùng cùng
một symbol module 98 chân, GPIO không có tiền tố P, BOOT0/BOOT1 ở cạnh
trên bên phải nhóm debug. Các phiên bản cũ lưu ở
`DevKit_STM32_legacy.kicad_sym`, không đăng ký vào thư viện dự án.

## Bảng chân

Nguồn là `stm32_header_layout.csv`, USB hướng lên. Dòng 17 cột 18 đã sửa
C3 thành C9 theo xác nhận người dùng. Giữ tên A0…E15 như board; VB thành
VBAT. Header trái/phải mỗi bên 2×18, dưới 2×10, SWD 1×4, thêm BOOT0/1
đại diện chân giữa của jumper. Chân nguồn lặp và SWD được giữ riêng.
Không thêm USB hay nút RST thành chân symbol.

Symbol có 80 GPIO xuất hiện đúng một lần và 18 chân nguồn/debug/BOOT.
Chân dọc xen kẽ ngoài O / trong I, từ trên xuống; header dưới xen kẽ
trong I / ngoài O, từ trái sang phải. Debug ở trên từ trái sang phải:
GND, SWDIO, SWCLK, 3V3, rồi BOOT0, BOOT1.

ID LO01, LI01, RO01, RI01, BI01, BO01, S1… là quy ước vị trí riêng,
không phải số chân MCU hay số nhà sản xuất. BOOT0/1 có ID cùng tên.
Chưa gán footprint. GPIO/debug dùng bidirectional, BOOT dùng input,
nguồn dùng passive. Kết nối bên trong board không được mô tả riêng.

`DevKit_STM32_headers.csv` chứa từng ID và ô CSV nguồn.
Tạo lại thư viện bằng `create_header_symbol.py`, không chạy script cũ
`create_devkit_symbol.py` vì script đó tạo các bản logic trước đây.

## Làm mới trong KiCad

Nếu symbol đã được đặt trên schematic, dùng **Tools → Update Symbols
from Library…** để lấy phiên bản mới; KiCad lưu bản sao symbol trong
schematic nên sửa thư viện không tự thay bản đã đặt.
Nếu vẫn thấy bản cũ trong hộp chọn, đóng/mở lại KiCad và kiểm tra đường
dẫn thư viện DevKit_STM32 trỏ tới thư mục dự án này.
