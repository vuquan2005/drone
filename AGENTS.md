# Hướng dẫn cho agent

Tài nguyên hướng dẫn cho agent nằm trong [docs/agent/](docs/agent/README.md).
Khi thực hiện công việc liên quan đến Git, đọc [hướng dẫn Git cho agent](docs/agent/git.md).

<!-- CODEGRAPH_START -->

## CodeGraph

Trong repository có `.codegraph/` ở gốc, dùng CodeGraph trước khi tìm kiếm hoặc đọc tệp để tìm hiểu mã nguồn:

- Ưu tiên MCP `codegraph_explore` nếu có; đưa tên tệp, symbol hoặc câu hỏi vào truy vấn.
- Nếu không có MCP, dùng `codegraph explore "<symbol hoặc câu hỏi>"`.
- Nếu không có `.codegraph/`, bỏ qua CodeGraph; không tự tạo index.

Index gốc dành cho firmware. Để tra cứu mã tham khảo, dùng index riêng tại `docs/external/repos/`, nếu có; ví dụ `codegraph explore -p docs/external/repos "<symbol>"`.
<!-- CODEGRAPH_END -->
