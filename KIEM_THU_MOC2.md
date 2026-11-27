# Kết quả kiểm thử Mốc 2

Môi trường Linux x64, GCC 14.2, Qt 6.8.2, Release. Kiểm tra build thường và build OpenNURBS tĩnh. Các số liệu dưới đây đo trên mẫu kiểm thử, không suy rộng sang máy CNC hay tượng Di Lặc thật.

| Bài thử | Kết quả |
|---|---|
| CTest cấu hình/phôi, hình học/post và worker | 3/3 bài đạt với build thường và build có openNURBS |
| World → Face → World | 0,000 mm tại 0°, 90°, 180°, 270°, cho trục A dọc X và Y. Kiểm tra thêm khung có định thức +1 và hướng 90°/270° khác nhau đúng dấu |
| Drop-Cutter mặt nghiêng | Sai số 0,000000000 mm so với nghiệm bù cầu một lần trên mặt thử |
| Drop-Cutter mặt cầu | Sai số lớn nhất 0,001305944 mm tại 4 điểm trên lưới cầu STL so với cầu giải tích |
| Kiểm tra 4 vùng bao | 0/4 tư thế va chạm được chấp nhận và 0/4 tư thế thoáng bị loại |
| Cắt xuyên tâm và giữ lõi | Bài thử 2 mặt tới Z = −1 mm khi đủ tầm, bài thử 4 mặt giữ Min Z ≥ +1 mm |
| STL hộp thử | 354 điểm cho ca phá 2 mặt + xoay, 0 điểm bị loại trong ca mẫu; 220 điểm cho ca 4 mặt |
| Post NC | 7/7 lựa chọn đạt header ASCII, nhấc Z trước từng lệnh A, G01, dừng khi đổi dao, M5/M30 và không có từ lệnh trục B |
| SDF và Wrap | Khoảng cách có dấu đúng trong/ngoài hộp kín. Điểm và pháp tuyến trùng tại đường khép spline. Worker xuất 18.432 đỉnh lưới hiển thị |
| Nhập Rhino | `SUFACE_DILAC.3dm` đọc được một mặt, 4.608 tam giác xem trước, không bỏ qua đối tượng |
| Phép đo riêng BVH | 1 triệu tam giác phẳng tổng hợp, dựng cây 0,135 giây; 10.000 truy vấn Drop-Cutter cầu 0,092 giây |

Công đoạn chưa qua nghiệm thu sản xuất gồm bộ cài và khung nhìn trên Windows, phương ngữ điều khiển của từng post, giới hạn tọa độ cơ khí và khoảng hở với đồ gá thật, cắt đồng thời có A đang quay, thời gian toàn chương trình trên STL tượng thật.
