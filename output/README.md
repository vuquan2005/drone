# Quadrotor - sơ đồ module nối wire trực tiếp

Bộ kết quả được tạo và kiểm tra bằng **KiCad CLI 10.0.7** ngày 2026-11-02.
Schematic một trang A2 ngang, phía trước ở trên. Đây là sơ đồ liên kết các
module bên ngoài; chưa thiết kế PCB, footprint hoặc firmware.

## Vị trí nguồn hiện hành

Nguồn chỉnh sửa đã chuyển sang [dự án KiCad](../hardware/kicad/Drone_Direct_Wire/README.md).
Thư mục này giữ bản xuất và bằng chứng kiểm tra trước khi dọn dẹp; các thư viện
ở đây là bản sao lịch sử, không chỉnh sửa chúng. Bản perfboard cục bộ được giữ nguyên.
Các số liệu và đường dẫn trong phần bàn giao bên dưới mô tả bộ kết quả cũ.

## Mở và xem

Mở `../hardware/kicad/Drone_Direct_Wire/Drone_Direct_Wire.kicad_pro` bằng KiCad, sau đó mở schematic cùng tên.
Cũng có thể mở trực tiếp `Drone_Direct_Wire.kicad_sch` bằng Schematic Editor.
Để tra cứu thư viện, giữ các `.kicad_sym` và `sym-lib-table` cạnh schematic.
Tất cả symbol đã dùng cũng được **nhúng trong `lib_symbols`** của schematic.
Các đường dẫn thư viện dùng `${KIPRJMOD}`, không phụ thuộc đường dẫn máy tạo.

- `Drone_Direct_Wire.kicad_sch`: schematic chỉnh sửa được.
- `Drone_Direct_Wire.kicad_pro`: dự án schematic; không có PCB.
- `Drone_Direct_Wire.pdf`: bản vẽ vector một trang A2.
- `Drone_Direct_Wire.png`: render trực tiếp từ PDF, 300 dpi.
- Sáu thư viện gốc `.kicad_sym`, thư viện U7 logic, `ERC_Only.kicad_sym` và `sym-lib-table`.
- `Bang_noi_day.csv`: 43 quan hệ nguồn/chân - đích/chân, có tên và mã chân thực.
- `BOM.csv`: 7 loại module, tổng cộng 13 thiết bị.
- `validation/Drone_Direct_Wire.net.xml`: netlist do KiCad xuất.
- `validation/Drone_Direct_Wire.erc.json`: báo cáo ERC gốc, không lọc lỗi.
- `validation/Drone_Direct_Wire.erc.rpt`: ERC dạng văn bản.
- `validation/before_PWR_FLAG.erc.json`: hai lỗi nguồn trước khi thêm khai báo ERC.
- `validation/power_sources.json`: đường nguồn và net thực tế đã đối chiếu.
- `validation/KiCad_BOM.csv`: BOM xuất độc lập bằng KiCad, không có PWR_FLAG.
- `validation/manifest.json`: hash thư viện đầu vào và các mạng dự kiến.
- `validation/verification.json`: kết quả đối chiếu độc lập với yêu cầu.
- `validation/visual_qa.json`: kiểm tra hình học/readability của plugin, kết quả PASS.
- `verify_design.py`: chạy lại kiểm tra bằng Python chuẩn sau khi xuất netlist/ERC.
- `Drone_Direct_Wire_bundle.zip`: gói bàn giao gồm dự án, thư viện, bản xuất và báo cáo.

CSV dùng UTF-8 có BOM để Excel nhận dạng đúng. Các dòng bảng nối dây thể hiện
quan hệ trong cùng mạng điện; mạng nguồn/GND chia nhánh dùng chung wire trên
schematic. Không dùng net label, global label, hierarchical label hoặc ký hiệu
nguồn để thay đường nối. Hai PWR_FLAG là ngoại lệ được người dùng cho phép:
chúng chỉ khai báo nguồn cho ERC và có wire nhánh nối tới net đã xác minh.

## Symbol và giới hạn của U7

Không tìm thấy symbol LM2596 module trong `D:\kicad` hoặc workspace đầu vào.
Vì vậy U7 dùng **symbol khối module logic** được tạo theo phần cho phép của
yêu cầu. Đây là module DC-DC hoàn chỉnh, không phải chip LM2596 rời.

