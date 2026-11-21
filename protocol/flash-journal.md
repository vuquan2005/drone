# Journal telemetry SPI NOR v1

W25Q128FV có 65.536 trang, mỗi trang 256 byte. Chỉ số trang bắt đầu từ 0
và địa chỉ byte bằng chỉ số trang nhân 256. Journal ghi nối tiếp trên vùng
đã xóa. Phần dư ngoài payload giữ giá trị `0xFF`.

| Offset | Số byte | Trường         | Giá trị                                         |
| ------ | ------- | -------------- | ----------------------------------------------- |
| 0      | 4       | Magic          | ASCII `DRJ!`                                    |
| 4      | 1       | Version        | `1`                                             |
| 5      | 1       | Loại trang     | `1` BEGIN, `2` DATA, `3` END                    |
| 6      | 2       | Độ dài payload | Tối đa 232 byte                                 |
| 8      | 4       | Chỉ số trang   | Phải khớp vị trí vật lý                         |
| 12     | 4       | ID phiên       | Chỉ số trang BEGIN                              |
| 16     | 4       | Uptime         | ms modulo 2³²                                   |
| 20     | 4       | CRC            | CRC-32/ISO-HDLC của 20 byte đầu nối với payload |
| 24     | 0–232   | Payload        | Theo loại trang                                 |

Số nhiều byte dùng little-endian. BEGIN có payload rỗng và timestamp lúc
yêu cầu mở. DATA chứa một hoặc nhiều [frame DRT!](telemetry.md) nguyên
vẹn, với timestamp journal lấy lúc nhận frame đầu tiên trong trang. Từng
frame vẫn mang timestamp và CRC gốc. Khi giải mã DATA, kiểm tra CRC của
từng frame theo giao thức DRT!. END có ba trường `uint32` theo thứ tự:
lý do (`1` dừng theo yêu cầu, `2` hết chỗ cho DATA), số frame đã ghi
và số frame bỏ mà bên gọi đã báo. Timestamp END là lúc yêu cầu dừng hoặc
lúc journal xác nhận đã ghi trang DATA cuối cùng khi đầy.

Khi khởi động, journal quét tất cả các trang. Mọi trang có byte khác
`0xFF` đều chiếm chỗ, kể cả trang hỏng hoặc dữ liệu cũ. Vị trí ghi mới
nằm sau trang bị chiếm cuối cùng. DATA và END chỉ thuộc phiên nếu có
BEGIN hợp lệ mở phiên tương ứng. Phiên thiếu END hợp lệ được xem là
gián đoạn. Khi bỏ một trang hỏng giữa phiên, vẫn giữ các trang hợp lệ
phía sau có cùng ID phiên. Sau reset, lần ghi tiếp theo tạo phiên mới.

Mỗi lần mở cần còn ít nhất bốn trang cho BEGIN, một DATA chứa cấu hình,
một DATA chứa telemetry và END. Journal luôn dành một trang cho END. Một trang DATA được đóng khi đầy,
khi frame kế tiếp không vừa, khi đã chờ 100 ms hoặc khi đóng phiên. Bên
gọi giữ frame chưa được nhận để thử lại khi trạng thái trở về OPEN.
Khi SPI, timeout hoặc xác minh flash lỗi, journal báo FAULT đến lần
khởi động lại. Các trang trước đó vẫn có thể phục hồi bằng quét.

## Ghi trong chế độ bay

`DRONE_ENABLE_FLASH_RECORD` mặc định bật cho firmware và hoạt động cả khi
`DRONE_ENABLE_RTT` tắt. Trong `RUNTIME_FLIGHT`, SD MIDDLE mở phiên sau
khi quét và xác minh BEGIN. Rời RECORD thì ngừng nhận frame mới, xả FIFO
RAM rồi ghi END. Khi mất RC, logger giữ yêu cầu ghi hợp lệ gần nhất.

