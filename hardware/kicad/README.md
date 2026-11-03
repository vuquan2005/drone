# Drone Direct Wire

Mở `Drone_Direct_Wire.kicad_pro` bằng KiCad, sau đó mở schematic cùng tên.
Đây là sơ đồ nối dây giữa các module; chưa có PCB hoặc footprint được xác minh.

- `.kicad_pro` và `.kicad_sch`: nguồn thiết kế được lưu trong Git.
- `symbols/`: thư viện `DA_Drone`, chứa các symbol tự tạo của dự án.
- `PWR_FLAG` dùng thư viện chuẩn `power` của KiCad, không lưu trong `DA_Drone`.
- `sym-lib-table`: đăng ký bằng `${KIPRJMOD}/symbols/`, dùng được khi đổi máy.
- `.kicad_prl`, lock, cache và backup: trạng thái cục bộ, được Git bỏ qua.
- `gerber/`, `exports/`: đầu ra tự sinh khi cần, được Git bỏ qua.

Giữ nguyên tên thư viện và mã chân khi sắp xếp file. Symbol đang dùng cũng được
nhúng trong schematic. Chỉnh sửa thư viện trực tiếp tại `symbols/` bằng KiCad.
Script dựng sơ đồ đọc bộ thư viện này và giữ nguyên các file symbol cùng
`sym-lib-table`. Khi sửa thư viện, cập nhật symbol trên schematic bằng KiCad
để bản nhúng và thư viện đồng bộ.

Tài liệu bàn giao cũ tại `output/README.md` ở gốc repo chỉ còn trên máy, không
có trong bản clone mới. Các báo cáo trong `exports/` cũng chỉ là bản đối chiếu
cục bộ; không chỉnh sửa các thư viện trong `exports/baseline/`.

Kiểm tra từ gốc repository, với `kicad-cli` trong PATH:

```powershell
kicad-cli sch export netlist --format kicadxml -o output/validation/Drone_Direct_Wire.net.xml hardware/kicad/Drone_Direct_Wire.kicad_sch
kicad-cli sch erc --format json --exit-code-violations -o output/validation/Drone_Direct_Wire.erc.json hardware/kicad/Drone_Direct_Wire.kicad_sch
python output/verify_design.py
```

Script kiểm tra cục bộ `output/verify_design.py` không có trong bản clone mới.
Script kiểm tra ghi lại BOM, bảng nối dây và kết quả trong `output/`; giữ nguyên
các bằng chứng trước thay đổi để đối chiếu. Muốn dựng lại từ thư viện đầu vào,
xem cảnh báo ghi đè trong [README phần cứng](../README.md).

## Kết quả đối chiếu trước đây (2026-11-03)

File dự án, schematic và symbol giữ nguyên byte so với bản gốc. Các nhóm chân
trong netlist KiCad trước/sau giống nhau; mọi đường dẫn thư viện cục bộ tồn tại.
ERC ở cả hai vị trí cho cùng một cảnh báo `lib_symbol_mismatch` của STM32,
không có lỗi. Script `verify_design.py` ở cả bản gốc và bản đã chuyển đường dẫn
đều dừng ở so sánh symbol nhúng A2212; chưa thể coi kiểm tra tổng hợp là đạt.
Báo cáo đối chiếu hiện nằm trong `exports/` (cục bộ, không theo dõi trong Git).

## Cấu trúc

```text
kicad/
├── Drone_Direct_Wire.kicad_pro
├── Drone_Direct_Wire.kicad_sch
├── sym-lib-table
├── symbols/                 # DA_Drone.kicad_sym chứa bảy symbol
├── resources/
│   ├── STM32_DevKit_headers.csv
│   └── stm32_header_layout.csv
├── README.md                # Hướng dẫn và quy ước symbol
└── exports/                 # Đối chiếu cục bộ, Git bỏ qua
```

## Dựng lại sơ đồ

Script được duy trì tại `tools/build_quadrotor.py` ở gốc repository; không đặt
trong `temp/` vì đây là công cụ có thể dùng lại. Chạy từ gốc repository:

```powershell
python tools/build_quadrotor.py
```

Script đọc thêm `power:PWR_FLAG` từ thư viện chuẩn KiCad 10. Trên Linux,
mặc định là `/usr/share/kicad/symbols`; đặt biến môi trường `KICAD10_SYMBOL_DIR`
nếu cài thư viện ở đường dẫn khác.

Lệnh này **ghi đè schematic và cấu hình dự án**, đồng thời cập nhật manifest
trong `output/validation/`. Chỉ chạy khi chủ ý dựng lại; symbol và bảng thư viện
được giữ nguyên. Các chỉnh sửa schematic thủ công có thể bị mất.

