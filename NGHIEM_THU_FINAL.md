# Nghiệm thu ArtisanAxis CAM Pro v1.2.0

Ngày 28/11/2026 · Xuân Diệu CNC

Bản này bổ sung lượt rút dao 5X dùng tốc độ ăn dao đến mặt phẳng an toàn trước G00. Bộ đọc lại đánh dấu lượt rút riêng để mô phỏng báo khi rút chạm gỗ còn lại. Chẩn đoán phân biệt vượt hành trình, giao cắt bao dao tại một tư thế và khoảng hở đoạn chưa được chứng minh.

## Hồi quy và kiểm tra tích hợp

| Hạng mục | Kết quả |
|---|---|
| CTest Release trên Linux x64 | 10/10 đạt trong 95,22 giây. |
| Mặt dẫn | Đối chiếu tiết diện STL, tâm bàn quay, A dọc X/Y, đường ghép chu vi, pháp tuyến và ô hiện/ẩn ở cây dự án. |
| BVH chống va chạm | 1.980 phép kiểm thân và bầu kẹp gộp cho cùng kết quả với kiểm từng tiết diện. Các ca va giữa hai đầu mút được phát hiện. |
| Tích hợp 4X và 5X CM600 trên mẫu tổng hợp | Bốn mặt, phá tròn, tinh, ba trạng thái phôi và đọc lại NC. 4X có 2.450 khối, 5X có 1.964 khối, 0 voxel mẫu bị khoét. |
| Đầu B lệch tâm | Kiểm thử vectơ từ mũi dao đến tâm B cho hành trình Z bất đối xứng. Hộp thoại bỏ dấu xác nhận cũ sau khi đổi tọa độ, tay đòn hoặc động học nâng cao. |
| Lead/Lean và liên tục góc | 493 khối NC qua kiểm chứng với chiều B đảo và giới hạn B bất đối xứng. Ca SLERP đi qua ranh 360° theo cung ngắn và chiếu lại tiếp xúc trên STL. |
| Cập nhật | 9/9 mô phỏng trên PowerShell 7, cộng kiểm thử Qt lưu và mở lại cấu hình. Chưa cài hoặc khôi phục bằng Setup.exe trên Windows. |
| Rhino/OpenNURBS | CAMWorker Release đọc mẫu 3DM trên Linux, 1 mặt và 2.401 mẫu pháp tuyến. |

Trong mô phỏng, bộ đọc NC kiểm hành trình, lệnh khởi tạo và chuyển động theo máy mô hình. Kiểm tra phôi còn lại lấy mẫu voxel. Tệp NC cuối chỉ được cấp khi qua kiểm chứng và offset được xác nhận rõ trong hồ sơ.

## Mẫu thật Di Lặc

Ba tệp STL đính kèm có cùng SHA-256 và 1.056.883 tam giác. Kích thước đọc từ lưới là **259,904 × 94,990 × 73,691 mm**. Các ca thử co riêng chiều X về 250 mm, giữ Y/Z, dùng phôi hộp 250 × 100 × 80 mm, nhấc tối thiểu 114,031 mm. Dùng hồ sơ CM600 tham chiếu với Z mềm −450 đến +200 mm và offset Z −470,090 mm ở trạng thái chưa xác nhận. Dữ liệu dao, lượng dư và feed dùng trong ca nằm tại `tools/qa-dilac`.

| Ca thử trên STL thật | Kết quả |
|---|---|
| Mặt dẫn | 18.432 đỉnh hiển thị trong 0,66 giây. |
| Phá 4 mặt · 4X | 39.260 khối trong 18,74 giây, 0 voxel mẫu bị khoét và 130 vị trí không tới. Kiểm chứng mẫu đạt. Z cục bộ dừng ở 20,090 mm theo giới hạn mềm. |
| Ba công đoạn 5X · X 122–125 mm | Bước dọc 0,22 mm, Lead 5°, Lean 8°. 17.532 khối trong 131,67 giây, bị bộ kiểm chứng chặn xuất · 6 mục báo giao cắt bao dao. |
| Chẩn đoán 4X toàn chiều dài trước sửa rút dao · bước 2,5 mm | Khoảng lấy mẫu quanh chu vi 4 mm. 120.207 khối trong 203,57 giây, bị bộ kiểm chứng chặn xuất · 1 đoạn nối báo va chạm hoặc cận khoảng hở chưa đủ. |
| Chẩn đoán 5X toàn chiều dài trước sửa rút dao · bước 2,5 mm | Khoảng lấy mẫu quanh chu vi 4 mm. 127.216 khối trong 445,70 giây, bị bộ kiểm chứng chặn xuất · 2 đoạn nối báo va chạm hoặc cận khoảng hở chưa đủ, 1 cảnh báo chạy nhanh dưới mặt phẳng an toàn. |
| Chẩn đoán 4X toàn chiều dài · bước 0,22 mm | Khoảng lấy mẫu quanh chu vi 4 mm. 620.181 khối trong 607,62 giây, bị bộ kiểm chứng chặn xuất · 12 mục báo giao cắt bao dao. |
| Chẩn đoán 5X toàn chiều dài · bước 2,5 mm | Khoảng lấy mẫu quanh chu vi 4 mm. 127.216 khối trong 318,02 giây, bị bộ kiểm chứng chặn xuất · 2 mục báo giao cắt bao dao. |
| Chẩn đoán 5X toàn chiều dài trước sửa rút dao · bước 0,22 mm | Ca chẩn đoán chưa có kết quả cuối. Mốc đã ghi nhận 506/1028 vòng. |
| Phá bốn mặt 4X với tâm A và dải Z đọc từ MPRJ | 45.781 khối, qua bộ kiểm chứng mô hình. |
| Dải 5X X122–125 mm dùng giả định vectơ X +78,7 mm | 17.740 khối, bị bộ kiểm chứng chặn xuất · 10 mục báo giao cắt bao dao. |
| Dải 5X X122–125 mm dùng giả định vectơ X −78,7 mm | 17.740 khối, bị bộ kiểm chứng chặn xuất · 10 mục báo giao cắt bao dao. |

