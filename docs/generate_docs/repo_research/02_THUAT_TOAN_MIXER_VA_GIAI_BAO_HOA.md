# THUẬT TOÁN MIXER & CÁC CHIẾN LƯỢC GIẢI BÃO HÒA ĐỘNG CƠ

Trong thiết kế hệ thống điều khiển Quadrotor, **Mixer (Bộ hòa trộn động cơ)** đóng vai trò là "chiếc cầu nối" giữa bộ điều khiển trừu tượng (vốn chỉ tính toán ra Tổng lực đẩy $T$ và Mô-men xoắn 3 trục $[\tau_x, \tau_y, \tau_z]$) với các cơ cấu chấp hành vật lý (4 động cơ không chổi than).

Nếu Mixer thiết kế sơ sài, drone sẽ bị mất kiểm soát, lật úp hoặc rơi tự do ngay khi động cơ chạm ngưỡng giới hạn (hiện tượng **Bão hòa - Motor Saturation**).

---

## 1. Mô Hình Hình Học & Vật Lý Của Quadrotor Mixer

### 1.1. Cấu hình Quad-X (Phổ biến nhất hiện nay)

```
        Phía trước (Front / +X)
          M4 (CW)     M1 (CCW)
             \       /
              \     /
               [FC]
              /     \
             /       \
          M3 (CCW)    M2 (CW)
         Phía sau (Back / -X)
```

- **M1 (Front-Right)**: Quay ngược chiều kim đồng hồ (CCW)
- **M2 (Rear-Right)**: Quay thuận chiều kim đồng hồ (CW)
- **M3 (Rear-Left)**: Quay ngược chiều kim đồng hồ (CCW)
- **M4 (Front-Left)**: Quay thuận chiều kim đồng hồ (CW)

### 1.2. Nguyên lý sinh lực và mô-men
Mỗi cánh quạt $i$ ($i \in \{1, 2, 3, 4\}$) khi quay với tốc độ góc $\omega_i$ sẽ sinh ra:
1. **Lực đẩy hướng lên (Thrust)**: $f_i = c_T \omega_i^2$
2. **Mô-men cản khí động / phản lực (Drag torque)**: $\tau_{d,i} = c_Q \omega_i^2 \cdot \text{direction}_i$

Tổng hợp lực và mô-men tác động lên trọng tâm quadrotor:
- **Tổng lực đẩy (Total Thrust $T$)**:
  $$T = f_1 + f_2 + f_3 + f_4$$
- **Mô-men trục Lăn (Roll Torque $\tau_x$)** (Nghiêng trái/phải):
  Khoảng cách hình chiếu cánh quạt đến trục Roll là $d_y = L \sin(45^\circ) = \frac{\sqrt{2}}{2} L$:
  $$\tau_x = d_y (-f_1 - f_2 + f_3 + f_4)$$
- **Mô-men trục Lắc (Pitch Torque $\tau_y$)** (Chúc/ngửa mũi):
  Khoảng cách hình chiếu cánh quạt đến trục Pitch là $d_x = L \cos(45^\circ) = \frac{\sqrt{2}}{2} L$:
  $$\tau_y = d_x (f_1 - f_2 - f_3 + f_4)$$
- **Mô-men trục Chệch (Yaw Torque $\tau_z$)** (Quay đầu trái/phải):
  Dựa trên mô-men phản lực xoắn của cánh quạt:
  $$\tau_z = c (-f_1 + f_2 - f_3 + f_4) \quad \text{với } c = \frac{c_Q}{c_T}$$

### 1.3. Biểu diễn dạng Ma trận Hiệu lực (Actuator Effectiveness Matrix)

$$\begin{bmatrix} T \\ \tau_x \\ \tau_y \\ \tau_z \end{bmatrix} = \mathbf{B} \begin{bmatrix} f_1 \\ f_2 \\ f_3 \\ f_4 \end{bmatrix} = \begin{bmatrix} 1 & 1 & 1 & 1 \\ -d_y & -d_y & d_y & d_y \\ d_x & -d_x & -d_x & d_x \\ -c & c & -c & c \end{bmatrix} \begin{bmatrix} f_1 \\ f_2 \\ f_3 \\ f_4 \end{bmatrix}$$

Để tìm lực đẩy cần thiết cho từng động cơ từ yêu cầu $[T, \tau_x, \tau_y, \tau_z]^T$, ta nhân với ma trận nghịch đảo $\mathbf{B}^{-1}$:

