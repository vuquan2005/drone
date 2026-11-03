# Làm việc với phần cứng

- Mỗi dự án KiCad đặt trong `kicad/<TênDựÁn>/`; `.kicad_pro`, sơ đồ gốc,
  các sheet con và PCB (khi có) nằm trong cùng cây dự án.
- Lưu symbol riêng trong `symbols/`, footprint trong `footprints.pretty/`,
  mô hình trong `3dmodels/`. Đăng ký bằng `sym-lib-table` và `fp-lib-table`,
  dùng `${KIPRJMOD}` thay đường dẫn tuyệt đối. Không đổi library ID khi di chuyển file.
- Theo dõi nguồn thiết kế và thư viện trong Git. Đặt bản xuất mới ở `gerber/`
  hoặc `exports/`; không xóa dữ liệu kiểm tra hay bản xuất cũ chỉ vì có ignore.
- Không xóa lock của phiên KiCad đang mở. Không sửa số chân, kiểu điện,
  ánh xạ net hoặc kích thước footprint như một phần dọn thư mục.
- Trước khi chạy script sinh sơ đồ, đọc phạm vi ghi đè trong README phần cứng.
  Giữ nguyên bản chỉnh sửa thủ công nếu chưa được yêu cầu dựng lại.
- Sau khi di chuyển, kiểm tra đường dẫn thư viện và tính toàn vẹn nguồn;
  chạy ERC khi có KiCad CLI. Thay đổi PCB cần kiểm tra DRC theo phạm vi sửa.
  Báo riêng kết quả phần mềm và phần chưa đo kiểm trên phần cứng.
