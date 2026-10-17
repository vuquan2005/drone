# Công cụ làm việc

Ở bước chuẩn bị, chỉ cần Git và trình soạn thảo quen dùng. Các công cụ chuyên môn cài khi bắt đầu phần việc tương ứng.

| Khi nào | Công cụ | Dùng để làm gì |
| --- | --- | --- |
| Ngay từ đầu | Git | Lưu lịch sử thay đổi và đồng bộ repository |
| Ngay từ đầu | Trình soạn thảo, ví dụ VS Code | Sửa Markdown, mã nguồn và tệp cấu hình |
| Bắt đầu STM32 | STM32CubeIDE và STM32CubeMX | Phát triển, cấu hình và debug project STM32 trong `firmware/` |
| Bắt đầu mô phỏng | Gazebo | Tạo và chạy mô hình trong `simulation/` |
| Bắt đầu báo cáo | Overleaf hoặc bộ LaTeX trên máy | Viết và biên dịch báo cáo; lưu bản nguồn trong `report/` |

## Cách dùng với repository

- Mở thư mục gốc `drone/` bằng trình soạn thảo để xem toàn bộ đồ án.
- Khi tạo project STM32, chọn vị trí trong `firmware/`; lưu cả mã nguồn và tệp cấu hình `.ioc` nếu sử dụng CubeMX.
- Khi viết báo cáo bằng Overleaf, tải bản nguồn về `report/` trước khi commit các thay đổi cần lưu.
- Lưu script tự viết vào `tools/`. Ghi cách chạy ngay khi thêm script đầu tiên.
- Lưu mã nguồn và cấu hình vào Git; các sản phẩm build tạm được bỏ qua bằng `.gitignore`.

Chưa cần thiết lập hệ thống build chung ở bước chuẩn bị.

## Tài liệu chính thức

- [Cài Git](https://git-scm.com/downloads) và [hướng dẫn Git cơ bản](https://git-scm.com/docs/gittutorial).
- [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html).
- [STM32CubeMX](https://www.st.com/en/development-tools/stm32cubemx.html).
- [Bắt đầu với Gazebo](https://gazebosim.org/docs/latest/getstarted/): chọn hướng dẫn cài đặt phù hợp hệ điều hành trước khi cài.
- [Làm quen LaTeX trên Overleaf](https://www.overleaf.com/learn/latex/Learn_LaTeX_in_30_minutes).