## Dữ liệu chân STM32

`resources/DevKit_STM32_headers.csv` ánh xạ vị trí header sang tín hiệu;
`resources/stm32_header_layout.csv` lưu bố trí chân theo hướng USB. Giữ cả hai vì
chúng biểu diễn dữ liệu ở hai dạng khác nhau.

## Quy ước symbol

Thư viện riêng của dự án mang tên `DA_Drone`. Tên symbol dùng loại linh kiện
hoặc họ module, không chứa thông số điện, phiên bản hay hậu tố bố trí.
Ví dụ ID đầy đủ: `DA_Drone:BLDC_Motor`. Value mặc định trùng tên symbol.
Chỉ giữ năm property chuẩn: Reference, Value, Footprint, Datasheet, Description.
Reference và Value hiển thị; các trường còn lại ẩn. Description chỉ mô tả chức
năng ngắn gọn. Footprint và Datasheet để trống khi chưa xác minh.
Thông số linh kiện thực tế và quy ước chân được ghi trong tài liệu bên dưới.

| Symbol           | Linh kiện dùng trong dự án                                |
| ---------------- | --------------------------------------------------------- |
| BLDC_Motor       | A2212/10T, 1400 KV                                        |
| LiPo_Battery     | OVONIC, 3S1P, 11.1 V, 5200 mAh, 90C, 57.72 Wh (theo nhãn) |
| BLDC_ESC         | ESC 30 A; chức năng/điện áp BEC chưa xác minh             |
| STM32_DevKit     | STM32F103VET6 dev kit, hướng USB lên                      |
| BMI270_Module    | Breakout V1.3                                             |
| SB_Nano_Receiver | SB Nano 2G4RX                                             |
| LM2596_Module    | Module LM2596, đặt đầu ra 5.0 V; cần đo kiểm              |

Số chân là quy ước cục bộ, chưa có footprint xác minh. Pin LiPo chỉ biểu diễn
hai cực nguồn chính (1 dương, 2 âm), không có đầu nối cân bằng.
Tên mới đã được cập nhật trong schematic hiện tại, gồm symbol nhúng và instance;
không cần dựng lại sơ đồ. Khi sửa thư viện về sau, dùng **Tools → Update Symbols
from Library** và kiểm tra tùy chọn cập nhật property/trạng thái hiển thị.

### A2212/10T 1400KV

- Library: `symbols/DA_Drone.kicad_sym`
- KiCad symbol: `DA_Drone:BLDC_Motor`
- Reference prefix: `M`
- Pins: `1 = U`, `2 = V`, `3 = W`, all passive.
- Pin numbers are a local schematic convention, not manufacturer terminal numbers or wire colors.
- This is an externally wired motor: `on_board no`, with no assigned PCB footprint.
- Connect U/V/W to the three motor outputs of the ESC.

The project-local `sym-lib-table` registers the library. In the project's Schematic Editor,
press **A**, then search for `BLDC_Motor` to place it.

### Symbol module BMI270 V1.3

Thư viện: symbols/DA_Drone.kicad_sym
Symbol: DA_Drone:BMI270_Module

#### Thêm vào KiCad

1. Mở Symbol Editor → Preferences → Manage Symbol Libraries.
2. Chọn Project Specific hoặc Global → Add existing library.
3. Chọn symbols/DA_Drone.kicad_sym, lưu lại.
4. Trong Schematic Editor, nhấn A rồi tìm BMI270_Module.

Bảng sym-lib-table đăng ký thư viện tương đối với thư mục dự án.
Không ghi đè sym-lib-table đã có; dùng Manage Symbol Libraries để thêm.

#### Quy ước chân

Nhìn mặt có chữ như ảnh tham chiếu, VCC ở góc trên bên phải.
Đây là số chân tự quy ước, không phải pinout chính thức của nhà sản xuất.

| Số  | Nhãn     | Vị trí trên ảnh |
| --- | -------- | --------------- |
| 1   | VCC      | Phải            |
| 2   | GND      | Phải            |
| 3   | AD0/MISO | Phải            |
| 4   | SDA/MOSI | Phải            |
| 5   | SCL/SCLK | Phải            |
| 6   | CS       | Phải            |
| 7   | INT1     | Phải            |
| 8   | SDX      | Trái            |
| 9   | SCX      | Trái            |
| 10  | INT2     | Trái            |

