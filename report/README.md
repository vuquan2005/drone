# Báo cáo đồ án

Báo cáo LaTeX dựa trên mẫu thầy cung cấp, biên dịch bằng XeLaTeX và BibTeX qua latexmk.

## Cấu trúc

```text
report/
├── main.tex          # Điều phối thứ tự các phần của báo cáo
├── metadata.tex      # Thông tin đề tài, giảng viên và sinh viên
├── frontmatter/      # Bìa, mở đầu, cảm ơn và các danh mục
├── chapters/         # Nội dung các chương
├── appendices/       # Phụ lục
├── figures/          # Hình và sơ đồ; template/ chứa tài nguyên bìa
├── bibliography/     # Tài liệu được trích dẫn trong báo cáo
├── styles/           # Định dạng và các lệnh dùng chung
├── latexmkrc         # Cấu hình biên dịch
└── .chktexrc         # Quy tắc kiểm tra LaTeX
```

## Nguyên tắc

- Viết nội dung trong từng chương; thêm hoặc bớt chương tại `main.tex`.
- Sửa thông tin bìa trong `metadata.tex`, định dạng chung trong `styles/thesis.sty`.
- Dùng tên file tiếng Anh, chữ thường, không có khoảng trắng và đường dẫn tương đối.
- Đặt hình trong `figures/`; chia thư mục theo chủ đề khi có nhiều hình.
- Lưu nguồn trích dẫn trong `bibliography/references.bib`. Khi lấy nguồn từ [thư viện nghiên cứu](../docs/references/papers/references.bib), giữ nguyên khóa BibTeX và cập nhật thông tin nhất quán ở cả hai nơi.
- Dùng nhãn và tham chiếu tự động cho hình, bảng, công thức; thay hết placeholder trước khi nộp.
- Giữ kết quả biên dịch trong `build/`; thư mục này được bỏ qua trong Git.

## Biên dịch

Từ thư mục gốc dự án:

```sh
cd report
latexmk main.tex
```

Kết quả: `report/build/main.pdf`. Trong VS Code, mở `report/main.tex` và chạy **LaTeX Workshop: Build LaTeX project**. Trên Overleaf, tải thư mục `report/` lên và chọn compiler **XeLaTeX**.
