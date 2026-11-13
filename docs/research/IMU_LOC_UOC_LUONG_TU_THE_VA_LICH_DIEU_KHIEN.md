# Lấy mẫu, lọc IMU và lịch điều khiển quadrotor

Ghi chú kỹ thuật cho STM32F103VET6 và BMI270, đối chiếu ngày 14/11/2026.
Các tần số đề xuất là cấu hình để thử nghiệm trên board, chưa phải thông số
đã được xác nhận bằng đo đạc. Trọng tâm là lựa chọn ODR, bộ lọc và nhịp xử lý
IMU so với các vòng điều khiển. Phần ước lượng tư thế chỉ giới thiệu lựa chọn
thuật toán và quan hệ với lịch chạy, chưa đi vào thiết kế hoặc triển khai
bộ ước lượng đầy đủ.

## 1. Hướng thiết kế

Giữ `float` cho đổi đơn vị, lọc, ước lượng tư thế và PID. Tách nhịp mẫu IMU,
nhịp xử lý bộ lọc, nhịp điều khiển tốc độ góc (rate) và nhịp điều khiển tư thế
(attitude). Không cần cho mọi tầng chạy cùng tần số.

Hai cấu hình có ý nghĩa để so sánh:

| Công việc                         | Phương án cơ sở | Phương án lấy mẫu nhanh hơn                  |
| --------------------------------- | --------------- | -------------------------------------------- |
| Mẫu gyro và lọc trước giảm tần số | 400 Hz          | 800 Hz                                       |
| PID rate và tính lệnh mixer       | 400 Hz          | 400 Hz                                       |
| Cập nhật quaternion               | 400 Hz          | 400 Hz, sử dụng thông tin từ cả hai mẫu gyro |
| Điều khiển attitude               | 100 Hz          | 100 Hz                                       |

Phương án cơ sở giúp kiểm tra timing với ít tầng xử lý. Phương án 800/400 Hz
cho phép lọc ở nhịp cao hơn trước khi giảm tần số, nhưng chỉ đáng giữ khi có
lợi ích đo được và còn đủ thời gian CPU. Nhịp phát lệnh thực tế tới ESC phụ
thuộc giao thức, cấu hình timer và khả năng ESC, không tự bằng nhịp tính mixer.

## 2. Hiện trạng đường đọc IMU

[app_imu.c](../../firmware/drone/App/Src/app_imu.c) hiện thực hiện:

1. Xóa `sample_valid` trước mỗi lần poll và kiểm tra trạng thái khởi tạo.
2. Gọi BSP kiểm tra cả cờ dữ liệu mới của accel và gyro, rồi đọc burst bằng
   SPI blocking khi cả hai cùng sẵn sàng.
3. Lấy `t_read_us` sau khi BSP đọc thành công, trước bước đổi đơn vị.
4. Đổi ba trục accel sang m/s² và ba trục gyro sang rad/s.
5. Lưu thời gian cảm biến, tăng `sample_count` và đặt `sample_valid = true`.

Khi chưa có mẫu hoặc đọc lỗi, mẫu trước được giữ nguyên nhưng `sample_valid`
bằng `false`. Cờ này chỉ nói lần poll vừa rồi có mẫu mới, không bảo đảm mẫu
cũ còn đủ mới để điều khiển. Bộ đếm lỗi đọc chỉ tăng với `BSP_IMU_DRIVER_ERROR`.

[BSP BMI270](../../firmware/drone/BSP/Src/bsp_bmi270.c) cấu hình cả accel và
gyro ở 400 Hz, tương ứng chu kỳ danh định 2,5 ms; range ±16 g và ±2000 °/s.
Bộ lọc nội cảm biến đã được cấu hình, chế độ tiết kiệm điện nâng cao bị tắt.
Đường đọc hiện tại chưa dùng FIFO hoặc DMA. Timeout SPI trong
[cấu hình board](../../firmware/drone/BSP/Src/bsp_board.c) là 100 ms, có thể
làm đường xử lý bị chặn lâu hơn nhiều so với ngân sách 2,5 ms khi giao tiếp lỗi.

