# Nghiệm thu ArtisanAxis CAM Pro v1.0.1

Ngày 27/11/2026 · Xuân Diệu CNC

**Môi trường đã chạy** Linux x64, GCC 14.2, Qt 6.8.2. Hai bài kiểm toán giao diện dùng widget Qt thật ở chế độ offscreen, xác minh tín hiệu, dữ liệu đã ghi và trạng thái CAMWorker. **LỖI** trong bảng nghĩa là hạng mục chưa đủ điều kiện nghiệm thu. Cột cuối phân biệt lỗi đã quan sát và phần cần môi trường kiểm tra khác.

## Hình học, giao diện và dữ liệu

| Hạng mục | Trạng thái | Kết quả hoặc giới hạn kiểm chứng |
|---|---|---|
| Chạy tinh 0,00 mm | ĐẠT | Bỏ 0,035 mm. Các điểm tiếp xúc 4X trong bài thử khớp nghiệm Drop-Cutter trên STL trong 1e−9 mm. Điểm dây cung chèn lấy lại từ STL. Đoạn rút dao và đoạn lướt có mục đích khác với điểm tiếp xúc tinh. |
| NC 3 chữ số và dung sai | ĐẠT | Dung sai 0,004 mm chỉ áp dụng ở kiểm tra phần mũi trong NC Verifier. Côn, thân và collet giữ kiểm tra riêng. |
| Model | ĐẠT | Chọn trục, góc, tỷ lệ, dịch và đối xứng ba trục đều đi vào ma trận biến đổi. Nút gốc, quay nhanh, hoàn tác và 1:1 được thử. Lưu không chạy CAMWorker. |
| Stock | ĐẠT | Thử chín ô Min/Max/kích thước, độ nở, ba hình, tỷ lệ chóp cụt, cập nhật xem trước, Hủy khôi phục và Lưu chốt giá trị. Kiểm tra dữ liệu phôi 3D đúng 1:1 cho mẫu thử. |
| Phôi 3D ở mọi giá trị và trên Windows GPU | LỖI | Chưa kiểm chứng dựng hình trên Windows. Mô hình dựng trên float của Qt/OpenGL có giới hạn độ phân giải tại tọa độ rất lớn hoặc kích thước rất nhỏ. |
| Tool | ĐẠT | Sáu ô kích thước dao được thử, gồm chuyển dao trong bản nháp và ghi nhận khi Lưu. |
| Surface | ĐẠT | Hai ô điều khiển mặt bọc được ghi vào workflow. Giới hạn 8–96 và 4–128 trùng với worker. |
| Path | ĐẠT | Nút chọn đưa vào công việc, bước dịch và lượng dư đi vào dữ liệu lập đường. Min Z ở hộp thoại ghi rõ là ước tính tham khảo. |
| Machine | ĐẠT | Ba ô tiếng Việt hiện đúng tham chiếu CM600 264,607 / 455,104 / −470,090 mm. Sửa số bỏ xác nhận cũ. Lưu và mở lại giữ cả giá trị lẫn xác nhận. Mode/H1 hiển thị dưới dạng thông tin. |
| Xuất mọi kiểu máy/post | LỖI | Bộ xuất mô hình hóa máy 4X bàn A và máy 5X bàn A + đầu B vuông góc theo các dialect đã có. Đầu–đầu, bàn–bàn, G43.5 và post tự định nghĩa chưa có động học/bộ phát cho sản xuất. Chưa xác minh hành vi thực tế trên từng bộ điều khiển. |
| AI | ĐẠT | Kiểm tra mô tả, nút Đọc, số mặt, 4X/5X, dao phá/tinh, hai chiều dài nhô, bước tinh, lượng dư và giữ chỉnh tay. |
| Chế độ 4X/5X trên thanh công cụ | ĐẠT | WorkshopAudit kiểm tra cả hai chiều giữa thanh công cụ và Trợ lý CAM, post tương ứng của CM600, hồ sơ chỉ hỗ trợ một chế độ và khôi phục chế độ từ phiên đã lưu. Đổi chế độ cập nhật các lượt xoay. Lượt phá mặt vẫn dùng 4X. |
| Khóa cấu hình khi CAMWorker đang chạy | ĐẠT | WorkshopAudit chạy CAMWorker thật, thử gọi thao tác mở STL, đổi bộ mẫu và mở cài đặt khi khóa. Worker trả và hiển thị kết quả trên cùng phiên bản cấu hình. Hủy chủ động đưa các nút về trạng thái ban đầu và giữ kết quả trước đó. |
| Bộ mẫu và Lưu & Đóng | ĐẠT | Preset 2/4 mặt và lưu Model/Stock được thử ở mức Workshop với QProcess vẫn NotRunning. Lưu hộp thoại các nhóm còn lại áp dụng bản nháp mà không gọi khởi chạy worker. |
| Lưu ngoài thư mục cài đặt | ĐẠT | Thử khôi phục STL liên kết, biến đổi, phôi và đường dao phiên trước mà không tính lại. Cấu hình máy lưu theo người dùng. |
| Cam kết 0 nút chết, 0 ô trang trí trên toàn bộ Windows | LỖI | Đã kiểm toán các hộp thoại và thao tác được liệt kê ở trên. Chưa có chạy Windows để chứng minh toàn bộ, gồm render, hộp thoại hệ thống, cài đặt và cập nhật. |

## Đóng gói và chạy Windows

