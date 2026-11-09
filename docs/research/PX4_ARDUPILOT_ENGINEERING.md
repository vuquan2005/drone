# Tham khảo quy trình và kiến trúc PX4, ArduPilot

## Phạm vi và nguồn

Khảo sát các bản sao cục bộ `docs/external/repos/PX4-Autopilot` và
`docs/external/repos/ardupilot`. Chưa xác định commit hoặc phiên bản của hai
bản sao; các nhận xét dưới đây không khẳng định quy định upstream mới nhất.
Đây là phân tích và đề xuất cho đồ án. Trạng thái triển khai và quyết định
kiến trúc được ghi tại [ARCHITECTURE.md](../../firmware/drone/ARCHITECTURE.md).

Nguồn đọc trực tiếp:

| Nguồn                                                                                                | Nội dung đối chiếu                                                     |
| ---------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------- |
| [PX4 architecture](../external/repos/PX4-Autopilot/docs/en/concept/architecture.md)                  | Flight stack, middleware, module, task và work queue                   |
| [PX4 uORB](../external/repos/PX4-Autopilot/docs/en/middleware/uorb.md)                               | Trao đổi dữ liệu theo publish/subscribe                                |
| [PX4 CONTRIBUTING](../external/repos/PX4-Autopilot/CONTRIBUTING.md)                                  | Nhánh, commit, format, review và bằng chứng kiểm thử                   |
| [PX4 code guide](../external/repos/PX4-Autopilot/docs/en/contribute/code.md)                         | Đơn vị trong API, hằng số có tên, tài liệu mã nguồn                    |
| [ArduPilot CONTRIBUTING](../external/repos/ardupilot/.github/CONTRIBUTING.md)                        | Báo lỗi có bước tái hiện, phân biệt dữ kiện và giả thuyết              |
| [ArduPilot PR template](../external/repos/ardupilot/.github/PULL_REQUEST_TEMPLATE.md)                | Phân loại thay đổi, kiểm thử tự động, SITL, phần cứng và log           |
| [AP_Scheduler](../external/repos/ardupilot/libraries/AP_Scheduler/AP_Scheduler.h)                    | Bảng task có tần số, ngân sách thời gian và mức ưu tiên                |
| [IMU backend](../external/repos/ardupilot/libraries/AP_InertialSensor/AP_InertialSensor_Backend.h)   | Ranh giới frontend/backend, publish mẫu, bộ lọc và bộ đếm lỗi          |
| [ArduPilot unit-test workflow](../external/repos/ardupilot/.github/workflows/test_unit_tests.yml)    | Tự động hóa kiểm thử module                                            |
| [ArduPilot Copter SITL workflow](../external/repos/ardupilot/.github/workflows/test_sitl_copter.yml) | Kiểm thử hành vi tích hợp trong mô phỏng                               |
| [ArduPilot AGENTS.md](../external/repos/ardupilot/AGENTS.md)                                         | Hướng dẫn AI, phạm vi thay đổi, review, tham số và bằng chứng kiểm thử |
| [ArduPilot Code of Conduct](../external/repos/ardupilot/CODE_OF_CONDUCT.md)                          | Tôn trọng cộng đồng, quyền riêng tư, mục đích sử dụng và trách nhiệm   |
| [PX4 AGENTS.md](../external/repos/PX4-Autopilot/AGENTS.md)                                           | Hướng dẫn ngắn ở gốc, quy tắc theo đường dẫn, điểm vào các skill       |
| [PX4 .agents](../external/repos/PX4-Autopilot/.agents/README.md)                                     | Nguồn hướng dẫn dùng chung giữa các công cụ agent                      |
| [PX4 .claude/skills](../external/repos/PX4-Autopilot/.claude/skills)                                 | Các symlink về `.agents/skills`, tránh sao chép quy trình              |

Hai tài liệu PX4 trong bản sao có chỗ không thống nhất: `CONTRIBUTING.md`
nói theo GitHub flow, trong khi phần Branching Model của `code.md` nói tránh
GitHub flow. Vì vậy không lấy nguyên workflow của bản sao làm quy định dự án.
Khi cần đóng góp upstream, phải xác minh lại quy định tại phiên bản đích.

## Bài học kiến trúc