Mẫu ứng dụng vẫn ở hệ trục XYZ của vỏ cảm biến, chưa có phép xoay sang hệ trục
thân drone hoặc hiệu chuẩn bias trong App. Driver Bosch có bước bù độ nhạy
chéo gyro. `t_read_us` là thời điểm đọc xong, không phải thời điểm lấy mẫu;
`sensor_time_ticks` là đồng hồ cảm biến 24 bit, không phải uptime MCU và
không nên mặc định xem là timestamp chính xác của từng trục đo.

[app_process()](../../firmware/drone/App/Src/app.c) poll receiver, poll IMU và
gửi telemetry khi đọc thành công. Đường này chưa chứa PID rate hoặc bộ ước
lượng tư thế. Chờ accel cùng sẵn sàng có thể giới hạn nhịp gyro khi sau này
cấu hình hai cảm biến khác tần số.

## 3. Float và dấu phẩy tĩnh

### 3.1. Các phép toán còn chạy trên MCU

Hệ số đổi đơn vị hiện tại là:

```c
accel_scale = 16.0f * 9.80665f / 32768.0f;
gyro_scale = 2000.0f * (pi_f / 180.0f) / 32768.0f;
```

Biên dịch riêng `app_imu.c` bằng compiler và flags trong `compile_commands.json`
của hai cấu hình hiện có cho thấy cả Debug và Release đều tính sẵn hai hệ số.
Các phép chia trong công thức không chạy lại mỗi mẫu. Phần đổi đơn vị còn
sáu lần đổi số nguyên sang float (`__aeabi_i2f`) và sáu phép nhân float
(`__aeabi_fmul`) trên mỗi mẫu thành công. Các hằng có hậu tố `f`, không đưa
phép tính `double` vào phần đổi đơn vị này.

[Toolchain](../../firmware/drone/cmake/gcc-arm-none-eabi.cmake) dùng
`-mcpu=cortex-m3`, Debug `-O0 -g3`, Release `-Os -g0`. `-Os` tối ưu kích thước
mã, không phải chế độ tối ưu tốc độ tối đa [1]. Cortex-M3 của F103 không có
FPU; float vẫn được thực hiện bằng phần mềm. Tính sẵn biểu thức hằng vẫn có
thể xảy ra ở `-O0`, như kết quả kiểm tra trên đã chỉ ra.

Không có số đo thời gian thực thi trên board cho phần chuyển đổi, bộ lọc hoặc
estimator. Việc chọn fixed-point chỉ nên dựa trên điểm nghẽn đã đo. Fixed-point
cần thiết kế thang đo, độ rộng tích lũy, làm tròn và chống tràn; không đơn thuần
là thay `float` bằng `int32_t`. Cũng không nên bật `-ffast-math` chỉ để kỳ vọng
float nhanh hơn mà chưa đánh giá các thay đổi về ngữ nghĩa số học [1].

### 3.2. Đối chiếu các firmware tham khảo

| Dự án      | Kiểu số trong đường xử lý đã đối chiếu                      | Nguồn cục bộ                                                                                                                |
| ---------- | ----------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| Betaflight | Gyro, bộ lọc và PID dùng `float`                            | [gyro.c](../external/repos/betaflight/src/main/sensors/gyro.c), [pid.c](../external/repos/betaflight/src/main/flight/pid.c) |
| INAV       | `gyroADCf`, bộ lọc và `pidController(float dT)`             | [gyro.c](../external/repos/inav/src/main/sensors/gyro.c), [pid.c](../external/repos/inav/src/main/flight/pid.c)             |
| PX4        | `float`, `Vector3f` cho rate PID                            | [rate_control.cpp](../external/repos/PX4-Autopilot/src/lib/rate_control/rate_control.cpp)                                   |
| ArduPilot  | `Vector3f gyro_rads`, `float dt` cho rate controller        | [AC_AttitudeControl_Multi.cpp](../external/repos/ardupilot/libraries/AC_AttitudeControl/AC_AttitudeControl_Multi.cpp)       |
| Crazyflie  | PID nhận float; đầu ra được giới hạn rồi đổi sang `int16_t` | [attitude_pid_controller.c](../external/repos/crazyflie-firmware/src/modules/src/controller/attitude_pid_controller.c)      |