| Mã chân U7 trên sơ đồ | Tên đầu nối | Kiểu điện của symbol logic |
| --- | --- | --- |
| 1 | IN+ | power_in |
| 2 | IN- | power_in |
| 3 | OUT+ | power_out |
| 4 | OUT- | power_out |

**Các số 1-4 chỉ là mã logic, chưa xác minh số chân, vị trí đầu nối hoặc
footprint của module thực tế.** Phải đối chiếu nhãn IN+/IN-/OUT+/OUT- trên
module trước khi đấu. Không có footprint nào được gán.

Sáu thư viện có sẵn được sao chép nguyên byte từ `hardware/kicad`; các symbol
nhúng được đối chiếu với thư viện và giữ nguyên tên, số, kiểu điện, hình vẽ
và tọa độ tương đối của chân. Chỉ xoay/di chuyển nguyên symbol và đặt lại
vị trí trường Reference/Value cho dễ đọc. A1 không bị tách thành chip rời.

Tên U1.3 trong symbol gốc là **`AD0/MISO`** (chữ số 0), thay vì cách viết
`ADO/MISO` trong yêu cầu; sơ đồ vẫn dùng đúng chân 3 này. Các header A4/A5...
là tên board, tương ứng PA4/PA5...; LO/LI/RO/RI là mã header riêng của symbol,
không phải số chân IC. Không đổi tên hoặc chuyển âm thầm sang chân khác.

## Bố trí và đi dây

A1 ở giữa; U1 nằm sát cạnh trái nhóm A4-A7, khoảng cách ngang giữa điểm nối
chân SPI của U1 và A1 là 12,7 mm. U2 ở chéo dưới phải. BT1 và U7 ở chéo
trên phải. Bốn nhánh giữ thứ tự trung tâm - ESC - động cơ:

| Vị trí | ESC | Động cơ | PWM từ A1 |
| --- | --- | --- | --- |
| Trên | U3 / ESC1 | M1 | B6/PB6, RO05, TIM4_CH1 |
| Phải | U4 / ESC2 | M2 | B7/PB7, RI05, TIM4_CH2 |
| Dưới | U5 / ESC3 | M3 | B8/PB8, RO04, TIM4_CH3 |
| Trái | U6 / ESC4 | M4 | B9/PB9, RI04, TIM4_CH4 |

Không gán chiều quay, mixer, cánh quạt hoặc trục tọa độ. Mỗi ESC nối chân
3 U / 4 V / 5 W đến động cơ cùng số, chân 1 U / 2 V / 3 W. Không có động cơ
nào nối vào GPIO hoặc nối thẳng hai cực DC của LiPo.

133 đoạn wire ngang/dọc tạo 26 mạng điện, có junction tại các điểm rẽ nối.
Giao cắt không nối không có junction. Tất cả 93 chân ngoài không sử dụng được đánh No Connect;
dấu này chỉ biểu thị không dùng chân ở ngoài module, không sửa mạch nội bộ.

## SPI1 / BMI270

| U1 | A1 |
| --- | --- |
| 1 VCC | LI02 3V3 |
| 2 GND | GND chung |
| 3 AD0/MISO | LO14 A6/PA6 |
| 4 SDA/MOSI | LI14 A7/PA7 |
| 5 SCL/SCLK | LI13 A5/PA5 |
| 6 CS | LO13 A4/PA4 |

U1.7 INT1, U1.10 INT2, U1.8 SDX và U1.9 SCX đều NC. PA0 cũng NC;
phương án đọc BMI270 định kỳ qua SPI, không dùng ngắt.
Cần xác minh SDX/SCX trên **module V1.3** là giao tiếp phụ ASDx/ASCx,
tránh nhầm với SDx/SCx chính của chip Bosch.

Cấu hình đã ghi trên schematic: SPI1 master, full duplex, 8 bit, MSB first;
Mode 0 CPOL=0/CPHA=0; software NSS; clock SPI1 72 MHz, prescaler /32,
SCK 2,25 MHz. PA4 output push-pull, khởi tạo High, CS tích cực Low.
Firmware phải thực hiện trình tự chọn SPI và khởi tạo BMI270 theo datasheet
và Bosch SensorAPI. **Đây là ghi chú cấu hình, không có nghĩa firmware đã
được tạo, biên dịch hoặc nạp.**

