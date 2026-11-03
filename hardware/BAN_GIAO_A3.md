# Bàn giao sơ đồ drone A3

Mở `Drone_Direct_Wire.kicad_pro` bằng KiCad 10, sau đó mở schematic cùng tên.

Bản này được chỉnh từ bản Drone_Direct_Wire A3 đã bàn giao trước khi xóa, hiện còn bản nguồn trong `hardware/kicad/`. Không dùng bố cục của bản TNR20.

- Giữ nguyên tiêu đề `DRONE QUADROTOR - MODULE WIRING`, toàn bộ nội dung khung tên, khung trang chuẩn và tên/vị trí/cỡ chữ ba tiêu đề khối như bản cũ.
- Bỏ các chữ giải thích màu xanh; giữ tên ba khối và tên linh kiện. Tên linh kiện dùng Times New Roman, tự chọn cỡ 1,8–2,2 mm và reference 1,5 mm để vừa bố cục. Không ép cỡ 20 pt.
- Reference và Value gốc vẫn giữ trong dữ liệu symbol. Ẩn Value chung khi đã có tên module dễ đọc để tránh lặp chữ.
- Không đổi vị trí symbol, dây, nhãn mạng điện, junction, dấu No Connect hoặc PWR_FLAG. Giữ nguyên thư viện symbol nhúng, reference, số/tên/kiểu điện của chân và UUID.
- Đối chiếu netlist: 119 nhóm chân trước/sau giống nhau, gồm cả chân không dùng. Giữ 93 dấu No Connect và hai PWR_FLAG.
- ERC: 0 lỗi, 0 cảnh báo theo cấu hình hiện có. Không thêm No Connect/PWR_FLAG để che lỗi.
- Đã xem bản render toàn trang. PDF một trang A3 ngang; PNG 4961 × 3508 pixel, xuất 300 dpi.
- Chưa thực hiện Highlight Net trong giao diện vì không có kết nối IPC; chưa đo kiểm phần cứng. Bỏ ghi chú khỏi bản vẽ theo yêu cầu trình bày không có nghĩa module/chân vật lý/footprint đã được xác minh.

Trong repository, bảng thư viện `hardware/sym-lib-table` trỏ tới thư viện duy nhất tại `kicad/symbols/DA_Drone.kicad_sym`. Bộ ZIP đã kèm thư viện và bảng thư viện trỏ tới `symbols/` để mở độc lập trên máy khác. Giữ toàn bộ cấu trúc khi giải nén. Máy cần cài Times New Roman để chỉnh sửa tên linh kiện đúng font; PDF đã nhúng font này.

ZIP kèm netlist trước/sau, báo cáo ERC và kết quả đối chiếu trong `validation/`.