Hàng phải: 1–7 từ trên xuống. Hàng trái: 8–10 từ trên xuống.
AD0 dùng chữ số 0; các nhãn còn lại theo ảnh.
Chân 3 và 4 có hướng tín hiệu khác nhau tùy chế độ giao tiếp, được đặt bidirectional.
SDX và SCX cũng dùng bidirectional để thể hiện vai trò phụ thuộc cấu hình.

Chỉ tạo symbol module, chưa gán footprint. Khi tạo footprint, phải đánh số pad
theo bảng này hoặc sửa lại số chân symbol tương ứng. Ảnh không đủ để xác nhận
kích thước, khoảng cách hai hàng chân, điện áp VCC hoặc mạch bên trong module.
Không dùng số chân IC BMI270 thay cho số chân module.

### STM32F103VET6 dev kit

Chọn `DA_Drone:STM32_DevKit`. Đây là symbol STM32 duy nhất
trong thư viện dự án: module 98 chân, GPIO không có tiền tố P,
BOOT0/BOOT1 ở cạnh trên bên phải nhóm debug. Các bản cũ đã được xóa.

#### Bảng chân

Nguồn là `resources/stm32_header_layout.csv`, USB hướng lên. Dòng 17 cột 18 đã sửa
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

`resources/DevKit_STM32_headers.csv` chứa từng ID và ô CSV nguồn.
Tệp thư viện hiện hành: `symbols/DA_Drone.kicad_sym`.

#### Làm mới trong KiCad

Nếu symbol đã được đặt trên schematic, dùng **Tools → Update Symbols
from Library…** để lấy phiên bản mới; KiCad lưu bản sao symbol trong
schematic nên sửa thư viện không tự thay bản đã đặt.
Nếu vẫn thấy bản cũ trong hộp chọn, đóng/mở lại KiCad và kiểm tra đường
dẫn thư viện DA_Drone trỏ tới thư mục dự án này.

### ESC 30A BLDC

Symbol: `DA_Drone:BLDC_ESC`. Reference prefix: `U`.
External wired module; no PCB footprint assigned.

| Pin | Name    | Electrical type |
| --- | ------- | --------------- |
| 1   | BAT+    | power_in        |
| 2   | BAT-    | power_in        |
| 3   | U       | passive         |
| 4   | V       | passive         |
| 5   | W       | passive         |
| 6   | PWM     | input           |
| 7   | BEC/VCC | passive         |
| 8   | GND     | power_in        |

Numbers are a local logical convention, not physical connector cavity numbers.
The photo shows a 30A marking, two power leads, three motor leads and a three-wire
control lead. Exact model, supply range, control protocol and BEC function/voltage
are not established from the image. Pin 7 is intentionally passive until verified
against the actual ESC. Motor phase U/V/W names are a local convention.

Add from the project's symbol chooser with `BLDC_ESC`.

### Symbol SB Nano 2G4RX

Tạo theo nhãn trên ảnh người dùng cung cấp, chưa xác minh bằng schematic nhà sản xuất.

#### Quy ước số chân

Nhìn mặt có linh kiện và chữ như ảnh, bốn pad ở cạnh dưới, từ trái sang phải:

| Số chân | Nhãn | Loại chân trong symbol |
| ------- | ---- | ---------------------- |
| 1       | RX   | Input                  |
| 2       | TX   | Output                 |
| 3       | 5V   | Power input            |
| 4       | GND  | Power input            |

Số chân là quy ước tự đặt; phải khớp với số pad footprint khi thiết kế PCB.
RX/TX được hiểu theo phía module: RX nhận, TX phát. Ảnh không xác nhận mức điện áp logic.
Nhãn nguồn 5V không có nghĩa chân tín hiệu RX/TX chịu được 5V.

Symbol có thân đứng, cả bốn chân ở cạnh dưới theo ảnh: RX – TX – 5V – GND.
Chưa gán footprint vì ảnh không cung cấp kích thước và khoảng cách pad.
Đầu nối anten không được biểu diễn thành chân schematic ngoài vì đây là symbol của cả module.

#### Thêm vào KiCad

Symbol Editor → Preferences → Manage Symbol Libraries → Add existing library.
Chọn symbols/DA_Drone.kicad_sym và lưu, sau đó nhấn A trong schematic để tìm SB_Nano_Receiver.

## Kiểm tra sau khi gộp thư mục

Sơ đồ, cấu hình và tám thư viện giữ nguyên nội dung. Netlist và kết quả ERC
giống trước khi gộp: còn một cảnh báo STM32 không khớp thư viện, không có lỗi.
Script dựng sơ đồ đã được chạy thử trong thư mục riêng và giữ nguyên các
file symbol cùng bảng thư viện. Không dựng lại sơ đồ đang chỉnh sửa.