$$\begin{bmatrix} f_1 \\ f_2 \\ f_3 \\ f_4 \end{bmatrix} = \begin{bmatrix} 0.25 & -\frac{1}{4 d_y} & \frac{1}{4 d_x} & -\frac{1}{4 c} \\ 0.25 & -\frac{1}{4 d_y} & -\frac{1}{4 d_x} & \frac{1}{4 c} \\ 0.25 & \frac{1}{4 d_y} & -\frac{1}{4 d_x} & -\frac{1}{4 c} \\ 0.25 & \frac{1}{4 d_y} & \frac{1}{4 d_x} & \frac{1}{4 c} \end{bmatrix} \begin{bmatrix} T \\ \tau_x \\ \tau_y \\ \tau_z \end{bmatrix}$$

Quy chuẩn hóa về tín hiệu điều khiển không thứ nguyên ($[-1.0, 1.0]$ hoặc $[0.0, 1.0]$):
- $u_1 = \text{Throttle} - \text{Roll} + \text{Pitch} - \text{Yaw}$
- $u_2 = \text{Throttle} - \text{Roll} - \text{Pitch} + \text{Yaw}$
- $u_3 = \text{Throttle} + \text{Roll} - \text{Pitch} - \text{Yaw}$
- $u_4 = \text{Throttle} + \text{Roll} + \text{Pitch} + \text{Yaw}$

*(Chú ý: Dấu cộng trừ phụ thuộc vào quy ước chiều dương các trục và thứ tự đánh số motor trong từng dự án).*

---

## 2. Vấn Đề Bão Hòa Động Cơ (Motor Saturation) & Hiện Tượng Xén Cụt (Clipping)

Động cơ vật lý luôn có giới hạn hoạt động:
$$u_i \in [u_{min}, u_{max}] \quad (\text{thường là } [0, 1] \text{ hoặc PWM } [1000\mu s, 2000\mu s])$$

### Tình huống nguy hiểm 1: Đẩy ga cao (High Throttle Saturation)
- Giả sử Throttle = $0.9$ ($90\%$). Bộ điều khiển góc yêu cầu Roll = $+0.2$ để cân bằng chống gió.
- Theo công thức cơ bản:
  - $u_3 = 0.9 + 0.2 = 1.1$ (Vượt quá $1.0$!)
  - $u_1 = 0.9 - 0.2 = 0.7$
- **Hậu quả nếu xén cụt đơn giản (`clamp`)**:
  - $u_3$ bị gọt về $1.0$.
  - Chênh lệch giữa hai động cơ chỉ còn $1.0 - 0.7 = 0.3$ (thay vì $0.4$ như bộ điều khiển mong muốn).
  - Tệ hơn nữa, nếu cả Roll và Pitch cùng yêu cầu lớn, nhiều motor bị bão hòa ở $1.0$, **mô-men xoay thực tế bị méo mó hoàn toàn**, drone không thể nghiêng đúng góc và bắt đầu trôi dạt mất kiểm soát.

### Tình huống nguy hiểm 2: Hạ ga về 0 (Zero Throttle / Low Throttle Inverted Flight)
- Khi phi công muốn lộn nhào (flip/roll) hoặc hạ độ cao nhanh, cần ga (Throttle) kéo về $0\%$.
- Lúc này $u_i = 0 \pm \text{Roll} \pm \text{Pitch}$.
- Một nửa số động cơ sẽ tính ra giá trị âm ($< 0$) và bị kẹp về $0$ (dừng quay).
- **Hậu quả**: Drone mất hoàn toàn lực điều khiển (Control Authority), rơi tự do như một viên đá và không thể lấy lại thăng bằng!

---

## 3. So Sánh 5 Thuật Toán Mixer Trong Các Dự Án

