# Drone Direct Wire

Mở `Drone_Direct_Wire.kicad_pro` bằng KiCad, sau đó mở schematic cùng tên.
Đây là sơ đồ nối dây giữa các module; chưa có PCB hoặc footprint được xác minh.

- `.kicad_pro` và `.kicad_sch`: nguồn thiết kế được lưu trong Git.
- `symbols/`: tám thư viện riêng, gồm sáu module đầu vào và hai thư viện phụ.
- `sym-lib-table`: đăng ký bằng `${KIPRJMOD}/symbols/`, dùng được khi đổi máy.
- `.kicad_prl`, lock, cache và backup: trạng thái cục bộ, được Git bỏ qua.
- `gerber/`, `exports/`: đầu ra tự sinh khi cần, được Git bỏ qua.

Giữ nguyên tên thư viện và mã chân khi sắp xếp file. Symbol đang dùng cũng được
nhúng trong schematic. Sáu thư viện đầu vào tại `../symbols/` được script sao chép
nguyên byte vào `symbols/`; chỉnh sửa bản sao có thể bị ghi đè khi dựng lại.

Xem [tài liệu bàn giao và giới hạn điện](../../../output/README.md) cho ánh xạ
chân, nguồn, UART, SPI và các mục chưa xác minh trên module thực tế.

Kiểm tra từ gốc repository, với `kicad-cli` trong PATH:

```powershell
kicad-cli sch export netlist --format kicadxml -o output/validation/Drone_Direct_Wire.net.xml hardware/kicad/Drone_Direct_Wire/Drone_Direct_Wire.kicad_sch
kicad-cli sch erc --format json --exit-code-violations -o output/validation/Drone_Direct_Wire.erc.json hardware/kicad/Drone_Direct_Wire/Drone_Direct_Wire.kicad_sch
python output/verify_design.py
```

Script kiểm tra ghi lại BOM, bảng nối dây và kết quả trong `output/`; giữ nguyên
các bằng chứng trước thay đổi để đối chiếu. Muốn dựng lại từ thư viện đầu vào,
xem cảnh báo ghi đè trong [README phần cứng](../../README.md).

## Kiểm tra sau khi sắp xếp (2026-11-03)

File dự án, schematic và symbol giữ nguyên byte so với bản gốc. Các nhóm chân
trong netlist KiCad trước/sau giống nhau; mọi đường dẫn thư viện cục bộ tồn tại.
ERC ở cả hai vị trí cho cùng một cảnh báo `lib_symbol_mismatch` của STM32,
không có lỗi. Script `verify_design.py` ở cả bản gốc và bản đã chuyển đường dẫn
đều dừng ở so sánh symbol nhúng A2212; chưa thể coi kiểm tra tổng hợp là đạt.
Báo cáo đối chiếu hiện nằm trong `exports/` (cục bộ, không theo dõi trong Git).
