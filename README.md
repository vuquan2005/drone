# Đồ án Quadrotor sử dụng STM32

Repository chứa firmware, thiết kế phần cứng, tài liệu nghiên cứu và báo cáo
cho đồ án quadrotor. Firmware đã có đọc IMU, nhận RC qua CRSF, điều khiển tốc
độ góc, mixer và arm/failsafe; gain và ánh xạ motor hiện chỉ dành cho thử trên
bộ gá, chưa hiệu chuẩn để bay.

- [Firmware](firmware/README.md): hướng dẫn bắt đầu, build và nạp.
- [Firmware drone](firmware/drone/README.md): chế độ vận hành và giao tiếp phần cứng.
- [Debug và thử trên board](firmware/drone/DEBUG.md) · [Công cụ RTT](tools/rtt/README.md).
- [Phần cứng](hardware/kicad/Quadrotor_Wiring.kicad_pro) · [Tài liệu nghiên cứu](docs/README.md) · [Công cụ](tools/README.md).
- `report/`: báo cáo LaTeX
- `experiments/`: cấu hình, dữ liệu và kết quả đo.

Thuật toán trong `firmware/drone/Control/` độc lập HAL và do `App/` điều phối.
Kiểm chứng bằng kiểm thử thuật toán nhỏ trên máy tính và đo trên board/bộ gá;
kế hoạch hiện tại không triển khai mô phỏng vòng kín hoặc SITL. Xem
[kiến trúc firmware](firmware/drone/ARCHITECTURE.md) và
[quy trình làm việc nhóm](CONTRIBUTING.md).