| Hạng mục | Trạng thái | Kết quả hoặc giới hạn kiểm chứng |
|---|---|---|
| Workflow Windows x64 | ĐẠT | Kiểm tra cấu trúc YAML, kích hoạt workflow_dispatch, quyền phát hành sau build và bộ tệp đầu ra. Workflow cài Qt, chạy CTest, đóng gói Inno và thử ứng dụng đã cài bằng mã xác nhận khởi động. |
| Setup.exe thực tế, CTest Windows và phát hành GitHub | LỖI | Chưa có kho đã cấp quyền trong phiên làm việc. Chứng chỉ ký mã là tùy chọn. Chưa chạy workflow trên máy Windows và chưa tạo hoặc đăng Setup.exe. |
| API chống ngủ tự động | ĐẠT | Gọi SetThreadExecutionState khi worker bắt đầu và giải phóng khi worker dừng hoặc ứng dụng đóng. Tác dụng chỉ giữ máy khỏi chế độ ngủ tự động vì rảnh trong Windows. |
| Hành vi chống ngủ đã đo trên Windows | LỖI | Chưa thử trực tiếp khi thu nhỏ cửa sổ hoặc khi hủy tác vụ. |
| Cập nhật 1-Click và rollback | LỖI | Có nút gọi trình cập nhật, kiểm tra phiên bản tăng dần và SHA256, xác minh chữ ký khi có chứng chỉ đã ghim, sao lưu thư mục cài và thông tin gỡ cài, chờ mã xác nhận của tiến trình mới để giữ bản cập nhật hoặc khôi phục. Chưa chạy vòng đời cài đè và rollback trên Windows. |
| Di Lặc thật, chạy khô và gia công thật | LỖI | Các phép thử dùng mô hình/mẫu kiểm thử. Chưa nghiệm thu đồ gá thực, bộ điều khiển thực hoặc độ phủ toàn bộ khe trên tượng thật. |

## Đo BVH

STL nhị phân tổng hợp, AMD EPYC 9V74. Ba tiến trình cho mỗi kích thước. Thời gian là trung vị, RAM đỉnh là giá trị cao nhất tính cả đọc và chuyển đổi STL. Mỗi lần đo 10.000 truy vấn Drop-Cutter cầu R0,4 mm. Không đo toàn bộ quá trình lập đường dao.

| Tam giác | Dựng BVH | 10.000 truy vấn | Truy vấn/giây | RAM đỉnh |
|---:|---:|---:|---:|---:|
| 1.000.000 | 0,100 s | 0,347 s | 28.815 | 110,16 MiB |
| 2.000.000 | 0,219 s | 0,613 s | 16.318 | 213,16 MiB |
| 3.000.000 | 0,351 s | 0,801 s | 12.486 | 316,16 MiB |

Mã chạy lại và kết quả từng lượt nằm trong tools/bvh-bench.

## Kiểm thử hồi quy v1.0.1

**8/8 nhóm đạt trên Linux x64 trong 155,55 giây.** WorkshopAudit chạy với các widget Qt thật ở chế độ offscreen và một tiến trình CAMWorker thật. Ca thử xác nhận đồng bộ chế độ và post theo hai chiều, nhận kết quả khi khóa cấu hình, giữ kết quả trước đó khi hủy và khôi phục chế độ khi mở lại. Chưa chạy Setup.exe và CTest trên Windows cho bản này.

## Số liệu CTest và lõi hình học ghi nhận ở v1.0.0

**8/8 nhóm đạt trên Linux trong 151,66 giây.** Hai nhóm kiểm toán hộp thoại và Workshop nằm trong cùng bộ CTest.

| Ca kiểm tra | Số liệu |
|---|---|
| NC Verifier | Chấp nhận sai lệch mũi 0,003 mm, từ chối 0,006 mm và từ chối can nhiễu collet 0,001 mm. |
| XYZA lệch tâm | 1.071 điểm ăn dao, Y biến thiên 1,37596–1,82404 mm, A quay 1.199,38°, 0 lần nhấc giữa lượt. Điểm tinh đối chiếu Drop-Cutter đạt ngưỡng 1e−9 mm trên mẫu. |
| 5X mẫu đầu và hai tay | 1.658 điểm liên kết có nghiêng B, 21 điểm trong dải gần khe. Có 7 lần tách lượt để tránh nối qua chỗ hình chiếu nhảy giữa hai mặt. |
| NC mẫu CM600 và bù trực tiếp | Đọc lại 1.813 và 2.354 khối, đều 0 lỗi theo mô hình. Offset Z cố tình đặt sai bị báo lỗi. |
| Va chạm liên tục | Bắt collet va giữa đoạn trong cả ca dịch ngang lẫn đảo B. |
| Đọc tệp đã lưu | Worker mở lại 1.017 khối đồng thời 4X. |
| OpenNURBS | Build Linux và đọc SUFACE_DILAC.3dm, nhận 1 mặt và 2.401 mẫu pháp tuyến. |

Các lần tách 5X là kết quả của ràng buộc tiếp xúc chính xác trên mẫu tổng hợp, chưa xác nhận độ phủ hoặc khoảng hở trên tượng của xưởng.

## Kiểm tra thay đổi ký mã tùy chọn

Đã phân tích cú pháp PowerShell và chạy 7 ca mô phỏng trình cập nhật trên PowerShell 7/Linux. Cả GitHub Releases và Google Sheet phát hiện bản mới khi bỏ trống chứng chỉ. Bản cũ, mã băm sai và cấu hình chứng chỉ sai bị xử lý đúng. Khi ghim chứng chỉ, bộ cài thiếu chữ ký bị từ chối. Đây là kiểm thử mô phỏng, chưa chạy bộ cài hoặc rollback trên Windows.
