# Tài liệu đồ án

```text
docs/
├── README.md
├── agent/                   # Tài nguyên hướng dẫn cho agent
│   ├── README.md
│   ├── tools.md
│   └── git.md
├── guides/                  # Hướng dẫn làm việc
│   └── git.md
├── research/                # Ghi chú và phân tích nghiên cứu
│   ├── README.md
│   ├── MIXER_NGHIEN_CUU_TU_CAC_DU_AN.md
│   └── control/             # Các tài liệu khảo sát kiến trúc điều khiển
├── references/              # Tài liệu tham khảo gốc
│   ├── papers/              # PDF, chỉ mục bài báo và references.bib
│   │   └── papers_md/       # Bản trích xuất Markdown và ảnh đi kèm
│   └── tutorials/           # Bài hướng dẫn lưu từ bên ngoài
└── external/
    ├── README.md
    └── repos/               # Repo tham khảo cục bộ, không đưa vào Git
```

## Đọc từ đâu?

- Tra cứu khi cần: [công cụ](agent/tools.md) và [Git cho agent](agent/git.md).
- Agent làm việc trong dự án: [tài nguyên cho agent](agent/README.md).
- Xem ghi chú đã tổng hợp: [mục lục nghiên cứu](research/README.md).
- Tìm bài báo: [chỉ mục bài báo](references/papers/README.md) và [BibTeX](references/papers/references.bib).
- Đọc tài liệu ngoài: [Arduino drone](references/tutorials/arduino_drone/arduino_drone.html.md) và [Mixer Theory](<references/tutorials/mixer_theory/Mixer Theory.html>).
- Xem mã nguồn tham khảo: [repo bên ngoài](external/README.md).

## Đặt tài liệu mới ở đâu?

- Hướng dẫn cài đặt hoặc thao tác đặt trong `guides/`.
- Hướng dẫn thực hành dành cho agent đặt trong `agent/` và liên kết từ mục lục của thư mục đó.
- Ghi chú, phân tích và nhận xét của đồ án đặt trong `research/`.
- Bài báo PDF đặt trong `references/papers/`; thông tin trích dẫn lưu trong `references/papers/references.bib`.
- Bài hướng dẫn bên ngoài đặt trong `references/tutorials/`, giữ ảnh và tệp đi kèm trong cùng thư mục của bài.
- Repo tham khảo chỉ để đọc đặt trong `external/repos/`; ghi nguồn vào `external/README.md`.

Dùng đường dẫn tương đối khi liên kết tài liệu để có thể mở trên máy khác. Nguồn báo cáo LaTeX nằm ở `report/` tại gốc dự án. Khi chọn tài liệu từ thư viện nghiên cứu để đưa vào báo cáo, làm theo [nguyên tắc của báo cáo](../report/README.md#nguyên-tắc); giữ nguyên khóa BibTeX giữa thư viện và báo cáo.
