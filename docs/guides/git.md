# Git cho đồ án

Chạy các lệnh dưới đây từ thư mục gốc `drone/`. Repository đã có Git, không cần chạy lại `git init`.

## Thiết lập danh tính

Kiểm tra thông tin đang dùng:

```sh
git config user.name
git config user.email
```

Nếu chưa có hoặc muốn dùng thông tin riêng cho đồ án, thay nội dung ví dụ rồi chạy:

```sh
git config user.name "Tên của bạn"
git config user.email "ban@example.com"
```

Kích hoạt framework `pre-commit` cho bản clone hiện tại:

```sh
uv tool install pre-commit
pre-commit install --install-hooks
```

Nếu trước đây đã dùng hook trong `.githooks`, chạy `git config --local --unset core.hooksPath` trước khi cài hook mới. Các formatter chỉ xử lý tệp đã stage và tự bảo vệ phần chưa stage. Khi formatter sửa tệp, xem diff, stage phần mong muốn rồi commit lại. Xem [các lệnh format/lint và yêu cầu công cụ](../../tools/README.md).

## Quy trình mỗi lần làm việc

Xem trạng thái trước khi sửa:

```sh
git status
```

Sau khi sửa, xem nội dung thay đổi. Tệp mới chưa được thêm vào Git cần mở để kiểm tra riêng:

```sh
git diff
```

Thêm các tệp thuộc cùng một công việc và kiểm tra trước khi commit. Ví dụ khi sửa README:

```sh
git add README.md
git diff --cached
git commit -m "docs: cập nhật hướng dẫn dự án"
```