Raw IMU là `int16_t` hoặc đầu ra motor là số nguyên không có nghĩa thuật toán
dùng dấu phẩy tĩnh. Bảng chỉ mô tả đường tính toán đã đối chiếu, không khẳng
định mọi module của các firmware đều dùng cùng kiểu số. Hiệu năng trên các
board của chúng cũng không thay thế phép đo trên F103.

## 4. Lấy mẫu nhanh hơn PID đem lại điều gì?

### 4.1. ODR, tần số đọc và băng thông là ba đại lượng khác nhau

ODR là tần số xuất mẫu của cảm biến. Tần số đọc là số lần MCU thực hiện giao
dịch. Băng thông bộ lọc quyết định những thành phần tín hiệu được giữ lại.
Cảm biến tiếp tục xử lý khi MCU chưa đọc; đọc chậm hơn không tự làm cảm biến
lọc mạnh hơn hoặc tự lấy trung bình các mẫu bị bỏ qua.

| Cách cấu hình                                     | Hệ quả                                                             |
| ------------------------------------------------- | ------------------------------------------------------------------ |
| ODR 400 Hz, đọc theo mẫu mới, PID 400 Hz          | Có thể cấp một mẫu mới cho mỗi bước PID                            |
| ODR 800 Hz, chỉ đọc thanh ghi ở 400 Hz            | Có thể bỏ mẫu; không bảo đảm mỗi lần đọc là trung bình của hai mẫu |
| ODR 800 Hz, xử lý đủ mẫu, lọc rồi giảm còn 400 Hz | Chủ động kiểm soát tín hiệu trước khi cấp cho PID                  |
| ODR 400 Hz, gọi poll ở 1 kHz                      | Không tạo thêm mẫu; chỉ có thể phát hiện mẫu sớm hơn               |

Với luồng 400 Hz, tần số Nyquist là 200 Hz. Ví dụ lý tưởng, rung 300 Hz được
lấy mẫu trực tiếp ở 400 Hz có thể xuất hiện như thành phần 100 Hz. Sau khi
aliasing xảy ra, bộ lọc phía sau không còn phân biệt được thành phần này với
chuyển động thật ở 100 Hz. Vì vậy cần suy giảm đủ thành phần ngoài băng trước
mỗi bước giảm tần số. Tần số cắt thấp hơn Nyquist chưa tự bảo đảm suy giảm đủ;
còn phải xét bậc lọc và biên độ nhiễu.

### 4.2. Bộ lọc nội cảm biến và bộ lọc phần mềm

Bộ lọc nội IMU không mặc định kém hơn. Nó có thể giảm tải CPU và xử lý tín
hiệu trước khi MCU nhận mẫu. Bộ lọc phần mềm cho phép chọn đặc tính theo phổ
rung thực tế, bố trí notch và điều chỉnh theo RPM khi có dữ liệu phù hợp.

Nếu nhiễu tập trung tại một đỉnh hẹp, notch có thể suy giảm vùng đó và cho
phép đặt thông thấp ở tần số cắt cao hơn so với chỉ dùng thông thấp. Lợi ích
là giữ tốt hơn đáp ứng trong dải chuyển động cần điều khiển. Tuy nhiên, notch
vẫn làm thay đổi biên độ và pha quanh vùng chắn; thông thấp cũng gây trễ,
và nhiều tầng lọc có tác động cộng dồn. PX4 mô tả trực tiếp đánh đổi này [2].

Lấy mẫu nhanh hơn không tự làm mọi bộ lọc phần mềm ít trễ hơn: với cùng tần
số cắt tính bằng Hz, độ trễ còn phụ thuộc loại lọc và tần số tín hiệu. Riêng
BMI270, đổi ODR còn thay đổi băng thông và độ trễ của chuỗi lọc nội, nên phải
xét thêm các giá trị trong datasheet dưới đây. Không nên mặc định tắt lọc nội
BMI270 hoặc chốt tần số cắt khi chưa kiểm tra cấu hình và dữ liệu rung đo được.

### 4.3. Độ trễ nội BMI270 theo ODR

