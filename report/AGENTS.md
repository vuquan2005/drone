# Hướng dẫn viết và chỉnh sửa báo cáo đồ án

Áp dụng cho nội dung và tài nguyên trong `report/`. Đây là báo cáo đồ án cơ điện tử bằng tiếng Việt, biên soạn bằng LaTeX. Ưu tiên nội dung cơ bản, mạch lạc, nhất quán.

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
- Sửa thông tin bìa trong `metadata.tex`.
- Dùng tên file tiếng Anh, chữ thường, không có khoảng trắng và đường dẫn tương đối.
- Lưu nguồn trích dẫn trong `bibliography/references.bib`. Khi lấy nguồn từ [thư viện nghiên cứu](../docs/references/papers/references.bib), giữ nguyên khóa BibTeX và cập nhật thông tin nhất quán ở cả hai nơi.
- Thay hết placeholder trước khi nộp.

## Văn phong và mức độ chi tiết

- Viết thành nội dung có thể đưa trực tiếp vào báo cáo: nêu vấn đề, trình bày cách giải quyết, tính toán và kết luận lựa chọn.
- Giải thích thuật ngữ và chữ viết tắt khi xuất hiện lần đầu. Khi giới thiệu công thức hoặc chỉ số, nêu ý nghĩa của đại lượng, ký hiệu, đơn vị và quy ước cần để đọc kết quả.
- Khi chỉnh văn phong hoặc dịch, giữ nguyên ý kỹ thuật, số liệu, công thức và trích dẫn. Nếu thiếu số liệu hoặc nguồn để hỗ trợ kết luận, chỉ rõ chỗ cần bổ sung khi bàn giao.
- Dùng đoạn văn liên kết để giải thích; dùng bảng cho thông số và so sánh, hình cho cấu tạo và sơ đồ.
- Không đưa lịch sử trao đổi, phương án đã bỏ, lời nhắc cho người viết hoặc hướng dẫn thao tác vào phần nội dung chính.
- Tránh giọng ghi chú kỹ thuật: không liên tục viết “cần kiểm tra”, “chưa xác nhận”, “không được xem là…”. Nêu giới hạn có ảnh hưởng đến kết luận một lần, tại đúng chỗ.
- Giữ mức chi tiết phù hợp đồ án. Không mở rộng thành quy trình chế tạo hoàn chỉnh hay chép toàn bộ datasheet khi người dùng chỉ cần tính chọn cơ bản.

## Bảng thông số và hình minh họa

- Giữ kiểu bảng thống nhất, có chú thích và `\label`; tránh hàng bị chia trang, chữ tràn ô và tiêu đề bảng đứng tách khỏi bảng.
- Xem ảnh trước khi chèn. Chú thích phải đúng đối tượng; ảnh chip không gọi là ảnh cả bo mạch, ảnh của một dòng sản phẩm không khẳng định là đúng mọi biến thể.
- Trước khi thay hoặc chỉnh ảnh, đối chiếu nội dung liên quan trong báo cáo: đoạn mô tả, chú thích, tham chiếu, quy ước trục, chiều quay, ký hiệu, thông số và công thức sử dụng hình. Ảnh thay thế phải thể hiện đủ thông tin cần thiết và nhất quán với các nội dung này; cập nhật phần liên quan khi cần.
- Dùng ảnh trong `figures/chXX/` ở đúng phần mô tả thiết bị. Giữ nguyên tỉ lệ, cắt lề trắng khi cần và không che mất nội dung ảnh.
- Kích thước ảnh phải đủ để nhận biết linh kiện ở khổ A4. Có thể bắt đầu khoảng 0,4–0,6 chiều rộng dòng cho ảnh thiết bị, rồi điều chỉnh theo phần vật thể thực sự nhìn thấy và độ phân giải.
- Có thể đặt hai ảnh liên quan cạnh nhau, như cell pin và BMS. Sơ đồ phải có chữ đọc được ở kích thước in.
- Dùng tham chiếu tự động; không ghi cứng số hình, số bảng hoặc số công thức trong nguồn LaTeX.

## Bố cục và phạm vi sửa

- Giữ định dạng của mẫu trong `styles/thesis.sty`; chỉ đổi định dạng chung khi công việc thực sự cần hoặc người dùng yêu cầu.
- Tập trung cơ sở lý thuyết ở Chương 2, tính toán và thiết kế ở Chương 3, kết quả cùng đánh giá ở Chương 4; hạn chế lặp nội dung giữa các chương.
- Kết luận tóm tắt kết quả đạt được, không kể lại toàn bộ quá trình thực hiện.
- Giữ các file riêng theo cấu trúc hiện có; dùng `\input`/`\include` thay vì dồn mọi nội dung vào `main.tex`.
- Tôn trọng thay đổi của người dùng và các file họ đã thêm hoặc xóa. Chỉ sửa trong phạm vi công việc hiện tại.

## Đối chiếu nội dung giữa các chương

- Khi viết hoặc sửa một mục, đọc các phần mà mục đó dựa vào. Đối chiếu thuật ngữ tiếng Việt và tiếng Anh, chữ viết tắt, ký hiệu, hệ trục, dấu và đơn vị giữa nội dung, công thức, hình và bảng.
- Trong Chương 3, giải thích các lựa chọn chính theo mạch yêu cầu, căn cứ tính chọn và quyết định thiết kế.
- Phân biệt thông số từ datasheet, giá trị tính toán và số đo thực nghiệm. Trong Chương 4, trình bày rõ điều kiện thử, quan sát và số đo. Khi so sánh kết quả, ghi rõ mốc so sánh và chỉ số. Giới hạn kết luận theo điều kiện đã kiểm chứng, chẳng hạn điều kiện thử trên gimbal. Dùng dữ liệu bay để hỗ trợ nhận định về bay.
- Trong Chương 5, chỉ tổng hợp kết quả đã có căn cứ ở Chương 4. Khi rà nhiều nhận định cùng lúc, có thể dùng bảng Nhận định · Căn cứ · Phạm vi trong phần nhận xét biên tập.

## Kiểm tra trước khi bàn giao

- Với thay đổi nội dung LaTeX, bảng, hình hoặc định dạng, biên dịch từ thư mục `report/` bằng `latexmk main.tex`.
- Nếu cần chạy trực tiếp để chẩn đoán, dùng XeLaTeX và BibTeX theo cấu hình trong `latexmkrc`; chạy đủ lượt để ổn định tham chiếu và mục lục.
- Kiểm tra PDF bằng hình ảnh ở các trang bị ảnh hưởng và trang liền kề. Đặc biệt xem kích thước ảnh, chú thích, bảng, ngắt trang, khoảng trắng và chữ tràn.
- Không chỉ dựa vào việc biên dịch thành công. Không để tham chiếu hoặc trích dẫn chưa xác định trong phần vừa sửa.
- Bản PDF sinh tại `build/main.pdf`; giữ file build trong thư mục đang được Git bỏ qua.
- Nếu bản Markdown đối chiếu vẫn còn trong workspace và thuộc phạm vi công việc, đồng bộ với nội dung báo cáo. Không tự tạo lại file người dùng đã xóa.
- Trước khi commit, xem `git status` và `git diff` để rà thay đổi chưa stage. Sau khi stage, xem `git diff --cached` và chạy `git diff --cached --check`. Chỉ đưa vào commit các thay đổi thuộc phạm vi công việc.

## Biên dịch

Từ thư mục gốc dự án:

```sh
make report
```

Có thể build trực tiếp bằng `latexmk main.tex` khi đang ở thư mục `report/`.