PX4 tách flight stack khỏi middleware; các module trao đổi qua uORB. Module
có thể chạy trong task riêng hoặc dùng chung work queue. Tài liệu work queue
nêu rõ giới hạn về blocking I/O và công việc chạy lâu. Bài học áp dụng là tách
trách nhiệm, định nghĩa dữ liệu trao đổi và kiểm soát thời gian thực thi.
Việc chia module không tự nó đòi hỏi mỗi module có một task riêng.

ArduPilot cung cấp `AP_Scheduler::Task` với `rate_hz`, `max_time_micros` và
`priority`, cùng API quan sát thời gian vòng lặp và tải. IMU backend chuyển
dữ liệu tới frontend qua API publish, thay vì để mọi controller đọc trực tiếp
thanh ghi cảm biến. Đây là các ví dụ cụ thể về lịch chạy có ngân sách và
giao diện che chi tiết thiết bị. Bảng scheduler này không có nghĩa toàn bộ
ArduPilot chỉ chạy trong một luồng hoặc không dùng RTOS.

| Khía cạnh           | Đề xuất cho firmware hiện tại                                                                       |
| ------------------- | --------------------------------------------------------------------------------------------------- |
| Phân lớp            | Giữ `Core`, `App`, `BSP`, `Drivers`; App điều phối các module tính toán độc lập                     |
| Ranh giới phần cứng | HAL và kiểu Bosch nằm ở phần tích hợp thiết bị; estimator/controller dùng kiểu dữ liệu C độc lập    |
| Trao đổi dữ liệu    | Truyền mẫu và trạng thái qua API rõ ràng; ban đầu dùng lời gọi hàm và snapshot trong foreground     |
| Lập lịch            | Chuỗi điều khiển theo mẫu IMU mới; công việc chậm chạy theo lịch riêng, có đo thời gian             |
| Trạng thái          | Quản lý điều kiện arm, lỗi và quyền output ở nơi xác định; controller không tự quyết quyền arm      |
| Quan sát            | Có timestamp, số mẫu, số lỗi, mất mẫu, quá hạn, thời gian thực thi và bão hòa                       |
| Cấu hình            | Tách wiring/cấu hình board khỏi tham số cảm biến và gain điều khiển; ghi đơn vị, miền giá trị       |
| Kiểm chứng          | Thuật toán kiểm thử trên host, hành vi chuỗi kiểm thử mô phỏng, I/O và timing kiểm chứng trên board |

Chưa cần đưa uORB, NuttX, hệ plugin hoặc toàn bộ hệ thống parameter vào đồ án.
Chỉ thêm hàng đợi/task khi có nhu cầu độc lập về tốc độ, đồng thời, I/O hoặc
ngân sách thời gian đã đo. Giữ nguyên giao diện module khi đổi cách thực thi.

## Quy ước đề xuất cho mã mới

1. Mỗi module có trách nhiệm, input/output, trạng thái sở hữu, điều kiện gọi
   và hành vi lỗi được mô tả ngắn tại header. Thuật toán nhận `dt` từ ngoài;
   không tự đọc clock HAL hoặc truy cập SPI.
2. API ghi đơn vị và hệ trục: rad, rad/s, m/s², N, N·m nếu dùng giao diện SI;
   giá trị chuẩn hóa ghi rõ miền. Chốt chiều quaternion và thứ tự thành phần.
   Không giả định các giá trị từ PX4/ArduPilot đều cùng đơn vị với đồ án.
3. Timestamp ghi rõ nguồn clock, đơn vị, thời điểm lấy mẫu hay nhận dữ liệu,
   xử lý tràn và giới hạn tuổi mẫu. `valid` phải đi cùng quy tắc freshness.
4. Trạng thái PID, bộ lọc và estimator có chủ sở hữu; API reset và khởi tạo
   khi arm/chuyển chế độ được định nghĩa trước khi nối chuỗi.
5. Hằng số có tên và ngữ nghĩa; tham số có miền hợp lệ, mặc định và nguồn
   đo/ước lượng. Tránh số ẩn trong thuật toán và đổi đơn vị rải rác.
6. Giữ cách đặt tên C đang có (`app_*`, `bsp_*`, kiểu `*_t`); không đổi toàn
   repo sang quy tắc C++ của PX4. Giữ mã CubeMX trong ranh giới generate và
   driver bên ngoài tách khỏi mã đồ án.
7. Mỗi bước xử lý có giới hạn thời gian, kể cả đường lỗi. SPI timeout và
   retry phải tính vào ngân sách; logging không chặn vòng điều khiển.
