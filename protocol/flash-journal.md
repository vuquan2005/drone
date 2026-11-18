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

Mỗi lần mở cần còn đủ ba trang cho BEGIN, ít nhất một DATA và END.
Journal luôn dành một trang cho END. Một trang DATA được đóng khi đầy,
khi frame kế tiếp không vừa, khi đã chờ 100 ms hoặc khi đóng phiên. Bên
gọi giữ frame chưa được nhận để thử lại khi trạng thái trở về OPEN.
Khi SPI, timeout hoặc xác minh flash lỗi, journal báo FAULT đến lần
khởi động lại. Các trang trước đó vẫn có thể phục hồi bằng quét.
