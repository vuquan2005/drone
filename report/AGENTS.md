# Hướng dẫn viết và chỉnh sửa báo cáo đồ án

Áp dụng cho nội dung và tài nguyên trong `report/`. Đây là báo cáo đồ án cơ điện tử bằng tiếng Việt, biên soạn bằng LaTeX. Ưu tiên nội dung cơ bản, mạch lạc, nhất quán.

## Văn phong và mức độ chi tiết

- Viết thành nội dung có thể đưa trực tiếp vào báo cáo: nêu vấn đề, trình bày cách giải quyết, tính toán và kết luận lựa chọn.
- Dùng đoạn văn liên kết để giải thích; dùng bảng cho thông số và so sánh, hình cho cấu tạo và sơ đồ.
- Không đưa lịch sử trao đổi, phương án đã bỏ, lời nhắc cho người viết hoặc hướng dẫn thao tác vào phần nội dung chính.
- Tránh giọng ghi chú kỹ thuật: không liên tục viết “cần kiểm tra”, “chưa xác nhận”, “không được xem là…”. Nêu giới hạn có ảnh hưởng đến kết luận một lần, tại đúng chỗ.
- Giữ mức chi tiết phù hợp đồ án. Không mở rộng thành quy trình chế tạo hoàn chỉnh hay chép toàn bộ datasheet khi người dùng chỉ cần tính chọn cơ bản.

## Bảng thông số và hình minh họa

- Giữ kiểu bảng thống nhất, có chú thích và `\label`; tránh hàng bị chia trang, chữ tràn ô và tiêu đề bảng đứng tách khỏi bảng.
- Xem ảnh trước khi chèn. Chú thích phải đúng đối tượng; ảnh chip không gọi là ảnh cả bo mạch, ảnh của một dòng sản phẩm không khẳng định là đúng mọi biến thể.
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

## Kiểm tra trước khi bàn giao

- Với thay đổi nội dung LaTeX, bảng, hình hoặc định dạng, biên dịch từ thư mục `report/` bằng `latexmk main.tex`.
- Nếu cần chạy trực tiếp để chẩn đoán, dùng XeLaTeX và BibTeX theo cấu hình trong `latexmkrc`; chạy đủ lượt để ổn định tham chiếu và mục lục.
- Kiểm tra PDF bằng hình ảnh ở các trang bị ảnh hưởng và trang liền kề. Đặc biệt xem kích thước ảnh, chú thích, bảng, ngắt trang, khoảng trắng và chữ tràn.
- Không chỉ dựa vào việc biên dịch thành công. Không để tham chiếu hoặc trích dẫn chưa xác định trong phần vừa sửa.
- Bản PDF sinh tại `build/main.pdf`; giữ file build trong thư mục đang được Git bỏ qua.
- Nếu bản Markdown đối chiếu vẫn còn trong workspace và thuộc phạm vi công việc, đồng bộ với nội dung báo cáo. Không tự tạo lại file người dùng đã xóa.
- Trước commit, xem `git status`, `git diff` và chạy `git diff --check`; chỉ đưa vào commit các thay đổi thuộc công việc được yêu cầu.