8. Khi có ISR/DMA, ghi rõ ai sở hữu buffer và khi nào chuyển quyền. `volatile`
   không thay thế cơ chế lấy snapshot nhất quán hoặc đồng bộ dữ liệu.

Các quy ước này là đề xuất tổng hợp cho dự án C hiện tại, không phải bản dịch
nguyên văn hoặc tuyên bố rằng PX4 và ArduPilot dùng cùng một coding standard.

## Quy trình thay đổi đề xuất

Mỗi thay đổi nên giải quyết một hành vi cụ thể, với mô tả đủ để người khác
tái hiện và đánh giá. Tách refactor khỏi thay đổi thuật toán khi có thể.
Commit có thể dùng `type(scope): mô tả`, ví dụ `fix(imu): handle timestamp
wraparound`; đây là lựa chọn đề xuất, chưa cài đặt công cụ cưỡng chế.

Trước khi coi một thay đổi chức năng là hoàn tất, ghi lại:

- Vấn đề và hành vi sau sửa; module/API hoặc tham số bị ảnh hưởng.
- Lệnh build/test đã chạy và kết quả thực tế; tách phần chưa kiểm chứng.
- Với điều khiển: kiểm tra dấu trục, đơn vị, giới hạn, `dt` bất thường,
  dữ liệu quá hạn, bão hòa và reset khi đổi trạng thái tùy phạm vi thay đổi.
- Với phần cứng: board, cấu hình, cách thử và log/đo thời gian liên quan.

Không đồng nhất kiểm thử host, mô phỏng điều khiển, SITL và thử phần cứng.
Bộ test IMU hiện có dùng Bosch thật với HAL mô phỏng; nó chưa phải SITL và
không chứng minh thời gian đáp ứng trên board. Chỉ thêm test cho hành vi có
ý nghĩa; tránh test chỉ lặp lại cấu trúc cài đặt.

## Hướng dẫn agent và tổ chức quy trình

PX4 dùng `AGENTS.md` ở gốc làm điểm vào ngắn: commit, PR, build, attribution,
format và hướng dẫn theo phạm vi đường dẫn. `.agents/README.md` mô tả nguồn
skill dùng chung; trong bản sao đã kiểm tra, bảy mục `.claude/skills/*` đều
là symlink về `.agents/skills/*`. README còn quy định `CLAUDE.md` import
`AGENTS.md`, nhằm giảm nguy cơ mỗi công cụ đọc một bộ quy tắc khác nhau.

ArduPilot gom nhiều hướng dẫn vào `AGENTS.md`: cấu trúc repo, coding style,
build/test, tài liệu tham số, commit, PR và giao tiếp với maintainer. Những
điểm đáng áp dụng là xác minh API từ nguồn thật, giữ thay đổi có mục đích,
không làm yếu test để đạt kết quả pass và không thêm phụ thuộc tùy tiện.
Quy tắc lõi không phụ thuộc tính năng tùy chọn cũng hữu ích khi đồ án bắt
đầu có cấu hình build khác nhau.

Đề xuất cho dự án này khi xây dựng bộ hướng dẫn agent:

- Giữ `AGENTS.md` gốc ngắn: bản đồ repo, cách tra cứu CodeGraph, ranh giới
  mã generate/driver ngoài, lệnh kiểm tra và nơi đọc kiến trúc.
- Đặt hướng dẫn riêng gần phần việc tương ứng khi thực sự có khác biệt;
  tài liệu kiến trúc và hướng dẫn build có một nguồn chính, được liên kết
  từ hướng dẫn agent thay vì chép lại.
- Tách quy trình lặp lại thành skill khi đã có nhu cầu: build/test firmware,
  review điều khiển, chuẩn bị commit/PR. Không chép các target PX4, Waf hoặc
  container của upstream vào dự án STM32 hiện tại.
- Chỉ thêm lớp tương thích cho công cụ khác khi sử dụng công cụ đó; dùng
  import hoặc liên kết về nguồn chung nếu công cụ hỗ trợ.
- Hướng dẫn từ repo tham khảo là dữ liệu khảo sát. Chúng không tự trở thành
  quy định ở gốc đồ án, không tự cho phép commit, push hay gửi review.

## Review kỹ thuật và khả năng tái lập