Mỗi commit nên ghi lại một việc rõ ràng. Khuyến khích dùng [Conventional Commits](#conventional-commits) để thông điệp nhất quán, dễ đọc lịch sử; đây là quy ước viết thông điệp, không phải yêu cầu bắt buộc của Git.

Xem lịch sử:

```sh
git log --oneline -10
```

Nếu thêm nhầm tệp vào vùng chuẩn bị commit, bỏ chọn tệp đó bằng lệnh sau; nội dung tệp vẫn được giữ lại:

```sh
git restore --staged README.md
```

## Conventional Commits

Conventional Commits giúp người đọc nhận ra mục đích của thay đổi ngay từ dòng đầu. Các công cụ cũng có thể dựa vào đó để tạo changelog hoặc đề xuất phiên bản phát hành.

### Cấu trúc thông điệp

```text
type(scope): mô tả ngắn gọn

Phần giải thích thêm (body), nếu cần.

Footer, nếu cần.
```

- `type`: loại thay đổi, ví dụ `feat`, `fix`, `docs`.
- `scope`: phần bị ảnh hưởng, ví dụ `imu`, `pid`, `sim`, `report`; có thể bỏ cả dấu ngoặc khi không cần, như `docs: cập nhật hướng dẫn Git`.
- Mô tả: nêu cụ thể việc đã làm, có một dấu cách sau `:`. Có thể viết tiếng Việt như các ví dụ trong tài liệu này.
- Body: giải thích lý do, cách xử lý hoặc tác động khi dòng đầu chưa đủ; cách dòng đầu bằng một dòng trống.
- Footer: thông tin bổ sung như `Refs: #12` hoặc `BREAKING CHANGE: ...`; cách phần trước bằng một dòng trống.

### Chọn loại thay đổi

| Type       | Dùng khi                                         | Ví dụ                                              |
| ---------- | ------------------------------------------------ | -------------------------------------------------- |
| `feat`     | Thêm chức năng mới                               | `feat(imu): thêm hiệu chuẩn cảm biến`              |
| `fix`      | Sửa lỗi hoạt động                                | `fix(pid): giới hạn tích phân khi động cơ bão hòa` |
| `docs`     | Chỉ sửa tài liệu                                 | `docs(git): bổ sung Conventional Commits`          |
| `style`    | Chỉ đổi định dạng mã, không đổi hành vi          | `style: format mã nguồn theo cấu hình dự án`       |
| `refactor` | Tổ chức lại mã, không thêm chức năng hay sửa lỗi | `refactor(imu): tách hàm đọc cảm biến`             |
| `perf`     | Cải thiện hiệu năng                              | `perf(sim): giảm số lần cấp phát trong vòng lặp`   |
| `test`     | Thêm hoặc sửa kiểm thử                           | `test(pid): thêm kiểm thử giới hạn đầu ra`         |
| `build`    | Sửa hệ thống build hoặc phụ thuộc                | `build: cập nhật cấu hình CMake`                   |
| `ci`       | Sửa quy trình tích hợp liên tục                  | `ci: chạy kiểm thử khi mở pull request`            |
| `chore`    | Công việc bảo trì khác                           | `chore: cập nhật .gitignore`                       |

`feat` và `fix` có ý nghĩa được quy định trong đặc tả. Các type còn lại là quy ước phổ biến; nhóm nên thống nhất cách dùng. Không dùng `chore` thay cho mọi loại thay đổi.

Ví dụ commit ngắn:

```sh
git commit -m "docs(git): bổ sung Conventional Commits"
```

Ví dụ có body: mỗi tùy chọn `-m` tạo một đoạn riêng trong thông điệp.

```sh
git commit -m "fix(pid): giới hạn tích phân khi động cơ bão hòa" \
  -m "Ngăn tích phân tiếp tục tăng khi đầu ra đã chạm giới hạn, giúp bộ điều khiển phục hồi nhanh hơn."
```

### Đánh dấu thay đổi không tương thích

Nếu thay đổi buộc mã gọi, cấu hình hoặc người dùng hiện tại phải điều chỉnh, đánh dấu **breaking change** bằng `!` ngay trước `:` hoặc footer `BREAKING CHANGE:` viết hoa. Có thể dùng cả hai để vừa nhận diện nhanh vừa giải thích cách chuyển đổi:

```text
feat(config)!: dùng radian cho góc đặt

Thống nhất đơn vị góc giữa cấu hình và bộ điều khiển.

BREAKING CHANGE: góc đặt trong cấu hình chuyển từ độ sang radian; đổi giá trị cũ theo công thức rad = deg * pi / 180.
```

Breaking change có thể thuộc bất kỳ type nào. Chỉ dùng `!` khi có thay đổi không tương thích, không phải để nhấn mạnh một commit lớn hoặc quan trọng.

Nếu dự án phát hành theo Semantic Versioning (`MAJOR.MINOR.PATCH`), `fix` tương ứng tăng PATCH, `feat` tăng MINOR, và breaking change tăng MAJOR. Git không tự tăng phiên bản hay tạo changelog; cần quy trình hoặc công cụ riêng.

### Thói quen nên dùng

- Giữ type và scope viết thường để nhất quán trong đồ án.
- Viết mô tả cụ thể: `fix(imu): sửa dấu trục Z` dễ hiểu hơn `fix: sửa lỗi` hay `update`.
- Tách những việc độc lập thành các commit riêng, ví dụ sửa lỗi điều khiển và cập nhật hướng dẫn cài đặt.
- Chọn type theo mục đích chính: sửa lỗi vẫn là `fix` dù có thêm kiểm thử đi kèm.

Tham khảo: [đặc tả Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/).

## Đồng bộ GitHub hoặc GitLab

Kiểm tra remote và tên nhánh hiện tại:

```sh
git remote -v
git branch --show-current
```

Nếu chưa có `origin`, tạo một repository trống trên dịch vụ bạn dùng rồi thay URL ví dụ:

```sh
git remote add origin https://github.com/USERNAME/REPOSITORY.git
```

Lần đẩy đầu tiên, thay `TEN_NHANH` bằng tên nhánh vừa kiểm tra:

```sh
git push -u origin TEN_NHANH
```

Sau khi thiết lập liên kết nhánh, dùng `git pull --ff-only` trước khi bắt đầu sửa khi thư mục làm việc sạch, và `git push` sau khi commit. Nếu pull báo hai phía đã phân kỳ, kiểm tra lịch sử trước khi xử lý.

## Những gì cần lưu

- Lưu mã nguồn, cấu hình, tài liệu, nguồn LaTeX, hình cần thiết và kết quả nhỏ phục vụ báo cáo.
- `.gitignore` bỏ qua sản phẩm build, tệp LaTeX tạm và dữ liệu thô trong `experiments/raw/`, `experiments/processed/`.
- PDF tài liệu và hình PDF vẫn được phép lưu; PDF báo cáo xuất ra nên đặt trong `report/build/`.
- Repo tham khảo trong `docs/external/repos/` hiện chỉ nằm trên máy. Ghi URL và commit đã tham khảo vào ghi chú nếu cần tái tìm lại.
- Git không lưu thư mục rỗng, vì vậy các thư mục mới có `.gitkeep`.

Tài liệu tham khảo: [Git tutorial chính thức](https://git-scm.com/docs/gittutorial).
