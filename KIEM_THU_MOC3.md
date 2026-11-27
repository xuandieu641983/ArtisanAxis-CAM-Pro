# Kiểm thử ArtisanAxis CAM Pro · Mốc 3

Nền tảng thử Linux x64, Qt 6.8.2, GCC 14.2, cấu hình Release. Các mô hình quay và va chạm là đồ gá/mẫu tổng hợp. Không có thử cắt gỗ hoặc kết nối bộ điều khiển thật.

| Bài thử | Kết quả |
|---|---|
| CTest | 5 nhóm, gồm kiểm tra cấu hình/phôi, hồi quy hình học/post 4X, hồi quy worker Mốc 2, đồng thời 4/5X và đọc tệp NC qua worker |
| Xoắn ốc XYZA | 240 bước ăn dao liên kết. A tích lũy 1.195°, Y biến thiên 1,37596–1,82404 mm, B khóa ở 0 và không có từ lệnh B. Không nhấc giữa lượt. |
| 5X trên mẫu đầu–hai tay rời | 346 chuyển động kế hoạch, 340 điểm ăn dao liên kết có B nghiêng. 6 điểm rơi trong dải kiểm tra gần khe. 0 lần tách/nâng giữa lượt và 0 lớp ứng viên bị loại trên mẫu tổng hợp này. Dải đo trong mặt phôi: 5,1 < abs(Y) < 6,85 mm và 4 < X < 15 mm, không đại diện cho độ phủ hết khe. |
| Va chạm giữa hai điểm | Cả bài dịch ngang và bài đổi hướng nghiêng B đều bắt được va chạm collet ở giữa khi hai đầu đoạn thoáng. |
| CM600 G43.4 H1 | 363 khối chuyển động được dựng lại, 0 lỗi mô hình. Cố tình làm sai offset Z bị báo lỗi có số dòng. |
| Bù tay đòn trực tiếp | 366 khối chuyển động được dựng lại, 0 lỗi mô hình. Offset Z sai bị báo lỗi có số dòng. |
| Siemens TRAORI | Bài kiểm tra bật TRAORI, hủy TRAFOOF và đọc ngược theo cùng mô hình đạt. |
| Tệp NC trên đĩa | Worker xuất và mở lại 244 khối đồng thời 4X từ tệp trên đĩa. |
| Mô phỏng trạng thái phôi | Ca mô phỏng ba công đoạn còn lần lượt 100.457 / 100.383 / 100.315 voxel. |
| Import Rhino | Bản Linux có openNURBS biên dịch được. SUFACE_DILAC.3dm cho một mặt, 4.608 tam giác xem trước và 2.401 mẫu pháp tuyến hữu hạn. |

Giới hạn nghiệm thu: chưa chạy Windows, máy thật và STL Di Lặc thực tế. Chưa xác nhận độ phủ hết khe, mức dư tinh tuyệt đối 0,00 mm, động học firmware hay chứng minh va chạm liên tục với phôi voxel/đồ gá thực. NC được đánh dấu để đối chiếu.