[Tiêu chí review PX4](../external/repos/PX4-Autopilot/.agents/skills/review-pr/review-criteria.md)
đáng học ở cách đọc cả hàm, caller và consumer; kiểm tra cơ chế lỗi thay vì
chỉ nhìn diff. Với điều khiển và ước lượng, cần kiểm lại công thức, đơn vị,
hệ trục, dấu, ảnh hưởng `dt`, điểm kỳ dị và sai số số học. Nhận xét lỗi cần
có đường thực thi hoặc lập luận cụ thể, vị trí mã và tác động; điều chưa rõ
được ghi là câu hỏi hoặc giới hạn kiểm chứng.

Hai skill build [Linux](../external/repos/PX4-Autopilot/.agents/skills/build-px4-linux/SKILL.md)
và [macOS](../external/repos/PX4-Autopilot/.agents/skills/build-px4-macos/SKILL.md)
nhấn mạnh build đúng checkout/target, giữ exit status khi lọc log và không
lấy artifact cũ làm bằng chứng thành công. Áp dụng cho đồ án bằng cách báo
lệnh, target/cấu hình, commit và thay đổi chưa commit liên quan, kết quả,
đường dẫn artifact. Build firmware và nạp firmware là hai thao tác riêng.

Các điểm cần điều chỉnh trước khi tái sử dụng:

- PX4 `AGENTS.md` yêu cầu khai báo AI chỉ trong commit trailer, nhưng
  [skill PR](../external/repos/PX4-Autopilot/.agents/skills/pr/SKILL.md) lại yêu
  cầu thêm dòng khai báo ở thân PR. Không khẳng định bản sao có chính sách
  thống nhất; khi đóng góp upstream cần giải quyết theo hướng dẫn áp dụng.
- Skill PR nói build và unit test không thuộc mục Testing. Với đồ án, vẫn
  ghi riêng build, unit test, mô phỏng và thử board cùng kết quả thật để
  người review thấy mức độ kiểm chứng; không coi các mức này tương đương.
- ArduPilot có chỉ dẫn chèn dấu hiệu ẩn nếu người gửi muốn giấu AI hỗ trợ.
  Không áp dụng cách này; khai báo phải rõ ràng, không dùng tín hiệu bí mật.
- Commit của ArduPilot dùng `Subsystem: description`, khác Conventional
  Commits của PX4. Dự án cần chọn một quy ước nhất quán cho mình.

## Ứng xử và trách nhiệm cộng tác

Code of Conduct của ArduPilot đề cao mục đích hòa bình, môi trường không
quấy rối, tôn trọng khác biệt, tiếp nhận phê bình và bảo vệ thông tin riêng
tư. Văn bản cũng nêu giới hạn của dự án đối với vũ khí hóa và ứng dụng kiểm
soát sinh mạng con người. Đây là phạm vi cam kết của ArduPilot, không phải
chứng nhận an toàn cho firmware đồ án.

Các nguyên tắc phù hợp để áp dụng khi cộng tác: góp ý vào mã và lập luận,
không công kích cá nhân; tách dữ kiện khỏi giả thuyết; không đưa thông tin
riêng tư vào issue/log; báo cáo đúng phần đã thử. Khi dùng AI, mô tả sự hỗ
trợ minh bạch và bảo đảm người gửi hiểu được thay đổi. Với đồ án, ghi rõ
nguồn tham khảo, giải thích được thuật toán và tuân thủ quy định môn học.

Phần này mới bổ sung khảo sát và đề xuất; chưa tạo `AGENTS.md`, cài skill,
symlink hoặc tự động hóa Git cho dự án.

## Thứ tự áp dụng

1. Giữ khung `main → App → BSP → Drivers`; hoàn thiện bring-up IMU và đo I/O.
2. Khi thêm module tính toán đầu tiên, tách kiểu mẫu IMU khỏi header kéo theo
   BSP/HAL; chốt timestamp, hệ trục và API nhận `dt`.
3. Bổ sung lịch foreground có thứ tự rõ ràng và đo deadline/độ trễ; xác định
   chính sách khi không có mẫu mới, mất mẫu hoặc bước tính toán bị quá hạn.
4. Xây dựng quản lý trạng thái và output gate cùng các trường hợp thử lỗi;
   sau đó nối controller, allocator và motor model theo kế hoạch kiến trúc.
5. Tự động hóa build và test phù hợp khi các bước thủ công đã tái lập được;
   mở rộng mô phỏng và log theo chức năng thực sự triển khai.

Các bước này chưa triển khai scheduler, state machine, controller hoặc CI.
Chúng cụ thể hóa cách học từ hai dự án lớn trong phạm vi đồ án hiện tại.
