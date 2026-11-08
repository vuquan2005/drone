# Đồ án Quadrotor sử dụng STM32

Repository chứa firmware, thiết kế phần cứng, tài liệu nghiên cứu và báo cáo cho đồ án quadrotor sử dụng STM32. Firmware hiện ở giai đoạn khởi tạo ngoại vi, chưa có chức năng điều khiển bay.

```text
drone/
├── firmware/      # Project STM32 và hướng dẫn build, debug
├── hardware/      # Thiết kế phần cứng
├── experiments/   # Cấu hình, dữ liệu và kết quả thí nghiệm (dự kiến)
├── report/        # Báo cáo LaTeX
├── docs/          # Nghiên cứu và tài liệu tham khảo
└── tools/         # Công cụ hỗ trợ
```

Các thư mục đang trống có thể chưa xuất hiện sau khi clone. Xem hướng dẫn tại [firmware](firmware/README.md), [báo cáo](report/AGENTS.md), [tài liệu](docs/README.md) và [công cụ](tools/README.md).

Thuật toán lọc, ước lượng và điều khiển dự kiến đặt trong
`firmware/drone/Control/`, dùng API độc lập HAL và do `App/` điều phối.
Kiểm chứng bằng kiểm thử thuật toán nhỏ trên máy tính và đo trên board/bộ gá;
kế hoạch hiện tại không triển khai mô phỏng vòng kín hoặc SITL.
Xem [kiến trúc firmware](firmware/drone/ARCHITECTURE.md).

Xem [quy trình làm việc nhóm](CONTRIBUTING.md) để nhận nhiệm vụ, mở Pull Request và review.

Mở [project KiCad](hardware/kicad/Drone_Direct_Wire.kicad_pro) để chỉnh sửa sơ đồ nối dây quadrotor. Thư viện symbol nằm tại `hardware/kicad/symbols/`; xem [quy ước phần cứng](hardware/AGENTS.md) khi chỉnh sửa.
