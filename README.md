# ArtisanAxis CAM Pro · v1.0.0

Mã nguồn native C++20 / Qt 6. Chọn Máy/Post, phôi và dao, chọn 4X hoặc 5X trong Trợ lý rồi bấm **Tính 4X/5X**. CAMWorker trả đường dao, nhật ký và các trạng thái phôi sau mô phỏng.

## Biên dịch

Trên Windows x64, dùng Developer PowerShell của Visual Studio 2022, Qt 6 MSVC, CMake và Inno Setup. Chỉnh đường dẫn Qt.

    .\scripts\Build-Windows.ps1 -QtRoot C:\Qt\6.8.3\msvc2022_64

Thêm -Rhino3dm để bật OpenNURBS tĩnh. Mặc định chỉ cần Qt 6 và thư viện chuẩn C++20. Trên Linux hoặc khi build qua CMake trực tiếp:

    cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt-kit>
    cmake --build build --config Release
    ctest --test-dir build -C Release -V

Có thể bật Rhino bằng -DARTISAN_RHINO=ON, dùng checkout đã ghim với -DFETCHCONTENT_SOURCE_DIR_ARTISAN_OPENNURBS=<checkout>. Kịch bản Windows kế thừa từ Mốc 2. Việc biên dịch và chạy kiểm thử lần bàn giao được thực hiện trên Linux.

## Hệ tọa độ, post và kiểm chứng

- Tọa độ mũi lưu trong khung tức thời của bàn quay. B quay quanh trục ngang của khung đó. Với 4X, B = 0 và không có từ lệnh B. Y thay đổi trong từng ứng viên trước khi Drop-Cutter. Với bàn A dọc Y, hướng dọc và ngang đổi tương ứng.
- 5X hiện áp dụng máy bàn A + đầu B vuông góc. Mô hình tay đòn giả định đòn dọc trục dao trung hòa. Tọa độ cơ khí suy ra từ mũi, chênh lệch tay đòn so với B = 0 và workOffsetMachine [X,Y,Z] nhập trực tiếp ở Hồ sơ máy. Bộ xuất khóa khi tọa độ này chưa được xác nhận hoặc thiếu giới hạn Z. Máy đầu–đầu, bàn–bàn, tay đòn lệch tâm khỏi trục dao và G43.5 chưa được mô hình hóa.
- Các profile CM600, Syntec và Fanuc xuất mẫu G43.4 H1/G49 theo quy ước XYZ mũi trong hệ phôi. Profile Siemens dùng TRAORI/TRAFOOF theo cùng mô hình. Profile không RTCP xuất XYZ đã bù chênh tay đòn. Các mẫu chưa được chạy trên firmware thực tế tương ứng. A/B xuất theo dấu cấu hình.
- G00 dịch ngang hoặc đổi A/B yêu cầu cao trình an toàn. G01 dùng cho cắt xoắn ốc đồng thời. Khối khởi đầu giả định máy bắt đầu ở tư thế trung hòa đã an toàn. Phần mềm không có vị trí máy thật trước khối đầu. Cần đối chiếu thực tế bật/hủy RTCP, hành trình, gốc máy, thủ tục thay dao và gá trước khi đưa vào sản xuất.
- Sau khi lưu, ứng dụng tự đọc lại chính tệp vừa ghi. **Đọc lại NC** cũng cho phép mở file đã lưu và xác minh với STL, dao và máy đang chọn. Bộ đọc chấp nhận tập con phát sinh của v1.0.0 (G00/G01 tuyệt đối, đầy đủ tọa độ, lệnh bù, spindle, tool, kết thúc). Nó dựng lại từ số đã làm tròn trong NC, xét hành trình và kiểm tra sweep trên STL. Lỗi có số dòng. Chu trình tổng quát, arc và macro chưa được hỗ trợ. Mô hình nội suy là XYZ và góc đồng bộ trong khối; quy tắc nội suy firmware vẫn cần đối chiếu.

## Đường dao, mô phỏng và nhật ký

1. Quy hoạch động trên đồ thị C-Space nhiều lớp tìm chuỗi A liên tục, Y lệch tâm và B dẫn hướng bởi pháp tuyến mặt bọc. Drop-Cutter dùng đúng tư thế ứng viên. Bù cầu nằm trong nghiệm Drop-Cutter và post không cộng bán kính thêm. Điểm tiếp xúc tinh lấy đúng nghiệm Drop-Cutter 0,00 mm. Lõi chia nhỏ dây cung khi cần, giữ điểm chèn trên mặt bao tiếp xúc. Chỉ bộ đọc lại NC dùng dung sai 0,004 mm ở phần lưỡi chạm mẫu để đối chiếu NC 3 chữ số. Phần côn, thân và collet giữ kiểm tra riêng.
2. SDF hỗ trợ lọc khoảng cách, BVH kiểm tra mặt STL, bao côn chia vành và kiểm tra cùng bao thân/collet. Đoạn nối dùng mô hình nội suy của post, thử lướt thấp khi phù hợp. Cận dịch chuyển và chia thích nghi dùng để chứng minh khoảng hở. Nếu không nối được, tách lượt và lưu tọa độ, nguyên nhân trong **Nhật ký NC**. Vùng không tiếp cận được thống kê theo lớp không tìm được tư thế. Chưa có bản đồ chứng minh phủ toàn bộ mặt trong khe tượng thật.
3. **Nhập mặt Rhino** dùng pháp tuyến của mặt đầu tiên nội suy từ lưới tham số 49×49 vào bộ sinh hướng. Cần căn chung hệ tọa độ và phương tham số quanh thân/dọc trục. STL vẫn quyết định hình học tiếp xúc. Hồ sơ mặt nhập cần trùng định hướng với mẫu, chưa tự đăng ký hình học.
4. Chọn **Sau phá mặt / Sau cận tinh / Sau chạy tinh** để xem phôi còn lại. Màu cam là dư bề mặt, đỏ là voxel mục tiêu đã bị bóc. Bảng báo kích thước ô và số voxel. Voxel dư có thể bao gồm lượng dư chủ động. Phôi được mô phỏng trên các mẫu chuyển động đọc lại từ NC. Bộ mô phỏng phát hiện một số trường hợp thân/collet hoặc chạy nhanh quẹt phôi và khóa xuất nếu phát hiện. Đây là phép kiểm tra lấy mẫu theo độ phân giải voxel, chưa có chứng minh va chạm liên tục với phôi và chưa nhận hình CAD đồ gá.
5. Giới hạn quy mô đồ thị có báo lỗi. Chưa đo thời gian cả job trên lưới tượng 1–3 triệu tam giác thực của xưởng.

Hướng dẫn cấu hình căn bản và chế độ phân độ cũ có trong README_MOC2.md. Kết quả của Mốc 3 được lưu riêng ở KIEM_THU_MOC3.md.

Xem NGHIEM_THU_FINAL.md và HUONG_DAN_CAI_DAT_VA_CAP_NHAT.md để phân biệt các kết quả đã chạy với phạm vi cần Windows/GitHub và máy thật.

Dữ liệu phiên làm việc (liên kết STL/mặt dẫn, biến đổi, phôi, dao, đường dao và thiết lập) và hồ sơ máy lưu theo người dùng ngoài thư mục cài đặt. Kết quả NC đã tính không được tự khôi phục để tránh dùng chung với một cấu hình mới.