Các ca so sánh MPRJ giữ xác nhận hiệu chuẩn tắt. Các số ±78,7 mm ở bảng là giả định độ nhạy, còn giá trị Z của vectơ thử là 232,9175 mm. Không dùng các giả định đó để thay thông số máy.

Bước dọc 2,5 mm dùng để kiểm tra chuỗi công đoạn toàn hình. Các ca này chưa đánh giá chất lượng tinh ở 0,22 mm. Chưa nghiệm thu NC chạy máy cho toàn tượng. Giới hạn Z tham chiếu giữ lại phần vật liệu thấp hơn, và bộ kiểm chứng còn từ chối một số đoạn nối trên mẫu thật. Cần kiểm chứng lại với offset, giới hạn, dao và đồ gá được xác nhận trên máy xưởng. Chứng cứ công khai mô tả cách thao tác HongFa và PowerMill. Chưa có bộ NC đối chứng cùng hình học để đo tương đương 100%.

## Tài liệu

- Đối chiếu bản nền trong `DOI_CHIEU_LOI_CAM.md`. Hồ sơ MPRJ và chương trình TAP đã chạy thử được phân tích trong `DOI_CHIEU_CM600.md`.
- Dùng `tools/qa-dilac/run.py` để chạy lại STL thật. Kết quả theo từng ca và số dòng chẩn đoán có trong `tools/qa-dilac/results-v1.2.0.json`.
- Mở cấu hình và nguồn cập nhật theo `HUONG_DAN_CAI_DAT_VA_CAP_NHAT.md`.

## Biên bản phiên bản trước

# Nghiệm thu ArtisanAxis CAM Pro v1.1.0

Ngày 27/11/2026 · Xuân Diệu CNC

## Kết quả đã chạy

| Hạng mục | Kết quả |
|---|---|
| CTest Release Linux x64, GCC 14.2, Qt 6.8.2 | 9/9 nhóm đạt trong 46,67 giây |
| CTest `LongSpiral250mm` | Mẫu trụ dài 250 mm có 4.896 tam giác. Bước dọc trục 0,22 mm. Đạt 429.220 lớp, 1.109,09 vòng xoay, 442.041 điểm cắt. Không nhấc trung gian, không mất lớp. |
| CAMWorker và tệp NC trên mẫu 250 mm | Worker đọc STL nhị phân, lập đường, kiểm chứng và ghi tệp NC 21,13 MB. Đạt 442.060 khối NC, 0 lỗi kiểm chứng, 0 lớp không tới, 0 lần nhấc trung gian. Có 1 trạng thái mô phỏng phôi. |
| Thử tải `LongSpiralCheck --stress` | 5.204.291 lớp, 5.204.290 điểm cắt, 1.118 vòng trong 194,30 giây. Mẫu trụ rất nhỏ tổng hợp có bước dọc trục 0,22 mm. Ca này đo lõi lập đường xoắn ốc. |
| Cấu hình và giao diện | WorkshopAudit thử đồng bộ 4X/5X, kế hoạch và post, tích đường, lệnh AI tính toàn bộ, khóa chỉnh sửa, hủy và nhận tiến độ 100%. DialogAudit thử thông số đường dao nâng cao và lưu cấu hình. WorkerSimultaneousFile thử ghi, đọc lại NC, tiến độ, playback, tốc độ F và RPM. |
| OpenNURBS trên Linux | Biên dịch và đọc được tệp .3dm mẫu, nhận 1 mặt, 2.401 mẫu pháp tuyến và 13.824 đỉnh hiển thị. |

## Phạm vi còn cần xác nhận

- Tệp đính kèm chứa HTML v6.2. Chưa có `di lac 250.stl` với 1.056.883 tam giác. Số đo trên mẫu trụ kiểm tra hồi quy giới hạn cũ và khả năng xuất NC, chưa đo thời gian hoặc độ phủ trên tượng của xưởng.
- Các ca giao diện dùng Qt offscreen. Cần xác nhận trực quan OpenGL, biểu tượng Desktop, Setup.exe và đọc .3dm với workflow Windows x64.
- Cửa sổ nối lưu tối đa 4.096 lớp ứng viên và tiếp tục từ tư thế đã chốt. Tìm đường theo cửa sổ có thể chọn tuyến khác khi rời một vùng khó. Bộ kiểm chứng vẫn kiểm tra đường đã xuất theo số NC làm tròn. Bộ nhớ dùng cho các chuyển động đã chốt và kiểm chứng tăng theo kích thước chương trình.

## Biên bản từ v1.0.1 và v1.0.0

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