Datasheet Bosch BST-BMI270-DS000-07, mục 4.6.11, bảng 10 và 13, trang 30,
cho các giá trị sau với `GYR_CONF.gyr_bwp = 0x02` và
`GYR_CONF.gyr_filter_perf = 1` [7]:

| ODR gyro | Tần số cắt −3 dB | Group delay điển hình |
| -------- | ---------------- | --------------------- |
| 200 Hz   | 77 Hz            | 5,97 ms               |
| 400 Hz   | 152 Hz           | 3,55 ms               |
| 800 Hz   | 300 Hz           | 2,34 ms               |
| 1600 Hz  | 557 Hz           | 0,97 ms               |
| 3200 Hz  | 751 Hz           | 0,82 ms               |

`BMI2_GYR_NORMAL_MODE` trong firmware tương ứng `gyr_bwp = 0x02`; cấu hình
hiện tại bật chế độ lọc hiệu năng và đặt ODR 400 Hz. Vì vậy 3,55 ms là giá
trị datasheet tương ứng cho gyro, không phải kết quả đo trên board. Giá trị
này đã lớn hơn chu kỳ PID 400 Hz là 2,5 ms; đọc ngay khi có data-ready không
loại bỏ được độ trễ nội đó. Group delay biểu thị trễ của tín hiệu qua chuỗi
xử lý, không có nghĩa giao dịch SPI cần chờ thêm đúng khoảng thời gian này.

Tăng ODR từ 400 lên 800 Hz giảm group delay nội điển hình 1,21 ms; tăng lên
1600 Hz giảm 2,58 ms so với 400 Hz. Đồng thời băng thông và nhiễu đầu ra tăng,
nên cần tính cả độ trễ của bộ lọc phần mềm, tích lũy/giảm tần số và lịch PID
khi so sánh. Không được xem toàn bộ mức giảm trễ nội là mức cải thiện cuối cùng.

Do đó, 800/400 Hz có thêm động cơ thiết kế là giảm trễ nội cảm biến, ngoài
khả năng lọc rung. Phương án gyro 1600 Hz, PID 400 Hz cũng đáng đánh giá nếu
CPU và I/O cho phép; cần xử lý đủ bốn mẫu trong mỗi bước PID. Các phương án
400/400 và 800/400 Hz ở mục 1 là điểm thử ban đầu, chưa phải lựa chọn tối ưu
về độ trễ. Chỉ tăng tốc độ poll MCU mà giữ ODR 400 Hz không đem lại mức giảm
group delay này.

### 4.4. Lộ trình từ lọc nội đến notch theo dữ liệu đo

Ban đầu có thể dùng bộ lọc số tích hợp trong BMI270 ở ODR 400 Hz, đọc theo
mẫu mới và chạy rate 400 Hz để kiểm tra hoạt động cơ bản. Bộ lọc nội đã xử lý
tín hiệu trước khi xuất mẫu cho MCU; chưa cần thêm notch khi chưa xác định
được thành phần rung cần suy giảm. Cấu hình này vẫn phải được đánh giá về
nhiễu và độ trễ, không mặc định đủ cho điều khiển bay.

Cần phân biệt ODR vận hành với ODR thu dữ liệu khảo sát. Có thể giữ nhịp rate
400 Hz nhưng thu gyro ở 800 hoặc 1600 Hz trên board/bộ gá để quan sát phổ
rộng hơn. Dữ liệu 400 Hz không phân biệt đầy đủ các thành phần trên 200 Hz;
nhiễu đã alias xuống thấp có thể dẫn đến chọn sai tần số notch. Khi khảo sát,
cần ghi rõ đáp ứng bộ lọc nội và giữ đủ mẫu, vì cả lọc nội lẫn mất mẫu đều
ảnh hưởng đến phổ quan sát được.

Lộ trình lựa chọn bộ lọc gồm:

1. Xác minh nhịp mẫu, timing, đơn vị và hệ trục với bộ lọc nội làm cơ sở.
2. Thu dữ liệu ODR cao hơn ở các điều kiện motor đại diện, phân tích phổ
   và sự thay đổi đỉnh rung theo thời gian hoặc RPM.
