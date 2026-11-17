# BMI160 Sensor API

Bản sao nguyên gốc từ https://github.com/boschsensortec/BMI160_SensorAPI,
commit `252ac2859ac1d2010915b7a7cfcbf501d7adfb40`, giấy phép BSD-3-Clause.
Nguồn tham khảo local: `docs/external/repo/BMI160_SensorAPI`.

Build dùng `bmi160.c`, `bmi160.h`, `bmi160_defs.h`; không sửa mã upstream.
BSP dùng SPI1/CS PA4, accel và gyro normal mode, 400 Hz, ±16 g, ±2000 °/s.
Callback Bosch không có context pointer; mọi init/read BSP phải được tuần tự
hóa trong foreground, kể cả khi có nhiều instance. Không gọi API Bosch trực tiếp.

API BSP công khai: `bsp_imu.h`; App và board dùng tên IMU chung. Driver
BMI270 vẫn được giữ tại `Drivers/BMI270`. Chọn backend trong `cmake/imu.cmake`;
chỉ backend được chọn tham gia build firmware.
