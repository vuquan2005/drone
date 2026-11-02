# CodeGraph

Với mã nguồn đã có index `.codegraph/`, dùng CodeGraph trước khi tìm kiếm hoặc đọc mã trực tiếp:

- Ưu tiên MCP `codegraph_explore` nếu có; đưa tên tệp, symbol hoặc câu hỏi vào truy vấn.
- Nếu không có MCP, dùng `codegraph explore "<symbol hoặc câu hỏi>"`.
- Nếu không có index, bỏ qua CodeGraph; không tự tạo index.

Index gốc dành cho mã nguồn chính; phạm vi loại trừ nằm trong [codegraph.json](../../codegraph.json). Để tra cứu mã tham khảo, dùng index riêng tại `docs/external/repos/`, nếu có; ví dụ `codegraph explore -p docs/external/repos "<symbol>"`. Với MCP, truyền `projectPath` trỏ đến thư mục tương ứng.

Với mã ngoài phạm vi index hoặc thông tin CodeGraph không cung cấp, tìm kiếm và đọc trực tiếp; cấu hình và tài liệu cũng có thể đọc trực tiếp.