Đầu mỗi phiên, logger lưu frame cấu hình type 10 với ID yêu cầu bằng 0,
rồi ghi IMU raw gộp, RC gọn, rate, attitude và attitude control. IMU raw
type 9 giữ tất cả mẫu đọc thành công ở ODR 800 Hz, tối đa tám mẫu mỗi
frame. Nhóm đang dở được tách theo ranh giới phiên để giữ mẫu đầu và cuối
đúng phiên. RTT live tiếp tục dùng lịch phát hiện có và thống kê RTT vẫn
đi riêng qua RTT.

Bộ đếm trong END tính theo frame. Số frame đã ghi bao gồm frame cấu hình.
Số frame bỏ gồm các frame không vừa FIFO và các frame còn trong FIFO hoặc
batch IMU khi hết chỗ ghi DATA. Có thể đối chiếu sequence trong type 9 để
đếm số mẫu IMU bị mất giữa các nhóm đã lưu.

## Xuất ảnh flash qua RTT v1

Với firmware preset `Export`, host gửi ASCII `EXPORT` nối với đúng 8 chữ
số hex thường của ID yêu cầu trên RTT down-channel 0. Ví dụ
`EXPORT0000002a`. Firmware trả các bản tin dưới đây trên RTT DATA.
Mỗi lần yêu cầu mới bắt đầu đọc lại từ trang 0. Bản tin còn tồn trong
buffer của yêu cầu trước được nhận diện bằng ID.

| Offset | Số byte | Trường                                |
| ------ | ------- | ------------------------------------- |
| 0      | 4       | Magic ASCII `DRE!`                    |
| 4      | 1       | Version `1`                           |
| 5      | 1       | Loại bản tin                          |
| 6      | 2       | Độ dài payload                        |
| 8      | 4       | ID yêu cầu                            |
| 12     | 4       | Chỉ số trang hoặc mốc kết thúc        |
| 16     | N       | Payload                               |
| 16+N   | 4       | CRC-32/ISO-HDLC của header và payload |

Số nhiều byte dùng little-endian. Payload theo loại bản tin:

- `1 BEGIN` có chỉ số 0 và hai `uint32` lần lượt là kích thước trang và
  tổng số trang.
- `2 PAGE` chứa 256 byte nguyên bản tại chỉ số trang. Trang toàn
  `0xFF` được lược khỏi luồng. Các trang không trống, kể cả trang có dữ
  liệu cũ hoặc CRC journal sai, được giữ nguyên và gửi theo thứ tự tăng.
- `3 END` dùng tổng số trang đã quét làm chỉ số. Ba `uint32` lần lượt
  là số trang đã gửi, số trang đã gửi có envelope journal không hợp lệ
  và CRC-32/ISO-HDLC của luồng trang. Đầu vào CRC là chuỗi nối
  `uint32 page_index` với 256 byte dữ liệu cho từng trang đã gửi.
- `4 ERROR` báo chỉ số trang đang xử lý và một `uint32` mã lỗi BSP.
  Sau lỗi, có thể gửi yêu cầu mới. Lỗi BSP bị khóa cần reset board.
- `5 ERASE_STATUS` dùng tổng số trang làm chỉ số và chứa một `uint32`
  trạng thái xóa. Giá trị `0` báo đã nhận yêu cầu, `1` báo đã xác minh
  toàn chip là `0xFF`, và `2` báo lỗi xóa hoặc xác minh.

Host chỉ xác nhận bản xuất khi nhận `END` đúng ID, đúng số trang và CRC.
Các khoảng chỉ số bị lược được điền lại bằng `0xFF` để tái tạo ảnh toàn
chip. Trong ảnh, CSV được trích từ các trang hợp lệ có `BEGIN` tương ứng.

Sau khi lưu và kiểm tra ảnh trên đĩa, host có thể gửi ASCII `ERASE!` nối
với đúng 8 chữ số hex thường của ID yêu cầu đã dùng trong `END`. Ví dụ
`ERASE!0000002a`. Firmware chỉ nhận lệnh khi lượt xuất tương ứng vừa hoàn
tất trong lần boot hiện tại. Các lệnh đến trong lúc xóa bị bỏ qua. Board
trả `ERASE_STATUS` với cùng ID khi nhận lệnh và sau khi xóa, đọc xác minh
toàn bộ chip. Sau khi board nhận lệnh, quá trình vẫn tiếp tục nếu RTT ngắt.