3. Nếu có đỉnh hẹp tương đối cố định, đánh giá notch cố định. Nếu đỉnh dịch
   theo tốc độ motor, đánh giá notch theo RPM khi có telemetry phù hợp hoặc
   dynamic notch dựa trên ước lượng phổ. Cả hai cần quản lý dữ liệu trễ và
   tránh bám nhầm thành phần chuyển động thật.
4. So sánh mức giảm nhiễu, tổng độ trễ và tải CPU để chọn ODR vận hành,
   chuỗi lọc và nhịp giảm tần số. Chỉ giữ các tầng lọc có lợi ích đo được.

Không cần chờ thiết kế xong notch mới tăng ODR: dữ liệu ODR cao có thể là
đầu vào cần thiết để thiết kế nó. Ngược lại, ODR cao không bắt buộc phải đi
kèm notch; ngay cả khi chỉ dùng thông thấp, nó vẫn có thể cho phép lựa chọn
đáp ứng phù hợp hơn. So sánh phải dựa trên mức suy giảm nhiễu và băng thông
điều khiển tương đương, thay vì chỉ so độ trễ nội IMU. Nếu lọc nội đã đáp ứng
yêu cầu, tiếp tục dùng nó là lựa chọn hợp lý.

### 4.5. Cách tách nhịp trong dự án tham khảo

Betaflight đặt chu kỳ xử lý mục tiêu bằng `pidDenom` lần chu kỳ lấy mẫu trong
[gyroSetTargetLooptime()](../external/repos/betaflight/src/main/sensors/gyro_init.c).
Ví dụ theo công thức, gyro 8 kHz và hệ số 2 cho PID 4 kHz; đây không phải mặc
định cho mọi board. `gyroUpdate()` lọc phục vụ giảm tần số hoặc cộng mẫu để
lấy trung bình. Các bộ lọc còn lại và PID có lịch riêng.

ArduPilot tách nhịp mẫu cảm biến với nhịp backend lọc; tài liệu có ví dụ lấy
mẫu 8 kHz rồi giảm còn 1 kHz [3]. Vòng rate nhanh có thể chạy theo nhịp gyro
đã xử lý hoặc một ước số của nhịp đó [4]. PX4 lọc ở nhịp mẫu cảm biến và
giới hạn nhịp xuất dữ liệu điều khiển bằng `IMU_GYRO_RATEMAX` [2]. Các ví dụ
này minh họa kiến trúc nhiều nhịp, không phải tần số phải sao chép sang F103.

## 5. Các lựa chọn ước lượng tư thế và quan hệ với lịch chạy

### 5.1. Quaternion và thuật toán ước lượng

Lọc thông thấp/notch xử lý nhiễu trên tín hiệu IMU; bộ ước lượng tư thế kết
hợp các phép đo và mô hình chuyển động để suy ra tư thế. Quaternion là cách
biểu diễn tư thế, không phải tên một bộ lọc. Dùng quaternion không đồng nghĩa
với việc phải dùng EKF.

| Lựa chọn | Đặc điểm cần nhớ khi chọn thuật toán                                                                                                          |
| -------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| Mahony   | Bộ lọc bù phi tuyến; dùng gyro lan truyền tư thế và sai lệch hướng tham chiếu để hiệu chỉnh, có thể ước lượng bias gyro                       |
| Madgwick | Bộ ước lượng quaternion với bước hiệu chỉnh theo gradient; là một lựa chọn để đánh giá cùng Mahony [5]                                        |
| KF       | Bộ lọc Kalman chuẩn cho mô hình tuyến tính; cần xác định rõ mô hình và phạm vi xấp xỉ khi áp dụng cho tư thế                                  |
| EKF      | Mở rộng Kalman bằng tuyến tính hóa mô hình phi tuyến quanh ước lượng hiện tại; có thể thiết kế cho tư thế, bias hoặc nhiều trạng thái hơn [6] |

EKF không phải lựa chọn duy nhất phù hợp cho điều khiển tư thế quadrotor.
Mahony và Madgwick có triển khai quaternion để khảo sát [5]. Các lựa chọn
không nhất thiết có cùng đầu vào hoặc cùng tập trạng thái, nên cần so sánh
theo yêu cầu chuyển động, cảm biến, độ chính xác và tải tính toán cụ thể.
Phương trình cập nhật, quy ước quaternion, chọn gain và đánh giá thuật toán
thuộc phần nghiên cứu riêng về ước lượng tư thế.