## USART1 / ELRS và PWM

U2.1 RX nối trực tiếp A1.RO16 A9/PA9 USART1_TX;
U2.2 TX nối trực tiếp A1.RI15 A10/PA10 USART1_RX.
U2.3 nhận 5 V từ U7.OUT+, U2.4 nối GND chung.
Cấu hình ghi trên schematic: CRSF 420000 baud, 8N1, không đảo,
full duplex, không hardware flow control.

TIM4 dùng ánh xạ mặc định, không remap. Bốn đầu PWM ở bảng phía trên tách
thành bốn mạng riêng, đi đến chân 6 của bốn ESC. Tần số PWM, khoảng độ rộng
xung và hiệu chuẩn phải theo ESC thực tế; không giả định 400 Hz hoặc DShot.
PA13/PA14 và SWDIO/SWCLK không dùng cho PWM; giữ cho SWD. Đặt jumper BOOT0
có sẵn trên DevKit ở mức 0; không thêm bộ nạp, jumper hoặc đầu nối mới.

## Nguồn và các việc phải xác minh trên phần cứng

BT1.+ cấp đồng thời U7.IN+ và BAT+ chân 1 của U3-U6. BT1.- nối U7.IN-,
U7.OUT-, BAT- chân 2 và GND tín hiệu chân 8 của bốn ESC, A1.LO01,
U1.2 và U2.4. IN-/OUT- được nối chung bằng wire bên ngoài trên sơ đồ.

U7.OUT+ điều chỉnh thành **5,0 V** cấp A1.LO03 và U2.3. A1.LI02 3V3 cấp
U1.1. Chân 7 BEC/VCC của cả bốn ESC đều **NC**. Các header nguồn trùng
tên khác của A1 không sử dụng ở ngoài được NC.

Trước khi đấu và cấp nguồn:

1. Đối chiếu các đầu IN+/IN-/OUT+/OUT- thực tế của U7; mã logic không phải pad map.
2. Điều chỉnh và đo U7.OUT+ so với OUT- đạt 5,0 V trước khi nối tải.
3. Xác nhận DevKit cho phép cấp tại chân 5V LO03, và bộ ổn áp 3V3 trên board
   đủ khả năng cấp BMI270; xác nhận module BMI270 V1.3 hỗ trợ nguồn 3,3 V.
4. Xác nhận LM2596 module đủ dòng thực tế, điều kiện tản nhiệt và dải vào
   cho toàn bộ phần logic. LiPo 3S danh định 11,1 V, đầy 12,6 V.
5. Không đưa LiPo vào 5V, 3V3 hoặc VBAT backup của STM32; không đưa đầu ra
   5 V của U7 vào 3V3. Không nối BEC ESC vào đường nguồn logic.
6. USB không cấp nguồn trong phương án này. Chỉ cắm USB đồng thời sau khi
   xác minh mạch nguồn DevKit tránh cấp ngược.
7. Xác minh chân SDX/SCX phụ của BMI270; mức logic UART của ELRS phù hợp
   STM32; dải nguồn, giao thức và tham số xung của từng ESC thực tế.
8. Các số chân motor, ESC và module trong thư viện là quy ước sơ đồ;
   đối chiếu dây/đầu nối thực tế. Chưa xác định chiều quay hoặc motor mixer.

Không có xác minh phần cứng, đo điện áp hoặc nạp firmware trong công việc này.

## Kết quả kiểm tra

Đã dùng KiCad để đọc schematic, xuất netlist và PDF, chạy ERC. Không dùng
kiểm tra cú pháp thay cho ERC. Đối chiếu độc lập netlist xuất từ KiCad với
toàn bộ ánh xạ chân yêu cầu, sau đó dựng lại đồ thị nối từ tọa độ wire:

- Đúng 13 thiết bị: 1 STM32, 1 BMI270, 1 ELRS, 4 ESC, 4 motor, 1 LiPo, 1 LM2596.
- Đúng 26 mạng nối, 69 chân sử dụng, 93 chân NC; bảng nối dây 43 dòng.
- Sáu thư viện gốc không thay đổi; symbol nhúng giống thư viện.
- Không wire hở vì lệch điểm chân; không ngắn nguồn hoặc nối nhầm giao cắt.
- Không wire xuyên thân symbol dạng khối, không chồng thân linh kiện.
- UART chéo, SPI đúng ánh xạ, PWM tách biệt, BEC cả bốn ESC NC.
- 5 V lấy từ LM2596; 3V3 từ DevKit; không lấy từ USB hoặc ESC BEC.
- Không label/power symbol thay wire; không footprint, PCB hoặc linh kiện ngoài danh sách.

**Rev 2.1: ERC đạt 0 lỗi, 0 cảnh báo** sau khi khai báo đúng hai nguồn
bằng PWR_FLAG. KiCad chạy với `--exit-code-violations`, không thêm exclusion
hoặc tắt kiểm tra. Trước thay đổi có hai lỗi `power_pin_not_driven`:

| Mạng | KiCad báo tại | Nguyên nhân của mô hình symbol |
| --- | --- | --- |
| 3V3 | U1.1 VCC | A1.LI02 là `passive`; bộ ổn áp bên trong DevKit không được mô hình hóa bằng `power_out`. |
| LiPo dương | U3.1 BAT+ | BT1.+ là `passive`; cả mạng LiPo dương không có chân `power_out`. |

Đã đối chiếu đường nguồn từ netlist KiCad và từ đồ thị wire trước khi đặt cờ:

- LiPo BT1.+ nối U7.1 IN+ và chân BAT+ của bốn ESC; BT1.- nối IN-/OUT- và GND chung.
- U7.3 OUT+ nối A1.LO03 5V và U2.3 5V. Chân OUT+ đã là `power_out`, không cần thêm cờ 5 V.
- A1.LI02 3V3 nối U1.1 VCC; đây là đầu ra ổn áp bên trong DevKit theo phương án cấp nguồn.

`#FLG01` được đặt bằng wire nhánh trên net **LiPo dương**, không phải chân
VBAT backup của A1. `#FLG02` được đặt bằng wire nhánh trên net **3V3** giữa
A1.LI02 và U1.1. Cả hai sao chép định nghĩa PWR_FLAG chuẩn của KiCad, có
`in_bom no`, `on_board no`, footprint rỗng. Không phải linh kiện vật lý,
không vào BOM hoặc bố trí bo. Sáu symbol module gốc giữ nguyên kiểu điện;
không đặt cờ lên net đang hở hoặc lên 5 V/GND đã có `power_out` của U7.

So sánh netlist trước/sau cho thấy **mọi nhóm chân thiết bị vật lý đều giữ
nguyên**, vẫn 13 module, 26 net, 69 chân dùng, 93 NC; chỉ thêm hai khai báo
ERC cùng wire nhánh. Hai báo cáo trước thay đổi được lưu riêng để đối chiếu.
ERC sạch xác nhận các quy tắc của mô hình schematic; chưa phải đo kiểm nguồn
thực tế. Các mục xác minh điện áp/dòng và đầu nối U7 ở trên vẫn áp dụng.

KiCad CLI phát thông báo không ghi được registry `HKCU\Software\kicad-cli`
trong môi trường hạn chế, nhưng các lệnh đọc/xuất/ERC hoàn tất và tạo tệp.
Cổng kiểm tra tổng hợp PCB của plugin không áp dụng vì dự án không có PCB;
không tạo PCB để vượt qua cổng đó.

Các lệnh có thể chạy lại từ thư mục này (đưa `kicad-cli` vào PATH):

```powershell
kicad-cli sch export netlist --format kicadxml -o validation/Drone_Direct_Wire.net.xml ../hardware/kicad/Drone_Direct_Wire/Drone_Direct_Wire.kicad_sch
kicad-cli sch erc --format json --exit-code-violations -o validation/Drone_Direct_Wire.erc.json ../hardware/kicad/Drone_Direct_Wire/Drone_Direct_Wire.kicad_sch
python verify_design.py
```

PDF/PNG được render và xem lại toàn trang cùng các vùng SPI, UART và nguồn;
các trường chữ được chỉnh để không chồng chân hoặc dây. PNG cuối có kích
thước 7016 x 4961 pixel ở 300 dpi; PDF giữ nét vector khi phóng to/in A2.
