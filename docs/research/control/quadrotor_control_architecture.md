# KIẾN TRÚC ĐIỀU KHIỂN QUADROTOR, CÁC VÒNG LẶP (CASCADED LOOPS) VÀ THUẬT TOÁN MIXER

> **Tài liệu nghiên cứu và đối chiếu mã nguồn từ các dự án mã nguồn mở tiêu biểu**  
> _(PX4-Autopilot, ArduPilot, Betaflight, Crazyflie, STM32 Quadcopter, Arduino-Quadcopter)_

---

## MỤC LỤC

1. [Tổng Quan Kiến Trúc Điều Khiển Phân Tầng (Cascaded Control Architecture)](#1-tổng-quan-kiến-trúc-điều-khiển-phân-tầng)
2. [Chi Tiết Từng Vòng Điều Khiển (Input - Algorithm - Output)](#2-chi-tiết-từng-vòng-điều-khiển)
   - [2.1 Vòng Vị trí (Position Loop)](#21-vòng-vị-trí-position-loop)
   - [2.2 Vòng Vận tốc (Velocity Loop)](#22-vòng-vận-tốc-velocity-loop)
   - [2.3 Bộ Chuyển Đổi Gia Tốc / Lực Nâng Sang Góc Tư Thế (Accel to Attitude)](#23-bộ-chuyển-đổi-gia-tốc--lực-nâng-sang-góc-tư-thế)
   - [2.4 Vòng Tư thế (Attitude / Angle Loop)](#24-vòng-tư-thế-attitude--angle-loop)
   - [2.5 Vòng Vận tốc góc (Angular Rate / Gyro Loop)](#25-vòng-vận-tốc-góc-angular-rate--gyro-loop)
3. [Thuật Toán Mixer & Phân Phối Lực Động Cơ (Control Allocation)](#3-thuật-toán-mixer--phân-phối-lực-động-cơ)
   - [3.1 Ma trận Mixer hình học (Khung X và Khung +)](#31-ma-trận-mixer-hình-học)
   - [3.2 Ma trận hiệu dụng điều khiển & Giả nghịch đảo (Control Allocation Matrix)](#32-ma-trận-hiệu-dụng-điều-khiển--giả-nghịch-đảo)
   - [3.3 Hiện tượng bão hòa động cơ & Thứ tự ưu tiên (Sequential Desaturation)](#33-hiện-tượng-bão-hòa-động-cơ--thứ-tự-ưu-tiên-sequential-desaturation)
   - [3.4 Cơ chế Air Mode (Bảo toàn mô-men khi Throttle = 0)](#34-cơ-chế-air-mode-bảo-toàn-mô-men-khi-throttle--0)
   - [3.5 Thuật toán Capping đối xứng (Crazyflie Method)](#35-thuật-toán-capping-đối-xứng-crazyflie-method)
   - [3.6 Tuyến tính hóa lực đẩy (Thrust Linearization)](#36-tuyến-tính-hóa-lực-đẩy-thrust-linearization)
4. [Phân Tích Chi Tiết Mã Nguồn Các Dự Án Thực Tế](#4-phân-tích-chi-tiết-mã-nguồn-các-dự-án-thực-tế)
   - [4.1 PX4-Autopilot](#41-px4-autopilot)
   - [4.2 ArduPilot (ArduCopter)](#42-ardupilot-arducopter)
   - [4.3 Betaflight](#43-betaflight)
   - [4.4 Crazyflie Firmware](#44-crazyflie-firmware)
   - [4.5 STM32 Quadcopter & Arduino-Quadcopter](#45-stm32-quadcopter--arduino-quadcopter)
5. [Khuyến Nghị Và Lộ Trình Triển Khai Thiết Kế](#5-khuyến-nghị-và-lộ-trình-triển-khai-thiết-kế)

---

## 1. Tổng Quan Kiến Trúc Điều Khiển Phân Tầng

Quadrotor là một hệ thống **Under-actuated** (chỉ có 4 động cơ tạo lực đẩy song song dọc theo trục $Z$ của thân máy bay, nhưng cần làm chủ 6 bậc tự do: vị trí $x, y, z$ và góc hướng $\phi, \theta, \psi$).
Để sinh ra gia tốc tịnh tiến theo phương ngang ($x, y$), máy bay bắt buộc phải nghiêng thân một góc Roll/Pitch ($\phi, \theta$) tương ứng.

### Nguyên lý tách thang thời gian (Time-scale Separation)

Trong động lực học bay:

- **Động học quay (Rotational Dynamics)**: Máy bay có mô-men quán tính nhỏ, phản ứng quay quanh các trục diễn ra rất nhanh (vài mili-giây đến vài chục mili-giây).
- **Động học tịnh tiến (Translational Dynamics)**: Chuyển động thay đổi vị trí đòi hỏi thời gian tích lũy vận tốc, phản ứng diễn ra chậm hơn đáng kể (hàng trăm mili-giây đến giây).

Do đó, toàn bộ các hệ thống điều khiển bay tiêu chuẩn đều được thiết kế dưới dạng **chuỗi vòng lặp lồng nhau (Cascaded Loops)**:

```text
[Mục tiêu Vị trí: x_sp, y_sp, z_sp]
               │
               ▼
   ┌───────────────────────┐   Tần số: 50 - 100 Hz
   │  VÒNG 1: VỊ TRÍ       │ ◄─── Vị trí thực tế (GPS, VIO, Baro, Lidar)
   │  (Position Loop)      │
   └───────────┬───────────┘
               │ Vận tốc đặt (vx_sp, vy_sp, vz_sp)
               ▼
   ┌───────────────────────┐   Tần số: 50 - 100 Hz
   │  VÒNG 2: VẬN TỐC      │ ◄─── Vận tốc thực tế (EKF, Optical Flow)
   │  (Velocity Loop)      │
   └───────────┬───────────┘
               │ Gia tốc đặt (ax_sp, ay_sp, az_sp) + Tổng lực đẩy (Thrust)
               ▼
   ┌───────────────────────┐
   │ BỘ CHUYỂN ĐỔI GIA TỐC │ ─── Định hướng trục Z thân máy bay (Body Z-axis)
   │ SANG GÓC TƯ THẾ       │ ─── Kết hợp góc xoay Yaw mong muốn (Heading)
   └───────────┬───────────┘
               │ Góc tư thế đặt: Roll_sp, Pitch_sp, Yaw_sp (hoặc Quaternion q_sp)
               ▼
   ┌───────────────────────┐   Tần số: 100 - 250 Hz (Crazyflie, PX4)
   │  VÒNG 3: TƯ THẾ       │ ◄─── Góc nghiêng thực tế (AHRS / IMU Sensor Fusion)
   │  (Attitude Loop)      │
   └───────────┬───────────┘
               │ Tốc độ góc đặt: p_sp, q_sp, r_sp (rad/s hoặc deg/s)
               ▼
   ┌───────────────────────┐   Tần số: 250 Hz - 8 kHz (Inner-most Core Loop)
   │  VÒNG 4: VẬN TỐC GÓC  │ ◄─── Tốc độ góc thực (Gyroscope đã lọc nhiễu)
   │  (Angular Rate Loop)  │
   └───────────┬───────────┘
               │ Mô-men xoắn yêu cầu: [tau_x, tau_y, tau_z] + Tổng lực nâng T
               ▼
   ┌───────────────────────┐
   │  BỘ PHỐI TRỘN ĐỘNG CƠ │ ─── Khử bão hòa động cơ (Desaturation)
   │  (Mixer / Allocation) │ ─── Duy trì điều khiển ở ga thấp (Air Mode)
   └───────────┬───────────┘
               │ Tín hiệu lệnh từng động cơ (m1, m2, m3, m4)
               ▼
         [4x ESC & Motors]
```

---

## 2. Chi Tiết Từng Vòng Điều Khiển

| Vòng điều khiển              | Tần số lấy mẫu   | Bộ điều khiển                    | Tín hiệu vào (Inputs)                                                                   | Tín hiệu ra (Outputs)                                           |
| :--------------------------- | :--------------- | :------------------------------- | :-------------------------------------------------------------------------------------- | :-------------------------------------------------------------- |
| **Vị trí (Position)**        | 50 – 100 Hz      | **P** hoặc P + Feedforward       | Vị trí đặt $\vec{p}_{sp}$, Vị trí ước lượng $\hat{\vec{p}}$                             | Vận tốc đặt $\vec{v}_{sp} = [v_x, v_y, v_z]^T$                  |
| **Vận tốc (Velocity)**       | 50 – 100 Hz      | **PID** + Accel Feedforward      | Vận tốc đặt $\vec{v}_{sp}$, Vận tốc ước lượng $\hat{\vec{v}}$, Gia tốc trọng trường $g$ | Vector gia tốc đặt $\vec{a}_{sp}$ & Lực nâng tập trung $T_z$    |
| **Chuyển đổi Lực $\to$ Góc** | Cùng tần số Vel  | Giải tích hình học / Vector      | $\vec{a}_{sp}$, $T_z$, Yaw mong muốn $\psi_{sp}$                                        | Quaternion tư thế đặt $q_{sp}$ hoặc $(\phi_{sp}, \theta_{sp})$  |
| **Tư thế (Attitude)**        | 100 – 500 Hz     | **P** (Euler hoặc Quaternion)    | Tư thế đặt $q_{sp}$, Tư thế thực $\hat{q}$ (từ AHRS/EKF)                                | Tốc độ góc đặt $\vec{\omega}_{sp} = [p_{sp}, q_{sp}, r_{sp}]^T$ |
| **Vận tốc góc (Rate)**       | 250 Hz – 8 kHz   | **PID** + Rate Feedforward       | $\vec{\omega}_{sp}$, Tốc độ góc thực từ Gyroscope $\vec{\omega}_{gyro}$                 | Mô-men xoắn $\vec{\tau} = [\tau_x, \tau_y, \tau_z]^T$           |
| **Bộ Mixer**                 | Cùng tần số Rate | Ma trận hiệu dụng + Giảm bão hòa | Mô-men $\vec{\tau}$, Lực nâng $T$                                                       | Tín hiệu xung PWM / DShot từng motor $(m_1..m_4)$               |

---

### 2.1 Vòng Vị trí (Position Loop)

- **Mục tiêu**: Bám theo vị trí toạ độ mong muốn trong hệ quy chiếu cục bộ (NED / ENU).
- **Thuật toán phổ biến**: Bộ điều khiển tỉ lệ **P** (Proportional) kết hợp bù đón đầu **Feed-Forward**:
  $$\vec{v}_{sp} = K_p^{pos} \cdot (\vec{p}_{sp} - \hat{\vec{p}}) + \vec{v}_{ff}$$
- **Xử lý phi tuyến thực tế**:
  - **Giới hạn tốc độ (Velocity Saturation)**: $\vec{v}_{sp}$ phải được giới hạn theo phương ngang $v_{xy,max}$ và phương đứng $v_{z,up}, v_{z,down}$.
  - **Square Root Controller (ArduPilot)**: Khi khoảng cách sai lệch lớn, bộ P thuần sẽ tạo vận tốc mong muốn quá cao. ArduPilot dùng thuật toán giảm tốc căn bậc hai:
    $$v_{sp} = \sqrt{2 \cdot a_{max} \cdot \Delta p}$$
    giúp drone giảm tốc mượt mà với gia tốc không đổi khi tiếp cận điểm đích, tránh giật và overshoot.

---

### 2.2 Vòng Vận tốc (Velocity Loop)

- **Mục tiêu**: Bám theo vector vận tốc mong muốn $\vec{v}_{sp}$.
- **Thuật toán phổ biến**: **PID** trên sai số vận tốc $\vec{e}_v = \vec{v}_{sp} - \hat{\vec{v}}$:
  $$\vec{a}_{sp} = K_p^{vel} \vec{e}_v + K_i^{vel} \int \vec{e}_v dt - K_d^{vel} \dot{\vec{v}} + \vec{a}_{ff}$$
- **Đặc điểm trọng yếu trên trục Z (Cao độ)**:
  - Trục Z phải chịu gia tốc trọng trường $g \approx 9.81 \text{ m/s}^2$.
  - Lực nâng thẳng đứng: $a_{z,sp} = a_{z,PID} - g$.
  - Tỉ lệ lực nâng chuẩn hóa theo trạng thái treo tĩnh (Hover Thrust $T_{hover}$):
    $$T_{z} = \frac{a_{z,sp}}{g} \cdot T_{hover}$$
  - **Anti-Windup**: Khi ga đã chạm mức tối đa hoặc tối thiểu, phải ngừng tích phân $K_i$ theo trục Z ngay lập tức.

---

### 2.3 Bộ Chuyển Đổi Gia Tốc / Lực Nâng Sang Góc Tư Thế

Do quadrotor chỉ có thể đẩy theo trục $Z$ của thân máy bay ($Z_B$), để tạo ra vector gia tốc mong muốn trong không gian $\vec{a}_{sp} = [a_x, a_y, a_z]^T$, trục $Z_B$ phải nghiêng theo hướng của vector lực tổng:

$$\vec{F}_{des} = m \cdot \begin{bmatrix} a_x \\ a_y \\ a_z - g \end{bmatrix}$$

1. **Chuẩn hóa vector trục Z thân máy bay mong muốn**:
   $$\vec{z}_B = \frac{-\vec{F}_{des}}{\|\vec{F}_{des}\|}$$
2. **Khống chế góc nghiêng tối đa (Tilt Angle Limit)**:
   Để tránh việc drone lật quá nghiêng gây sụt giảm lực nâng thẳng đứng, góc giữa $\vec{z}_B$ và phương thẳng đứng của Trái Đất $\vec{z}_W = [0, 0, 1]^T$ được bão hòa qua hàm giới hạn góc `limitTilt()` (xem PX4 `ControlMath::limitTilt`).
3. **Dựng hệ trục toạ độ mục tiêu ($R\_{sp} = [\vec{x}\_B, \vec{y}\_B, \vec{z}\_B]$)**:
   Dựa vào hướng Yaw mong muốn $\psi_{sp}$, vector phụ nằm trên mặt phẳng ngang là $\vec{y}_C = [-\sin\psi_{sp}, \cos\psi_{sp}, 0]^T$:
   $$\vec{x}_B = \frac{\vec{y}_C \times \vec{z}_B}{\|\vec{y}_C \times \vec{z}_B\|}, \quad \vec{y}_B = \vec{z}_B \times \vec{x}_B$$
4. Chuyển ma trận xoay $R_{sp}$ thành Quaternion mong muốn $q_{sp}$ hoặc các góc Euler tương đương:
   $$\phi_{sp} = \arcsin\left(\frac{a_x \sin\psi - a_y \cos\psi}{g}\right), \quad \theta_{sp} = \arctan\left(\frac{a_x \cos\psi + a_y \sin\psi}{g - a_z}\right)$$

---

### 2.4 Vòng Tư thế (Attitude / Angle Loop)

- **Mục tiêu**: Đưa hướng nghiêng của thân drone về trùng với hướng mục tiêu.
- **Cách tiếp cận 1: Góc Euler (Dành cho hệ thống đơn giản: Betaflight Angle mode, STM32 Quadcopter)**:
  $$p_{sp} = K_p^{att} \cdot (\phi_{sp} - \hat{\phi})$$
  $$q_{sp} = K_p^{att} \cdot (\theta_{sp} - \hat{\theta})$$
  $$r_{sp} = K_p^{yaw} \cdot (\psi_{sp} - \hat{\psi}) + r_{ff}$$
  _Nhược điểm: Bị hiện tượng Gimbal Lock khi góc Pitch $\to \pm 90^\circ$ và tính toán hàm lượng giác tốn chu kỳ MCU._

- **Cách tiếp cận 2: Quaternion (Chuẩn công nghiệp & hàn lâm: PX4, ArduPilot, Crazyflie)**:
  - Tính Quaternion lỗi:
    $$q_e = \hat{q}^{-1} \otimes q_{sp} = [q_{e,w}, \vec{q}_{e,v}]^T$$
  - Vector sai số góc quay quanh trục thân (Axis-Angle error vector):
    $$\vec{e}_q = 2 \cdot \text{sgn}(q_{e,w}) \cdot \vec{q}_{e,v}$$
  - Tách thành phần nghiêng (Tilt: Roll/Pitch) và xoay hướng (Yaw) với hệ số trọng số `yaw_weight` để luôn ưu tiên giữ thăng bằng chống lật:
    $$\vec{\omega}_{sp} = K_p \cdot \vec{e}_q + \vec{\omega}_{ff}$$

---

### 2.5 Vòng Vận tốc góc (Angular Rate / Gyro Loop)

Đây là **vòng lặp trong cùng (Inner-most Loop)**, có vai trò sống còn quyết định máy bay có ổn định hay bị rung lắc, dao động.

- **Tín hiệu vào**: $\vec{\omega}_{sp} = [p_{sp}, q_{sp}, r_{sp}]^T$ và dữ liệu đo góc quay từ Gyroscope $\vec{\omega}_{gyro} = [p, q, r]^T$.
- **Giải thuật**: **PID đầy đủ + Feedforward**:
  $$\tau_x = K_p \cdot e_p + K_i \int e_p dt + K_d \cdot \frac{d(-p)}{dt} + K_{ff} \cdot p_{sp}$$

#### Các kỹ thuật xử lý tín hiệu bắt buộc trong Rate Loop

1. **D-on-Measurement (Đạo hàm trên giá trị đo)**:
   Thay vì vi phân sai số $\frac{d(e)}{dt} = \frac{d(sp - meas)}{dt}$, ta chỉ lấy vi phân của tín hiệu đo $\frac{-d(meas)}{dt}$. Điều này ngăn hiện tượng "Derivative Kick" – khi tín hiệu từ cần lái nhảy đột ngột, motor không bị vọt lố công suất.
2. **Lọc thông thấp cho khâu D (D-term Filter)**:
   Khâu vi phân $D$ khuếch đại nhiễu tần số cao (do cánh quạt và motor quay rung truyền vào IMU). Bắt buộc phải có bộ lọc thông thấp (PT1 Low-pass, Biquad Filter, hoặc Dynamic Notch Filter theo vòng tua motor RPM).
3. **Feedforward ($K\_{ff}$)**:
   Cho phép mô-men phản hồi lập tức theo chuyển động tay gạt cần lái mà không cần đợi sai số xuất hiện rồi khâu $P$ mới phản ứng. Giúp tăng độ nhạy (stick responsiveness) mà không gây dao động do tăng $K_p$.
4. **Anti-Windup (Chống bão hòa tích phân)**:
   Khi đầu ra motor đã chạm giới hạn trần (100%) hoặc sàn (0%), đóng băng khâu tích phân $I$ (I-term freeze).

---

## 3. Thuật Toán Mixer & Phân Phối Lực Động Cơ

Sau khi vòng Rate tính ra **Mô-men xoắn 3 trục $(\tau_x, \tau_y, \tau_z)$** và **Tổng lực nâng $T$ (Collective Thrust)**, bộ phối trộn (Mixer) chịu trách nhiệm tính toán công suất phát ra cho từng động cơ.

### 3.1 Ma trận Mixer hình học

Quy ước chiều:

- Roll ($\tau_x$): Nghiêng sang phải dương $\to$ Motor bên trái tăng ga, motor bên phải giảm ga.
- Pitch ($\tau_y$): Mũi ngóc lên dương $\to$ Motor phía trước tăng ga, motor phía sau giảm ga.
- Yaw ($\tau_z$): Xoay mũi sang phải (theo chiều kim đồng hồ) dương $\to$ Motor quay CCW tăng tốc, motor quay CW giảm tốc (theo định luật bảo toàn mô-men động lượng).

#### Cấu hình Quad X (Chuẩn phổ biến hiện nay)

```text
      M4 (CW)     M1 (CCW)
         \       /
          \  ▲  /  (Mũi tên chỉ hướng đầu máy bay)
          /     \
         /       \
      M3 (CCW)    M2 (CW)
```

Công thức chuyển đổi:

$$
\begin{cases}
m_1 = T - \tau_{roll} + \tau_{pitch} - \tau_{yaw} \\
m_2 = T - \tau_{roll} - \tau_{pitch} + \tau_{yaw} \\
m_3 = T + \tau_{roll} - \tau_{pitch} - \tau_{yaw} \\
m_4 = T + \tau_{roll} + \tau_{pitch} + \tau_{yaw}
\end{cases}
$$

#### Cấu hình Quad + (Plus - thường gặp ở các dự án thử nghiệm ban đầu)

```text
             M1 (Front)
                 ▲
     M4 (Left) ──┼── M2 (Right)
                 │
             M3 (Rear)
```

$$
\begin{cases}
m_1 = T + \tau_{pitch} - \tau_{yaw} \\
m_2 = T - \tau_{roll} + \tau_{yaw} \\
m_3 = T - \tau_{pitch} - \tau_{yaw} \\
m_4 = T + \tau_{roll} + \tau_{yaw}
\end{cases}
$$

---

### 3.2 Ma trận hiệu dụng điều khiển & Giả nghịch đảo

Trong các hệ thống cao cấp như PX4 ([ControlAllocator](../../external/repos/PX4-Autopilot/src/modules/control_allocator/ControlAllocator.cpp)), mixer không dùng công thức cộng trừ cứng mà xây dựng **Ma trận hiệu dụng điều khiển (Actuator Effectiveness Matrix $B$)**:

$$\begin{bmatrix} \vec{\tau} \\ \vec{T} \end{bmatrix} = B \cdot \vec{u}$$

Với mỗi động cơ thứ $i$ đặt tại toạ độ $\vec{r}_i = [x_i, y_i, z_i]^T$, có trục quay định hướng $\vec{n}_i$, hệ số lực đẩy $C_T$, và tỉ số mô-men phản kháng $k_m$:

- Lực đẩy do động cơ tạo ra: $\vec{F}_i = C_T \cdot \vec{n}_i \cdot u_i$
- Mô-men xoắn do động cơ tạo ra: $\vec{M}_i = (\vec{r}_i \times \vec{F}_i) - C_T \cdot k_m \cdot \vec{n}_i \cdot u_i$

Tín hiệu động cơ $\vec{u}$ được giải thông qua ma trận giả nghịch đảo Moore-Penrose:
$$\vec{u} = B^+ \cdot \begin{bmatrix} \vec{\tau}_{sp} \\ \vec{T}_{sp} \end{bmatrix} = B^T (B B^T)^{-1} \begin{bmatrix} \vec{\tau}_{sp} \\ \vec{T}_{sp} \end{bmatrix}$$

---

### 3.3 Hiện tượng bão hòa động cơ & Thứ tự ưu tiên (Sequential Desaturation)

Khoảng làm việc của động cơ chỉ nằm trong $[0.0, 1.0]$. Khi drone thực hiện thao tác cơ động mạnh ở ga cao, giá trị $m_i$ tính toán từ công thức lý thuyết có thể vượt quá $1.0$ hoặc nhỏ hơn $0.0$.

Nếu chỉ đơn giản dùng hàm `clamp(m_i, 0.0, 1.0)` cho từng động cơ riêng biệt, **tỉ lệ chênh lệch mô-men giữa các động cơ bị phá vỡ**, dẫn đến mất khả năng giữ thăng bằng và drone rơi tự do.

PX4 triển khai thuật toán **Sequential Desaturation** ([ControlAllocationSequentialDesaturation.cpp](../../external/repos/PX4-Autopilot/src/lib/control_allocation/control_allocation/ControlAllocationSequentialDesaturation.cpp)) với thứ tự ưu tiên tuyệt đối:
$$\textbf{Roll \& Pitch (Giữ cân bằng chống lật)} > \textbf{Thrust (Duy trì độ cao)} > \textbf{Yaw (Giữ hướng mũi)}$$

1. **Bước 1**: Phối trộn Roll, Pitch và Thrust trước (tạm bỏ qua Yaw).
2. **Bước 2**: Kiểm tra xem động cơ nào vượt ngưỡng $[0, 1]$. Nếu vượt, điều chỉnh tăng/giảm tập trung lực nâng $T$ để kéo các động cơ về ngưỡng an toàn mà vẫn giữ nguyên $100\%$ độ lệch giữa các motor dành cho Roll/Pitch.
3. **Bước 3**: Thêm thành phần Yaw vào. Nếu việc thêm Yaw làm động cơ bị tràn biên, **cắt giảm biên độ Yaw** để bảo vệ Roll/Pitch.

---

### 3.4 Cơ chế Air Mode (Bảo toàn mô-men khi Throttle = 0)

Trong các chuyến bay nhào lộn FPV (Betaflight) hoặc khi hạ độ cao nhanh (PX4):

- **Vấn đề**: Người lái hạ cần ga về 0 ($T = 0$). Các động cơ quay ở tốc độ không tải tối thiểu. Lúc này, công thức $m_i = T \pm \tau$ sẽ khiến các motor không thể giảm ga sâu hơn được nữa để tạo chênh lệch lực $\to$ Drone mất hoàn toàn lực điều khiển tư thế (mất lái giữa không trung).
- **Thuật toán Air Mode**:
  Khi phát hiện tổng mô-men yêu cầu $\Delta m = m_{max} - m_{min} > 0$ nhưng cần ga ở mức 0, mixer tự động **nâng mức ga sàn ảo lên**:
  $$m_i = \text{offset} + m_{PID}$$
  sao cho motor thấp nhất không bị tắt và motor cao nhất có đủ khoảng trống công suất để tạo lực mô-men xoắn giữ góc theo đúng lệnh của con quay hồi chuyển.

---

### 3.5 Thuật toán Capping đối xứng (Crazyflie Method)

Trong mã nguồn Crazyflie ([power_distribution_quadrotor.c](../../external/repos/crazyflie-firmware/src/modules/src/power_distribution_quadrotor.c#L160-L190)):

```c
// Tìm động cơ có giá trị cao nhất
int32_t highestThrustFound = max(m1, m2, m3, m4);

// Nếu vượt trần cho phép (maxAllowedThrust)
if (highestThrustFound > maxAllowedThrust) {
    int32_t reduction = highestThrustFound - maxAllowedThrust;
    // Trừ đều lượng dư thừa này cho TẤT CẢ 4 động cơ
    for (int i = 0; i < 4; i++) {
        motor[i] = motor[i] - reduction;
    }
}
```

_Tác dụng_: Bằng cách trừ đều lượng dư thừa cho cả 4 động cơ, tỉ lệ sai phân lực giữa các cánh quạt được giữ nguyên chính xác $100\%$, bảo đảm mô-men lật thăng bằng không bị biến dạng, chỉ suy giảm một chút lực nâng thẳng đứng trong khoảnh khắc đó.

---

### 3.6 Tuyến tính hóa lực đẩy (Thrust Linearization)

Lực đẩy cánh quạt tỉ lệ với **bình phương tốc độ quay**:
$$F \propto \Omega^2 \propto V_{PWM}^2$$
Nếu gửi lệnh điều khiển tỉ lệ tuyến tính từ PID ra PWM, lực sinh ra ở dải ga cao sẽ nhạy gấp nhiều lần dải ga thấp, khiến việc tinh chỉnh PID rất khó khăn.
Các firmware chuyên nghiệp (ArduPilot `AP_Motors_Thrust_Linearization.cpp`, Betaflight `pidApplyThrustLinearization`) đều áp dụng hàm căn bậc hai để đưa quan hệ giữa Lệnh điều khiển $\to$ Lực đẩy thực tế về dạng tuyến tính hoàn hảo:
$$PWM_{out} = \frac{-1 + \sqrt{1 + 4 \cdot k_{lin} \cdot Thrust}}{2 \cdot k_{lin}}$$

---

## 4. Phân Tích Chi Tiết Mã Nguồn Các Dự Án Thực Tế

| Tiêu chí            | PX4-Autopilot     | ArduPilot              | Betaflight             | Crazyflie          | STM32 Quadcopter |
| :------------------ | :---------------- | :--------------------- | :--------------------- | :----------------- | :--------------- |
| **Ngôn ngữ**        | C++14/17          | C++                    | C99                    | C99                | C                |
| **Hệ điều hành**    | NuttX RTOS        | AP_HAL (NuttX/ChibiOS) | Bare-metal (Timer ISR) | FreeRTOS           | uCOS-II          |
| **Đại diện tư thế** | Unit Quaternion   | Unit Quaternion        | Euler Angles           | Quaternion / Euler | Euler Angles     |
| **Tần số Rate**     | 250 Hz – 1 kHz    | 400 Hz – 800 Hz        | 2 kHz – 8 kHz          | 500 Hz             | 333 Hz (~3ms)    |
| **Bộ mixer**        | Control Allocator | AP_MotorsMatrix        | mixTable               | Power Distribution | Direct Limit     |

---

### 4.1 PX4-Autopilot

- **Cấu trúc module**:
  - `mc_pos_control`: Đón nhận tọa độ điểm đến, chạy P vị trí $\to$ PID vận tốc $\to$ tính vector gia tốc $\vec{a}_{sp}$.
  - `mc_att_control`: Dùng thuật toán Quaternion tangent-space error. Có mô hình tham chiếu giảm chấn bậc hai (`propagateReferenceModel`) để triệt tiêu vọt lố.
  - `mc_rate_control`: Chạy đồng bộ theo ngắt con quay hồi chuyển Gyro. Áp dụng kỹ thuật nén độ lợi (`gain_compression`) khi phát hiện dao động cộng hưởng cơ khí.
  - `control_allocator`: Hỗ trợ cấu hình ma trận hình học động cơ tùy ý và thuật toán khử bão hòa đa tầng.

### 4.2 ArduPilot (ArduCopter)

- **Cấu trúc thư viện**:
  - `AC_PosControl`: Quản lý 3 trục vị trí. Sử dụng khâu Feedforward gia tốc và vận tốc kết hợp giới hạn gia tốc góc (jerk limiting).
  - `AC_AttitudeControl_Multi`: Bộ điều khiển tư thế quaternion với cơ chế **Angle P Boost** (tự động kích tăng độ nhạy góc khi cần ga biến thiên nhanh).
  - `AP_MotorsMatrix`: Cơ chế tính toán **Yaw Headroom** độc quyền. Luôn dự trữ một khoảng dung sai cho phép Yaw can thiệp mà không làm xáo trộn lực cân bằng Roll/Pitch.

### 4.3 Betaflight

- **Triết lý thiết kế**: Chuyên biệt cho đua FPV và nhào lộn tự do, tối ưu hóa độ trễ tính toán tới mức dưới $500\mu s$.
- **Đặc điểm nổi bật**:
  - **Acro Mode (Rate mode thuần)**: Bỏ qua hoàn toàn vòng Vị trí và Tư thế. Cần lái người điều khiển gán trực tiếp thành tốc độ góc quay mục tiêu ($\pm 1000^\circ/\text{s}$).
  - **Angle Mode**: Sử dụng hàm `pidLevel()` trong `pid.c` làm vòng ngoài tính độ lệch góc để lái vòng trong.
  - **Bộ lọc tinh vi**: Bộ lọc số RPM Filter (đọc trực tiếp tần số quay từng motor qua giao thức DShot hai chiều để đặt bộ lọc dải chặn notch filter theo đúng tần số rung cơ học).
  - **Anti-Gravity**: Khâu nhận biết thay đổi ga đột ngột để tạm thời nhân hệ số tích phân $I$, chống hiện tượng chúi mũi (pitch dip) khi thốc ga.

### 4.4 Crazyflie Firmware

- **Nền tảng nghiên cứu lý tưởng**:
  - Mã nguồn C cực kỳ trong sáng, tách bạch giữa tầng ước lượng trạng thái (`sensfusion6.c` / `estimator_kalman.c`), tầng điều khiển (`controller_pid.c`), và tầng phân phối công suất (`power_distribution_quadrotor.c`).
  - Hỗ trợ chuyển đổi nhanh qua các thuật toán điều khiển phi tuyến hiện đại:
    - `controller_mellinger.c`: Bộ điều khiển hình học phi tuyến trên không gian $SE(3)$ của GS. Daniel Mellinger (ĐH Pennsylvania).
    - `controller_lee.c`: Bộ điều khiển quỹ đạo hình học của GS. Taeyoung Lee.
    - `controller_indi.c`: Bộ điều khiển đảo ngược động lực học phi tuyến gia số (Incremental Nonlinear Dynamic Inversion - INDI) có khả năng kháng gió và thích nghi tải trọng vượt trội.

### 4.5 STM32 Quadcopter & Arduino-Quadcopter

- Dự án nhúng kinh điển thể hiện trọn vẹn thuật toán **Serial PID (Cascade PID)**:
  - Vòng ngoài: Nhận góc Euler sai lệch $\to$ tính ra vận tốc góc mong muốn.
  - Vòng trong: Lấy sai lệch vận tốc góc trừ dữ liệu Gyro $\to$ tính giá trị bù PWM cho 4 motor.
  - Phù hợp để làm bộ khung tham khảo cơ sở khi tự lập trình firmware trên các dòng chip STM32F4/G4 từ đầu.

---

## 5. Khuyến Nghị Và Lộ Trình Triển Khai Thiết Kế

Khi bạn bắt tay vào thiết kế bộ điều khiển cho Quadrotor của mình:

```text
[GIAI ĐOẠN 1]             [GIAI ĐOẠN 2]             [GIAI ĐOẠN 3]
Xây dựng Vòng Rate        Xây dựng Vòng Angle       Xây dựng Vòng Vị trí
(Gyro Loop)               (Attitude Loop)           (Position & Altitude)
       │                         │                         │
       ▼                         ▼                         ▼
- Đọc Gyro qua SPI        - Bộ lọc AHRS / Mahony    - Cảm biến Baro / ToF
- Bộ lọc LPF PT1 (~40Hz)  - P-Controller góc        - Cảm biến GPS / Optical Flow
- PID Rate + D-on-meas    - Bù Heading Yaw          - P Vị trí -> PID Vận tốc
- Mixer X + Capping       - Giới hạn góc nghiêng    - Ước lượng Hover Thrust
```

### 1. Trật tự Tune PID trên thực tế

- **Bước 1 (Inner Rate Loop)**: Cố định drone trên trục thử nghiệm (test bench 1 bậc tự do). Tune vòng Rate trước. Tăng $K_p$ đến khi hệ thống đáp ứng nhanh mà không rung, thêm $K_d$ để dập tắt dao động, thêm một chút $K_i$ để triệt tiêu sai số xác lập.
- **Bước 2 (Attitude Loop)**: Chỉ cần bộ điều khiển tỉ lệ **P** (khoảng $4.0 - 8.0$). Nếu $K_p$ góc quá lớn, drone sẽ giật và kích động vòng Rate bên trong.
- **Bước 3 (Thrust & Mixer)**: Bắt buộc phải có thuật toán **Desaturation / Capping đối xứng** (như của Crazyflie) để chống mất lái khi thốc ga hoặc hạ ga.