### 5.2. Estimator không nhất thiết cùng nhịp vòng attitude

Vòng attitude chỉ cần sử dụng tư thế tại mỗi bước điều khiển, nhưng estimator
có thể cập nhật nhanh hơn để giữ trạng thái sát thời điểm hiện tại. Nếu rate
400 Hz và attitude 100 Hz, có bốn mẫu gyro trong mỗi bước attitude 10 ms.
Không nên bỏ ba mẫu rồi nhân mẫu cuối với toàn bộ 10 ms.

Có hai cách sử dụng dữ liệu:

1. Cập nhật quaternion theo từng mẫu hoặc từng bước rate, vòng attitude đọc
   quaternion mới nhất ở 100 Hz.
2. Tích lũy gia số góc từ đủ mẫu, sau đó cập nhật estimator chậm hơn. Khi
   chuyển động ba chiều nhanh, cần xét sai số do thứ tự các phép quay và
   hiệu chỉnh coning; lấy trung bình đơn giản không tương đương tuyệt đối.

Với phương án gyro 800 Hz và quaternion 400 Hz, cần tích lũy thông tin từ cả
hai mẫu. Có thể thử cập nhật quaternion trực tiếp 800 Hz nếu đo được còn đủ
ngân sách CPU. Nhánh estimator không nhất thiết dùng cùng chuỗi lọc với
PID hoặc D-term: lọc quá mạnh có thể làm tư thế bị trễ.

Mahony/Madgwick là các phương án tương đối gọn so với một EKF đầy đủ, nhưng
chi phí thực tế phụ thuộc triển khai, chuẩn hóa vector và phép căn/chia trên
MCU không FPU. Mọi bước tích phân phải dùng đúng `dt`, tính bằng giây; các
gain hoặc hệ số lọc phụ thuộc chu kỳ phải cập nhật khi đổi tần số.

Với EKF, bước dự đoán/lan truyền và bước hiệu chỉnh có thể chạy khác nhịp.
PX4 còn lan truyền đầu ra từ thời điểm hợp nhất dữ liệu trễ đến hiện tại [6].
Do đó không có quy tắc chung rằng toàn bộ estimator phải nhanh hơn, bằng
hoặc chậm hơn vòng rate.

## 6. Đường dữ liệu và lịch chạy đề xuất

```text
Mẫu IMU mới → hiệu chuẩn và đổi sang hệ trục thân
                 ├→ lọc gyro → giảm tần số → PID rate → mixer → lệnh ESC
                 └→ tích lũy chuyển động → quaternion
                                             ↓
                                      điều khiển attitude
                                             ↓
                                      setpoint cho PID rate
```

Vòng rate phản hồi bằng gyro đã xử lý, không lấy đạo hàm góc Euler để suy ra
tốc độ góc. Vòng attitude dùng tư thế ước lượng để sinh setpoint rate; giữa
hai bước attitude, vòng rate tiếp tục dùng setpoint mới nhất.

Ở phương án cơ sở, sau mỗi mẫu mới: xử lý IMU, cập nhật quaternion, chạy
attitude nếu tới hạn, rồi chạy rate và mixer. Ở phương án 800/400 Hz, lọc và
tích lũy ở mỗi mẫu; cứ hai mẫu mới thực hiện bước 400 Hz. Đặt bộ lọc chống
aliasing trước giảm tần số, với hệ số theo đúng nhịp chạy của từng bộ lọc.

Ưu tiên đồng bộ xử lý với data-ready hoặc cơ chế xác nhận mẫu mới. Hai tác vụ
độc lập cùng ghi 400 Hz vẫn có thể lệch pha hoặc trôi đồng hồ. Nếu dùng ngắt,
ISR chỉ nên ghi nhận sự kiện/thời gian; phần đọc và tính toán chạy trong ngữ
cảnh phù hợp. FIFO hữu ích để giữ mẫu khi nhận theo lô, nhưng cần quản lý độ
sâu hàng đợi để tránh tăng tuổi mẫu. DMA chỉ nên thêm khi phép đo cho thấy
đường SPI đang chiếm ngân sách đáng kể.

