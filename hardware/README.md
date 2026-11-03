# Phần cứng quadrotor

Nguồn KiCad chỉnh sửa nằm trong `kicad/`, mỗi dự án có thư mục riêng:

```text
hardware/kicad/
├── Drone_Direct_Wire/
│   ├── Drone_Direct_Wire.kicad_pro
│   ├── Drone_Direct_Wire.kicad_sch
│   ├── sym-lib-table
│   ├── symbols/
│   └── README.md
├── symbols/               # Sáu thư viện đầu vào cho script
├── sym-lib-table
├── build_quadrotor.py
├── *_SYMBOL.md            # Quy ước chân module
└── *.csv                  # Hai bảng chân STM32
```

Mở [dự án chính](kicad/Drone_Direct_Wire/Drone_Direct_Wire.kicad_pro)
bằng KiCad. Xem [hướng dẫn dự án](kicad/Drone_Direct_Wire/README.md).
Sơ đồ này mô tả nối dây module và chưa có PCB.

Khi có PCB, đặt `<TênDựÁn>.kicad_pcb` cạnh `.kicad_pro` và `.kicad_sch`.
Chỉ thêm `footprints.pretty/`, `fp-lib-table` và `3dmodels/` khi có tài nguyên thực.
Footprint dùng `.kicad_mod`, mô hình dùng STEP/WRL và thư viện dùng `${KIPRJMOD}`.
Xuất dữ liệu sản xuất vào `gerber/`, bản xem vào `exports/` trong từng dự án;
hai thư mục này được Git bỏ qua và VS Code ẩn.

Các bản xuất PDF/PNG, BOM và bằng chứng kiểm tra đã có vẫn ở
[`output/`](../output/README.md). Các file đã được Git theo dõi tại đó vẫn được
bảo toàn; ignore không tự bỏ theo dõi chúng. Bản perfboard cục bộ và script
`tools/build_perfboard.py` chưa được theo dõi được giữ nguyên, chưa chuyển cấu trúc.

`python hardware/kicad/build_quadrotor.py` dựng lại dự án chính và manifest
trong `output/validation/`. Lệnh này **ghi đè bản chỉnh sửa thủ công** của sơ đồ,
cấu hình dự án, thư viện riêng và bảng thư viện; chỉ chạy khi chủ ý dựng lại.
