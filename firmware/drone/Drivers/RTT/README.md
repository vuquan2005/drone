# SEGGER RTT

Mã nguồn upstream giữ nguyên từ
[SEGGERMicro/RTT](https://github.com/SEGGERMicro/RTT/tree/9106eaa39babe53e00994d7828c1afb29390cdc1),
commit `9106eaa39babe53e00994d7828c1afb29390cdc1`:

- `RTT/SEGGER_RTT.c`
- `RTT/SEGGER_RTT.h`
- `RTT/SEGGER_RTT_ConfDefaults.h`
- `LICENSE.md` (MIT)

Cấu hình riêng đặt tại `BSP/Inc/SEGGER_RTT_Conf.h`: bản C, hai up-channel
(channel 0: log 2048 byte; channel 1: data 4096 byte), một down-channel
16 byte, chế độ không chờ `NO_BLOCK_SKIP`. Dữ liệu gửi xuống không được xử lý.
Wrapper `bsp_rtt` dùng một writer foreground, không tắt ngắt trong lúc copy.
Không dùng syscall retarget/printf RTT; App tự chọn định dạng và thời điểm ghi.

Xem [hướng dẫn debug](../../DEBUG.md) để nạp firmware và đọc RTT bằng OpenOCD.
