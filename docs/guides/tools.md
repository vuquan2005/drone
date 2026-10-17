# Công cụ làm việc

Ở bước chuẩn bị, chỉ cần Git và trình soạn thảo quen dùng. Các công cụ chuyên môn cài khi bắt đầu phần việc tương ứng.

| Khi nào          | Công cụ                         | Dùng để làm gì                                                |
| ---------------- | ------------------------------- | ------------------------------------------------------------- |
| Ngay từ đầu      | Git                             | Lưu lịch sử thay đổi và đồng bộ repository                    |
| Ngay từ đầu      | Trình soạn thảo, ví dụ VS Code  | Sửa Markdown, mã nguồn và tệp cấu hình                        |
| Bắt đầu STM32    | STM32CubeIDE và STM32CubeMX     | Phát triển, cấu hình và debug project STM32 trong `firmware/` |
| Bắt đầu mô phỏng | Gazebo                          | Tạo và chạy mô hình trong `simulation/`                       |
| Bắt đầu báo cáo  | Overleaf hoặc bộ LaTeX trên máy | Viết và biên dịch báo cáo; lưu bản nguồn trong `report/`      |

## Cách dùng với repository

- Mở thư mục gốc `drone/` bằng trình soạn thảo để xem toàn bộ đồ án.
- Khi tạo project STM32, chọn vị trí trong `firmware/`; lưu cả mã nguồn và tệp cấu hình `.ioc` nếu sử dụng CubeMX.
- Khi viết báo cáo bằng Overleaf, tải bản nguồn về `report/` trước khi commit các thay đổi cần lưu.
- Lưu script tự viết vào `tools/`. Ghi cách chạy ngay khi thêm script đầu tiên.
- Lưu mã nguồn và cấu hình vào Git; các sản phẩm build tạm được bỏ qua bằng `.gitignore`.

Chưa cần thiết lập hệ thống build chung ở bước chuẩn bị.

## File Nesting, lint và format trong VS Code

Mở thư mục gốc `drone/` để dùng đầy đủ cấu hình trong `.vscode/`. Mở Extensions, tìm `@recommended` và cài các extension được đề xuất. Khi mở riêng `firmware/drone/`, thư mục này cũng có cấu hình C/C++, JSON, YAML và CMake tương ứng.

| Loại tệp      | Formatter                               | Kiểm tra lỗi/cảnh báo                            |
| ------------- | --------------------------------------- | ------------------------------------------------ |
| C/C++         | ST clangd, dùng `.clang-format`         | Chẩn đoán clangd theo cấu hình build Debug       |
| Markdown      | Prettier                                | markdownlint, dùng `.markdownlint-cli2.jsonc`    |
| JSON/JSONC    | Prettier                                | Kiểm tra cú pháp và schema có sẵn trong VS Code  |
| YAML          | YAML của Red Hat                        | YAML Language Server: cú pháp và schema khi có   |
| CMake         | cmake-format                            | `cmake-lint`, chạy thủ công                      |
| LaTeX, `.sty` | LaTeX Workshop gọi `latexindent`        | LaTeX Workshop gọi ChkTeX khi lưu                |
| BibTeX        | Formatter tích hợp trong LaTeX Workshop | Kiểm tra thêm khi biên dịch bibliography/báo cáo |

Prettier và markdownlint chạy bằng extension, không cần tạo dự án npm. Trên Fedora, cài các chương trình ngoài mà CMake và LaTeX cần:

```bash
sudo dnf install cmakelang texlive-latexindent texlive-chktex
```

Sau khi cài extension hoặc công cụ, chạy `Developer: Reload Window`. Nếu VS Code báo formatter không hợp lệ, kiểm tra extension tương ứng đã được cài và bật trong môi trường đang mở project, kể cả khi dùng Remote SSH.

Nhấn `Shift+Alt+F` để format tệp đang mở. Format khi lưu theo thiết lập cá nhân của VS Code; cấu hình repo không ghi đè lựa chọn này. Prettier dùng `.prettierrc.json` ở gốc repo; CMake dùng `.cmake-format.json`. Khi mở riêng firmware, các công cụ này vẫn tìm cấu hình ở thư mục cha.

Markdownlint và ChkTeX hiện cảnh báo trong Problems (`Ctrl+Shift+M`). CMake lint chạy qua `Tasks: Run Task` → `CMake: lint firmware` khi mở thư mục gốc; kết quả nằm trong Terminal. Có thể chạy từ terminal ở gốc repo:

```bash
cmake-lint firmware/drone/CMakeLists.txt
```

File Nesting gom các tệp cùng thư mục thành nhóm trong Explorer:

- `README.md`: tài liệu phụ, các cấu hình format/lint và `codegraph.json`.
- `.gitignore`: `.gitattributes` và `.gitmodules` nếu có.
- `CMakeLists.txt`: presets và `.clangd`.
- `*.ioc`: `.mxproject` của CubeMX.
- `main.tex`: `metadata.tex` và `latexmkrc`.

`files.exclude` ẩn thư mục `build/`, `cmake-build-*`, `.codegraph/` và cache clangd khỏi Explorer. Đây là cấu hình hiển thị, không xóa tệp hay thay đổi cách Git theo dõi. Muốn xem lại, đổi mục tương ứng thành `false` trong `.vscode/settings.json`. Các sản phẩm LaTeX vẫn được build vào `report/build/` và mở bằng LaTeX Workshop.

File Nesting không gom được các thư mục hoặc các tệp ở hai thư mục khác nhau. Vì vậy không thể gom `Core/Inc/main.h` dưới `Core/Src/main.c` bằng tính năng này.

Không format hàng loạt repo: giữ nguyên phần CubeMX sinh ra, HAL, repo tham khảo và tài liệu tải về. `.prettierignore`, cấu hình markdownlint và các cấu hình clang-format hiện có loại trừ những phần tương ứng; với CMake/LaTeX chỉ gọi format trên tệp tự chỉnh sửa.

## Tài liệu chính thức

- [Cài Git](https://git-scm.com/downloads) và [hướng dẫn Git cơ bản](https://git-scm.com/docs/gittutorial).
- [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html).
- [STM32CubeMX](https://www.st.com/en/development-tools/stm32cubemx.html).
- [Bắt đầu với Gazebo](https://gazebosim.org/docs/latest/getstarted/): chọn hướng dẫn cài đặt phù hợp hệ điều hành trước khi cài.
- [Làm quen LaTeX trên Overleaf](https://www.overleaf.com/learn/latex/Learn_LaTeX_in_30_minutes).
- [File Nesting trong VS Code](https://code.visualstudio.com/updates/v1_67#_explorer-file-nesting).
- [Cấu hình Prettier](https://prettier.io/docs/configuration) và [markdownlint cho VS Code](https://github.com/DavidAnson/vscode-markdownlint).
- [cmake-format](https://cmake-format.readthedocs.io/en/latest/configuration.html).
- [Lint](https://github.com/James-Yu/LaTeX-Workshop/wiki/Linters) và [format LaTeX](https://github.com/James-Yu/LaTeX-Workshop/wiki/Format) trong LaTeX Workshop.
