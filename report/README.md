# Thesis report template

Bộ khung LaTeX được sắp xếp từ mẫu thầy cung cấp. Tên file và thư mục dùng tiếng Anh, chữ thường, không có khoảng trắng. Nhãn trình bày vẫn dùng tiếng Việt; nội dung, thông tin cá nhân và tài liệu trích dẫn đều là placeholder.

## Structure

```text
report/
├── main.tex                 # Tệp biên dịch chính
├── metadata.tex             # Thông tin bìa cần điền
├── latexmkrc                # Cấu hình XeLaTeX, BibTeX và thư mục build
├── .chktexrc                # Cấu hình linter ChkTeX (bỏ qua cảnh báo giả tiếng Việt, bảng kẻ dọc)
├── frontmatter/             # Bìa, mở đầu, cảm ơn, viết tắt, ký hiệu
├── chapters/                # Năm chương khung
├── appendices/              # Phụ lục
├── bibliography/
│   └── references.bib       # Trích dẫn placeholder
├── figures/
│   └── template/            # Khung bìa và logo từ mẫu gốc
└── styles/
    └── thesis.sty           # Định dạng kế thừa từ mẫu gốc
```

## Fill in the template

1. Điền các giá trị `[Thesis title]`, `[Student name]`, v.v. trong `metadata.tex`.
2. Thay `\placeholder{...}` và các nhãn `[Section title]` trong phần cần viết.
3. Đặt hình mới trong `figures/`; thay khung hình placeholder bằng `\includegraphics` khi có hình thực tế.
4. Thay mục `placeholder-reference` trong `bibliography/references.bib` và ví dụ trích dẫn ở chương 1 bằng nguồn đã kiểm tra. Không dùng mục placeholder làm nguồn tham khảo thật.
5. Thêm hoặc bớt chương bằng cách sửa các lệnh `\include` trong `main.tex`.

Hai bảng placeholder ở chương 3 và 4 dùng kiểu bảng lưới có đường kẻ dọc (`|c|c|`) với `\hline` theo truyền thống đồ án kỹ thuật. `styles/thesis.sty` đã cấu hình `\arraystretch=1.2` để các ô bảng có độ đệm thoáng; không sử dụng các lệnh của gói `booktabs` khi dùng bảng có kẻ dọc vì sẽ bị hở viền.

Quy chuẩn định dạng:

- Khổ A4, cỡ chữ thân bài 14 pt (`extbook`), tiêu đề chương 16 pt in đậm, mục cấp một (`\section`) 16 pt in đậm (`\large\bfseries`), các mục con 14 pt in đậm (`\normalsize\bfseries`).
- Lề trái 3.5 cm, lề phải 2 cm, lề trên/dưới 2 cm. Giãn dòng thân bài 1.5 (`\onehalfspacing`).
- Toàn bộ đoạn văn tự động thụt lề 1 cm (dùng `indentfirst`).
- Số trang: số La Mã thường (`roman`) cho phần mở đầu, số Ả Rập (`arabic`) bắt đầu từ Chương 1, đặt ở giữa chân trang (`\pagestyle{plain}`).
- Chú thích hình và bảng: tự động dùng nhãn tiếng Việt (**Hình**, **Bảng**), cỡ chữ nhỏ (`small`), nhãn in đậm và căn giữa (`justification=centering`). Caption hình đặt dưới hình, caption bảng đặt trên bảng. Khung bìa và logo trường được giữ lại; thay logo nếu sử dụng cho cơ sở khác. Khi điền tiêu đề dài hoặc nhiều sinh viên, kiểm tra lại trang bìa.

## Build locally

Cần XeLaTeX, BibTeX và latexmk, cùng các gói LaTeX mà `styles/thesis.sty` sử dụng. Font gốc là Times New Roman, Arial và Courier New; nếu máy không có, mẫu dùng font TeX Gyre tương ứng khi chúng đã được cài.

Từ thư mục gốc repository:

```sh
cd report
latexmk main.tex
```

PDF sinh ra ở `build/main.pdf`. File build được `.gitignore` bỏ qua. Để dọn các file phụ nhưng giữ PDF:

```sh
latexmk -c main.tex
```

## Build in VS Code / LaTeX Workshop

Mở thư mục gốc `drone/` bằng VS Code để nhận cấu hình trong `../.vscode/settings.json`. Mở `report/main.tex`, rồi chạy **LaTeX Workshop: Build LaTeX project** (`Ctrl+Alt+B`). Recipe `thesis (XeLaTeX)` chạy latexmk, tạo PDF và các file phụ trong `report/build/`; SyncTeX được bật để nhảy giữa nguồn và PDF. Xem PDF bằng **LaTeX Workshop: View LaTeX PDF file**.

Các file chương có chỉ dẫn `% !TEX root = ../main.tex` để extension tìm đúng file chính. Không thêm `% !TEX program = xelatex` vào `main.tex`: với extension hiện tại, chỉ dẫn đó có thể gọi trực tiếp XeLaTeX, bỏ qua latexmk và việc chạy BibTeX tự động. Nếu mở riêng thư mục `report/`, cần sao chép phần cấu hình LaTeX Workshop vào Workspace Settings của cửa sổ đó.

Extension đã bật sẵn linter ChkTeX (`onSave`). Cấu hình trong `.chktexrc` đã loại bỏ các cảnh báo giả liên quan đến tiếng Việt (ký tự UTF-8 có dấu, dấu chấm sau từ viết tắt `TS.`), quy ước gạch nối và định dạng bảng kẻ viền đầy đủ.

## Installed toolchain

Các gói Fedora đã được đề xuất cho mẫu này:

| Gói                                   | Vai trò                                               |
| ------------------------------------- | ----------------------------------------------------- |
| `latexmk`                             | Tự điều phối các lượt biên dịch và tài liệu tham khảo |
| `texlive-bibtex`                      | Xử lý bibliography `.bib` theo kiểu `ieeetr`          |
| `texlive-collection-xetex`            | XeTeX/XeLaTeX và các gói liên quan                    |
| `texlive-collection-latexrecommended` | Các gói LaTeX thường dùng                             |
| `texlive-collection-latexextra`       | Các gói LaTeX bổ sung                                 |
| `texlive-collection-fontsrecommended` | Font, gồm TeX Gyre dùng làm font dự phòng             |
| `texlive-collection-pictures`         | Vẽ hình, sơ đồ, gồm PGF/TikZ                          |

TeX Live là bản phân phối cung cấp engine, gói và font. XeLaTeX là engine đang dùng; latexmk là công cụ build; LaTeX Workshop là extension soạn thảo và xem PDF. Mẫu dùng BibTeX, chưa dùng BibLaTeX/Biber. Các font Times New Roman, Arial và Courier New không nằm trong danh sách gói đã đề xuất; vì vậy khi thiếu các font đó, bản dựng dùng font TeX Gyre dự phòng và có thể khác mẫu gốc về độ rộng chữ, xuống dòng.

## Build on Overleaf

Tải nội dung thư mục `report/` lên một project, chọn `main.tex` làm Main document và chọn compiler XeLaTeX. Cấu trúc thư mục trong project phải giống cấu trúc ở đây.

## Original template

Các đoạn văn, tên riêng, số liệu, hình minh họa nội dung, mã MATLAB và phụ lục trùng lặp của báo cáo mẫu đã được thay bằng placeholder hoặc bỏ khỏi bộ khung đang dùng. PDF báo cáo mẫu cũ không được giữ như bản kết quả của đồ án.