### Thuật toán 1: Direct Clamping (Cơ bản nhất)
- **Áp dụng trong**: [`Arduino-Quadcopter`](../other_git_repo/Arduino-Quadcopter/Quadcopter/Control.ino#L10-L22), [`MonteQuadcopter`](../other_git_repo/MonteQuadcopter/lib/Quadcopter/Quadcopter.cpp#L86-L99), [`Quadcopter` (STM32)](../other_git_repo/Quadcopter/Application/Algorithm/PID.c#L232-L235).
- **Mã nguồn (STM32)**:
  ```c
  motor1 = limit(expMode - pidRoll - pidPitch - pidYaw + pidThr, 1000, 2000);
  motor2 = limit(expMode - pidRoll + pidPitch + pidYaw + pidThr, 1000, 2000);
  motor3 = limit(expMode + pidRoll + pidPitch - pidYaw + pidThr, 1000, 2000);
  motor4 = limit(expMode + pidRoll - pidPitch + pidYaw + pidThr, 1000, 2000);
  ```
- **Đặc điểm**: Chỉ dùng hàm `limit()` hoặc `constrain()`.
- **Đánh giá**: Dễ viết nhưng nguy hiểm, dễ rơi máy bay khi gặp gió lớn hoặc thao tác gậy mạnh.

---

### Thuật toán 2: Attitude Prioritization via Capping (Crazyflie)
- **Áp dụng trong**: [`crazyflie-firmware/src/modules/src/power_distribution_quadrotor.c`](../other_git_repo/crazyflie-firmware/src/modules/src/power_distribution_quadrotor.c#L160-L190).
- **Nguyên lý cốt lõi**: **"Hy sinh một chút độ cao chứ nhất quyết không để mất góc nghiêng!"**
- **Thuật toán**:
  1. Tính toán giá trị xung cho 4 động cơ mà không kẹp giới hạn (`motorThrustUncapped`).
  2. Tìm giá trị động cơ cao nhất: $u_{max} = \max(u_1, u_2, u_3, u_4)$.
  3. Nếu $u_{max} > \text{UINT16\_MAX}$ (bão hòa trên):
     $$\text{reduction} = u_{max} - \text{UINT16\_MAX}$$
     **Trừ đều lượng thừa này vào TẤT CẢ 4 động cơ**:
     $$u_i \leftarrow u_i - \text{reduction} \quad (\forall i)$$
- **Mã nguồn thực tế**:
  ```c
  bool powerDistributionCap(const motors_thrust_uncapped_t* uncapped, motors_thrust_pwm_t* motorPwm)
  {
      int32_t highestThrustFound = 0;
      for (int i = 0; i < 4; i++) {
          if (uncapped->list[i] > highestThrustFound)
              highestThrustFound = uncapped->list[i];
      }

      int32_t reduction = 0;
      if (highestThrustFound > UINT16_MAX) {
          reduction = highestThrustFound - UINT16_MAX;
      }

      for (int i = 0; i < 4; i++) {
          int32_t thrustCappedUpper = uncapped->list[i] - reduction;
          motorPwm->list[i] = capMinThrust(thrustCappedUpper, idleThrust);
      }
  }
  ```
- **Ý nghĩa**: Bằng cách trừ đều tất cả các motor một lượng `reduction`, hiệu số lực $(u_A - u_B)$ (chính là mô-men xoay Roll, Pitch, Yaw) được **bảo toàn nguyên vẹn $100\%$**! Drone có thể bị tụt độ cao một chút trong tích tắc, nhưng giữ được thăng bằng tuyệt đối.

---

### Thuật toán 3: Dynamic Shifting & Airmode (Betaflight)
- **Áp dụng trong**: [`betaflight/src/main/flight/mixer.c`](../other_git_repo/betaflight/src/main/flight/mixer.c#L566-L599) & [`mixTable`](../other_git_repo/betaflight/src/main/flight/mixer.c#L686-L869).
- **Mục tiêu**: Đem lại khả năng nhào lộn tự do hoàn hảo ngay cả khi tay ga kéo sát sàn ($0\%$ ga).
- **Thuật toán 3 bước**:
  1. **Bước 1: Tính toán mô-men thuần túy (Roll, Pitch, Yaw Mix)** cho từng motor mà chưa cộng Throttle:
     $$\text{motorMix}[i] = \tau_{roll} \cdot M_{roll}[i] + \tau_{pitch} \cdot M_{pitch}[i] + \tau_{yaw} \cdot M_{yaw}[i]$$
  2. **Bước 2: Đo độ rộng dải điều khiển (Range check)**:
     Tìm $\text{mix}_{max} = \max(\text{motorMix})$ và $\text{mix}_{min} = \min(\text{motorMix})$.
     $$\text{motorMixRange} = \text{mix}_{max} - \text{mix}_{min}$$
     Nếu $\text{motorMixRange} > 1.0$ (tổng yêu cầu mô-men vượt quá $100\%$ công suất motor):
     $$\text{factor} = \frac{1.0}{\text{motorMixRange}}, \quad \text{motorMix}[i] \leftarrow \text{motorMix}[i] \times \text{factor}$$
  3. **Bước 3: Dời gốc ga động (Dynamic Throttle Shifting - Cốt lõi của Airmode)**:
     Thay vì giữ nguyên vị trí ga của phi công, thuật toán ép vị trí ga phải nằm trong khoảng an toàn để **cả đỉnh trên và đáy dưới đều không bị xén cụt**:
     $$\text{Throttle} = \text{constrain}\Big(\text{Throttle}, \; -\text{mix}_{min}, \; 1.0 - \text{mix}_{max}\Big)$$
     - *Ví dụ*: Khi phi công kéo ga về $0.0$, nhưng đang ra lệnh Roll cực mạnh khiến $\text{mix}_{min} = -0.3$ và $\text{mix}_{max} = +0.3$.
     - Thuật toán sẽ tự động đẩy Throttle lên: $\text{Throttle} = \max(0.0, -(-0.3)) = 0.3$!
     - Nhờ đó, 2 động cơ quay ở mức $0.6$, 2 động cơ quay ở mức $0.0$. Drone vẫn xoay cực gắt mà không bị chết máy!
- **Mã nguồn Betaflight**:
  ```c
  // betaflight/src/main/flight/mixer.c
  const float motorMixNormalizationFactor = motorMixRange > 1.0f ? 1.0f / motorMixRange : 1.0f;
  for (int i = 0; i < mixerRuntime.motorCount; i++) {
      motorMix[i] *= motorMixNormalizationFactor;
  }
  const float normalizedMotorMixMin = motorMixMin * motorMixNormalizationFactor;
  const float normalizedMotorMixMax = motorMixMax * motorMixNormalizationFactor;

  // Dời ga để tránh clipping
  throttle = constrainf(throttle, -normalizedMotorMixMin, 1.0f - normalizedMotorMixMax);
  ```

---

### Thuật toán 4: Sequential Desaturation (PX4-Autopilot)
- **Áp dụng trong**: [`ControlAllocationSequentialDesaturation.cpp`](../other_git_repo/PX4-Autopilot/src/lib/control_allocation/control_allocation/ControlAllocationSequentialDesaturation.cpp#L44-L228).
- **Nguyên lý phân cấp ưu tiên nghiêm ngặt (Strict Hierarchy)**:
  1. **Ưu tiên 1 (Tối cao)**: Mô-men Roll và Pitch (Đảm bảo drone không bị lật).
  2. **Ưu tiên 2**: Lực đẩy trục Z (Duy trì độ cao).
  3. **Ưu tiên 3 (Thấp nhất)**: Mô-men xoay đầu Yaw (Xoay đầu lệch vài độ không làm rơi máy bay, nhưng mất Roll/Pitch là rơi ngay).
- **Quy trình thực thi**:
  ```mermaid
  flowchart TD
      STEP1["1. Hòa trộn Roll + Pitch + Thrust_Z"] --> CHECK1{"Có motor nào bão hòa?"}
      CHECK1 -- Có --> DESAT_THR["Giải bão hòa bằng vector Thrust Z (Tăng/giảm collective thrust)"]
      CHECK1 -- Không --> STEP2["2. Hòa trộn thêm Yaw"]
      DESAT_THR --> STEP2
      STEP2 --> CHECK2{"Yaw làm motor bão hòa?"}
      CHECK2 -- Có --> DESAT_YAW["Xén bớt Yaw (Hy sinh tốc độ xoay Yaw để bảo vệ Roll/Pitch/Thrust)"]
      CHECK2 -- Không --> OUT["Xuất tín hiệu ra Actuator Motors"]
      DESAT_YAW --> OUT
  ```
- **Hàm tính hệ số giải bão hòa `computeDesaturationGain`**:
  PX4 tính toán chính xác hệ số tỉ lệ $k$ cần dịch chuyển theo vector giải bão hòa để đưa motor bị lố về sát biên ($u_{min}$ hoặc $u_{max}$) một cách tối ưu.

---

### Thuật toán 5: Headroom Dynamic Allocation & Thrust Linearization (ArduPilot)
- **Áp dụng trong**: [`AP_MotorsMatrix.cpp`](../other_git_repo/ardupilot/libraries/AP_Motors/AP_MotorsMatrix.cpp#L213-L404) & [`AP_Motors_Thrust_Linearization.cpp`](../other_git_repo/ardupilot/libraries/AP_Motors/AP_Motors_Thrust_Linearization.cpp).
- **Hai tính năng nổi trội hàng đầu**:

#### A. Dự trữ khoảng trống cho Yaw (`MOT_YAW_HEADROOM`)
- Trên quadrotor, trục Yaw có thẩm quyền lực (control authority) rất yếu vì chỉ dựa trên lực ma sát xoắn cánh quạt.
- Nếu Roll/Pitch và Throttle chiếm hết $100\%$ dải quay motor, drone sẽ hoàn toàn "liệt" điều khiển Yaw.
- ArduPilot luôn tính toán trước khoảng trống ga tốt nhất (`throttle_thrust_best_rpy`), và khóa cứng một lượng tối thiểu `yaw_allowed_min` (mặc định $20\%-35\%$) dành riêng cho trục Yaw.

#### B. Tuyến tính hóa lực đẩy (Thrust Linearization / Mot Thst Expo)
- **Thực tế vật lý**: Lực đẩy cánh quạt tỉ lệ với **bình phương vòng tua** ($F \propto \text{RPM}^2$). Tốc độ vòng tua lại tỉ lệ với điện áp hiệu dụng qua ESC ($V_{eff} \propto \text{PWM/Duty}$).
- Do đó, đồ thị Lực đẩy theo Tín hiệu điều khiển là một đường cong parabol (phi tuyến). Ở ga thấp, nhích $10\%$ ga sinh ra rất ít lực; ở ga cao, nhích $10\%$ ga sinh ra lực cực lớn $\implies$ Hệ số $K_p, K_d$ bị biến thiên theo tay ga, dễ gây tự kích dao động ở ga cao!
- **Công thức bù cong bậc 2 của ArduPilot & Betaflight**:
  $$u_{linearized} = u \cdot \big[1 + e(1 - u)\big]$$
  hoặc khai triển Taylor bậc 2:
  $$u_{out} = u \cdot \big[1 + e (1 - u)(1 + e (1 - 2u))\big]$$
  *(với $e$ là hệ số cong `MOT_THST_EXPO`, thường từ $0.55$ cho cánh $5''$ đến $0.75$ cho cánh $20''$)*.
- Nhờ hàm này, đáp ứng lực đẩy trở nên hoàn toàn tuyến tính với lệnh điều khiển từ PID!

---

## 4. Bảng So Sánh Tóm Tắt Các Thuật Toán Mixer

| Thuật toán Mixer | Dự án đại diện | Cách xử lý khi chạm trần ($100\%$ ga) | Cách xử lý khi chạm sàn ($0\%$ ga) | Độ phức tạp tính toán |
| :--- | :--- | :--- | :--- | :--- |
| **Direct Clamping** | Arduino, STM32 uCOS-II | Kẹp cứng về $100\%$, làm méo mô-men | Kẹp cứng về $0\%$, mất lực xoay | Cực thấp ($\mathcal{O}(1)$) |
| **Attitude Prioritized Capping** | Crazyflie | Trừ đều tất cả motor, giữ $100\%$ Roll/Pitch/Yaw | Kẹp theo mức `idleThrust` | Rất thấp ($\mathcal{O}(N)$) |
| **Dynamic Throttle Shift (Airmode)** | Betaflight | Chuẩn hóa tỉ lệ mô-men, dời ga xuống | Tự động nâng ga sàn lên để giữ nguyên mô-men | Trung bình |
| **Sequential Desaturation** | PX4-Autopilot | Ưu tiên Roll/Pitch $\to$ Chỉnh Thrust Z $\to$ Xén bớt Yaw | Chỉ giảm thrust (nếu tắt airmode), xén bớt Yaw | Cao (Ma trận $\mathbf{B}$, 2 lần lặp) |
| **Dynamic Headroom Matrix** | ArduPilot | Giới hạn Roll/Pitch bằng scaling, ép trần ga | Dành riêng % headroom cho Yaw, hòa trộn `_throttle_rpy_mix` | Rất cao (Tích hợp bù sụt áp pin) |