`App/` sở hữu lịch chạy, trạng thái và xử lý lỗi; `BSP/` sở hữu I/O và thời
gian; thuật toán lọc, quaternion, PID nhận mẫu và `dt` qua API độc lập HAL
trong `Control/`. Hợp đồng dữ liệu phải thống nhất đơn vị, hệ trục thân, chiều
quay dương và chiều biến đổi quaternion. Dùng phép trừ bộ đếm có xử lý tràn;
không trộn trực tiếp đồng hồ MCU với đồng hồ cảm biến.

## 7. Kiểm chứng trên board và bộ gá

Trước khi chọn 400/400 Hz hay 800/400 Hz, cần thu dữ liệu cho:

- Nhịp mẫu thực tế, số mẫu mất/lặp và khoảng cách giữa các timestamp.
- Tuổi mẫu khi PID sử dụng, thời gian thực thi lớn nhất và jitter của chuỗi
  đọc–lọc–ước lượng–điều khiển–xuất lệnh; so với ngân sách 2,5 ms ở 400 Hz.
- Phổ rung gyro/accel và đáp ứng sau lọc trên các điều kiện motor tương ứng,
  để chọn thông thấp và quyết định có cần notch hay không.
- Đáp ứng tư thế, drift khi đứng yên và sai lệch khi có gia tốc tịnh tiến.
- Hành vi khi mất mẫu hoặc lỗi SPI: giới hạn thời gian chặn, phát hiện mẫu
  quá cũ, và phản ứng trạng thái an toàn của ứng dụng.

So sánh hai cấu hình trong điều kiện cơ khí và tải tương đương. Ghi rõ ODR,
range, cấu hình bộ lọc nội, hệ số lọc phần mềm, estimator, gain, build flags
và nguồn timestamp. Lưu dữ liệu gốc tại `experiments/`, script phân tích tại
`tools/`. Kiểm thử thuật toán nhỏ trên máy tính có thể kiểm tra đơn vị, hệ
trục và tính toán; timing và I/O cần xác minh trên board/bộ gá. Chưa có kết
quả đo để khẳng định phương án 800/400 Hz tốt hơn phương án cơ sở.

## 8. Nguồn đối chiếu

Nguồn cục bộ được dẫn trực tiếp tại các mục tương ứng. Tài liệu trực tuyến
được đối chiếu ngày 14/11/2026; nội dung và mặc định có thể thay đổi theo phiên bản.

1. [GCC 14.3 — Optimize Options](https://gcc.gnu.org/onlinedocs/gcc-14.3.0/gcc/Optimize-Options.html): `-O0`, `-Os`, `-ffast-math` và đánh đổi tối ưu.
2. [PX4 — MC Filter Tuning & Control Latency](https://docs.px4.io/main/en/config_mc/filter_tuning): nhịp lọc, notch, thông thấp và độ trễ.
3. [ArduPilot — Measuring Vibration with IMU Batch Sampler](https://ardupilot.org/copter/docs/common-imu-batchsampling.html): lấy mẫu, giảm tần số và backend lọc gyro.
4. [ArduPilot — Aggressive Rate Loop Tuning](https://ardupilot.org/copter/docs/high-loop-rate-tuning.html): tách vòng rate nhanh khỏi main loop. Không dùng con số “2.5uS” trên trang này để suy ra chu kỳ 4 kHz; chu kỳ tương ứng là 250 µs.
5. [x-io — Open source IMU and AHRS algorithms](https://x-io.co.uk/open-source-imu-and-ahrs-algorithms/): nguồn thuật toán, báo cáo và triển khai quaternion Mahony/Madgwick.
6. [PX4 — Using PX4's Navigation Filter (EKF2)](https://docs.px4.io/main/en/advanced_config/tuning_the_ecl_ekf): hợp nhất dữ liệu trễ và lan truyền trạng thái đầu ra.
7. [Bosch Sensortec — BMI270 Datasheet, BST-BMI270-DS000-07](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf): mục 4.6.11, bảng 10 và 13 về băng thông và group delay gyro theo ODR.
