# Kiểm tra flash SPI2 trên board

Đối chiếu PB12 → /CS, PB13 → CLK, PB14 ← DO, PB15 → DI. Kiểm tra nguồn
3,3 V, GND chung và mức kéo lên của /CS, /WP, /HOLD. Dùng board trên bộ gá.

Từ thư mục `firmware/drone`, build bản kiểm tra với thư mục riêng để giữ
preset Debug thông thường.

```sh
cube cmake --preset Debug -B /tmp/drone-flash-smoke -DDRONE_FLASH_BOARD_SMOKE=ON
cube cmake --build /tmp/drone-flash-smoke
```

Nạp ELF của bản kiểm tra qua SWD và mở chính ELF đó trong GDB. Sau reset,
firmware đọc JEDEC ID và chờ lệnh qua debugger. PWM chưa được khởi chạy.

1. Cho firmware chạy rồi tạm dừng. `p/x flash_smoke_id` phải trả về
   `0xef4018` và `p flash_smoke_result` phải là `BSP_FLASH_OK`.
2. Chọn trước một trang dành riêng để thử, có địa chỉ căn 256 byte trong vùng
   16 MiB. Khi CPU đang dừng, đặt `flash_smoke_address` bằng địa chỉ đó và
   `flash_smoke_command = FLASH_SMOKE_READ` bằng lệnh `set variable` của GDB.
   Cho chạy rồi tạm dừng. Kiểm tra `flash_smoke_result` và dùng
   `x/256bx &flash_smoke_data` để xác nhận cả trang là `0xff`.
3. Khi CPU đang dừng, đặt `flash_smoke_command = FLASH_SMOKE_PROGRAM`. Cho
   chạy rồi tạm dừng. `BSP_FLASH_OK` xác nhận firmware đã đối chiếu dữ liệu
   đọc lại với mẫu ghi. Nếu có byte đã ghi, firmware trả
   `BSP_FLASH_NOT_ERASED` trước khi gửi lệnh program.
4. Gửi lại `FLASH_SMOKE_READ` và kiểm tra mẫu từng byte theo
   `byte[i] = i XOR 0xa5`. Chạy liên tục giữa các lần kiểm tra để DMA và
   deadline hoạt động bình thường.

Bước 3 chiếm một trang cho đến khi sector chứa trang đó được xóa.
