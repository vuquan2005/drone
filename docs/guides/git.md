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

Kích hoạt Git pre-commit hook (tự động format trước khi commit):

```sh
git config core.hooksPath .githooks
```

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

Mỗi commit nên ghi lại một việc rõ ràng. Có thể dùng các tiền tố `docs:` cho tài liệu, `feat:` cho chức năng mới và `fix:` cho sửa lỗi; không bắt buộc.

Xem lịch sử:

```sh
git log --oneline -10
```

Nếu thêm nhầm tệp vào vùng chuẩn bị commit, bỏ chọn tệp đó bằng lệnh sau; nội dung tệp vẫn được giữ lại:

```sh
git restore --staged README.md
```

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
