# Ghi chú nghiên cứu

Các tài liệu dưới đây là ghi chú khảo sát và tổng hợp để tham khảo khi làm đồ án.

Quyết định áp dụng cho mã hiện tại được ghi riêng trong
[kiến trúc firmware drone](../../firmware/drone/ARCHITECTURE.md), gồm trạng thái
đã triển khai và hướng mở rộng chuỗi điều khiển. Các tài liệu khảo sát bên dưới
không phải mô tả tính năng đã có trong firmware.

| Tài liệu                                                                                         | Nội dung                                                                           |
| ------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------- |
| [Quy trình và kiến trúc PX4, ArduPilot](PX4_ARDUPILOT_ENGINEERING.md)                            | Đối chiếu nguồn cục bộ, quy ước module, lịch chạy và kiểm chứng; đề xuất cho đồ án |
| [Tổng quan kiến trúc điều khiển](control/00_TONG_QUAN_KIEN_TRUC_DIEU_KHIEN_QUADROTOR.md)         | So sánh các dự án tham khảo                                                        |
| [Các vòng điều khiển và input/output](control/01_CHI_TIET_CAC_VONG_DIEU_KHIEN_INPUT_OUTPUT.md)   | Vai trò và giao diện từng vòng                                                     |
| [Thuật toán mixer và giải bão hòa](control/02_THUAT_TOAN_MIXER_VA_GIAI_BAO_HOA.md)               | Các cách phân bổ lệnh motor                                                        |
| [Khuyến nghị thiết kế bộ điều khiển](control/03_KHUYEN_NGHI_THIET_KE_BO_DIEU_KHIEN_QUADROTOR.md) | Gợi ý từ khảo sát                                                                  |
| [Kiến trúc điều khiển quadrotor](control/quadrotor_control_architecture.md)                      | Tài liệu diễn giải bổ sung                                                         |
| [Nghiên cứu mixer từ các dự án](MIXER_NGHIEN_CUU_TU_CAC_DU_AN.md)                                | Khảo sát mixer và dẫn chiếu mã nguồn                                               |
| [Hai cách chuyển lực sang PWM/DShot](HAI_CACH_CHUYEN_LUC_SANG_PWM_DSHOT.md)                      | Giao diện SI và chuẩn hóa, mô hình nghịch lực–lệnh, đối chiếu mã nguồn và ví dụ số |

Các liên kết đến mã nguồn trong ghi chú sử dụng bản sao cục bộ tại `../external/repos/`. Xem [hướng dẫn repo tham khảo](../external/README.md).
