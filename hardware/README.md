# Phần cứng quadrotor

`kicad/` chứa sáu thư viện symbol module (`*.kicad_sym`), bảng đăng ký
`sym-lib-table`, tài liệu quy ước chân (`*_SYMBOL.md`) và hai bảng CSV chân STM32.

Sơ đồ hiện hành và các bản xuất PDF/PNG nằm trong [`output/`](../output/README.md).
Mở `output/Drone_Direct_Wire.kicad_pro` bằng KiCad để chỉnh sửa sơ đồ.

Script `kicad/build_quadrotor.py` dùng thư viện nguồn tại đây để tạo lại sơ đồ
trong `output/`. Chạy script sẽ ghi đè sơ đồ, cấu hình dự án, bảng thư viện
và manifest tại đó; chỉ chạy khi muốn dựng lại từ mã nguồn.
