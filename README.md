# Đồ án Quadrotor sử dụng STM32

Repository quản lý mã nguồn, mô phỏng, phần cứng, tài liệu và báo cáo của đồ án. Dự án đang ở bước chuẩn bị; hiện chỉ thiết lập thư mục và hướng dẫn làm việc.

## Thư mục

```text
drone/
├── control/       # Thuật toán điều khiển và ước lượng dùng chung
├── firmware/      # Project STM32, cấu hình board và driver
├── simulation/    # Mô hình và môi trường mô phỏng
├── hardware/      # Sơ đồ điện, PCB, CAD và thông số linh kiện
├── experiments/   # Cấu hình, dữ liệu và kết quả thí nghiệm
├── report/        # Báo cáo LaTeX, hình và bảng biểu
├── docs/          # Hướng dẫn, ghi chú và tài liệu tham khảo
└── tools/         # Script hỗ trợ khi cần
```

Các thư mục mới có `.gitkeep` để được lưu trong Git; có thể bỏ tệp này khi thư mục đã có nội dung. Thư mục `src/` rỗng có sẵn không thuộc cấu trúc mới; mã mới được đặt theo chức năng ở trên.

Tài liệu được chia theo mục đích; xem [mục lục tài liệu](docs/README.md):

| Đường dẫn          | Nội dung                                                    |
| ------------------ | ----------------------------------------------------------- |
| `docs/guides/`     | Hướng dẫn làm việc và Git                                   |
| `docs/research/`   | Ghi chú, phân tích và tổng hợp nghiên cứu                   |
| `docs/references/` | Bài báo, bibliography và tài liệu tham khảo gốc             |
| `docs/external/`   | Hướng dẫn lưu repo tham khảo; bản sao cục bộ trong `repos/` |

## Bắt đầu

Đặt tài liệu hoặc mã mới vào thư mục tương ứng; tạo thư mục con khi có nội dung thực tế. Xem [hướng dẫn công cụ](tools/README.md) để cài đặt và chạy các lệnh hỗ trợ, hoặc [hướng dẫn Git](docs/guides/git.md) để làm việc với repository.

Báo cáo: xem [hướng dẫn bộ khung LaTeX](report/README.md). Tệp chính là `report/main.tex`; thông tin bìa được điền tại `report/metadata.tex`.

Quy ước ban đầu: `control/` chứa thuật toán độc lập; phần gọi STM32 HAL nằm trong `firmware/`, phần kết nối trình mô phỏng nằm trong `simulation/`.
