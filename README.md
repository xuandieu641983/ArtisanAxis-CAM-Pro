# ArtisanAxis CAM Pro · v1.2.0

Mã nguồn native C++20 / Qt 6 cho xưởng Xuân Diệu CNC.

- **Mở STL** tự căn phôi hộp theo giới hạn 3D của mẫu. Lấy gốc, xoay và scale cập nhật phôi khi bật chế độ tự ôm. Chỉnh phôi bằng tay sẽ giữ kích thước đã nhập cho đến khi chọn **Thiết lập → Tự ôm phôi theo mẫu STL**.
- Chọn hồ sơ máy, **4 Trục đồng thời (XYZA)** hoặc **5 Trục đồng thời (XYZAB)** rồi tích các đường trong cây dự án. Có thể chọn chiến lược theo từng công đoạn. Với một chương trình có cả lượt 4X và 5X, post 5X ghi B = 0 trong các lượt 4X.
- **Lưu & Đóng** chỉ lưu cấu hình. Dùng **Tính các đường đã tích**, **Tính riêng đường này** hoặc lệnh **Tự động tính toàn bộ** trong Trợ lý CAM để chạy. Hai nút xuất riêng trong Trợ lý tính và kiểm chứng nhóm được chọn trước khi ghi tệp.
- CAMWorker gửi tiến độ thực qua từng giai đoạn. Hủy giữ kết quả hợp lệ trước đó nếu cấu hình dự án vẫn khớp. Dùng thanh mô phỏng đáy cửa sổ để kéo tới vị trí bất kỳ hoặc chạy từ 1× đến 20×. Thời gian hiển thị là ước tính từ chiều dài chuyển động và tốc độ, không dùng mô hình tăng giảm tốc của bộ điều khiển.
- Mở **Mặt dẫn → Dựng từ STL** để dựng mặt dẫn thích ứng trong hệ trục quay. Ô tích tại **Mặt dẫn** bật tắt lưới bọc bán trong suốt. Bản build OpenNURBS nhận thêm mặt dẫn **.3dm**. Dùng **Nhật ký NC** để xem kết quả kiểm chứng và các lần tách lượt.

## Tính xoắn ốc dài

Lõi giữ tối đa 4.096 lớp ứng viên trong cửa sổ nối, dùng BVH chia sẻ và tính truy vấn ứng viên theo lô đa luồng. Điểm nối được mang sang cửa sổ tiếp theo mà không phát lệnh nhấc riêng. Quá trình vẫn có thể tách lượt khi mất tiếp xúc hoặc không tìm được đường nối hợp lệ. Đường dao đã chốt và dữ liệu kiểm chứng NC dùng bộ nhớ theo kích thước chương trình. GUI nhận phần xem trước hạn chế kích thước, còn NC đủ chương trình được ghi qua tệp tạm.

`LongSpiral250mm` chạy ca 250 mm, bước dọc trục 0,22 mm. Chạy thêm `LongSpiralCheck --stress` để thử hơn 5 triệu điểm trên mẫu tổng hợp. Xem số đo trong `NGHIEM_THU_FINAL.md`.

## Biên dịch

Trên Windows x64, dùng Developer PowerShell của Visual Studio 2022, Qt 6 MSVC, CMake và Inno Setup. Chỉnh đường dẫn Qt.

    .\scripts\Build-Windows.ps1 -QtRoot C:\Qt\6.8.3\msvc2022_64

Thêm -Rhino3dm để bật OpenNURBS tĩnh. Mặc định chỉ cần Qt 6 và thư viện chuẩn C++20. Trên Linux hoặc khi build qua CMake trực tiếp:

    cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt-kit>
    cmake --build build --config Release
    ctest --test-dir build -C Release -V

Có thể bật Rhino bằng -DARTISAN_RHINO=ON, dùng checkout đã ghim với -DFETCHCONTENT_SOURCE_DIR_ARTISAN_OPENNURBS=<checkout>. Workflow GitHub Actions bật OpenNURBS mặc định. Bản bàn giao đã được biên dịch và chạy kiểm thử trên Linux. Kết quả của Windows x64 cần được ghi nhận khi chạy workflow.

## Hệ tọa độ, post và kiểm chứng

- Tọa độ mũi lưu trong khung tức thời của bàn quay. B quay quanh trục ngang của khung đó. Với 4X, B = 0 và không có từ lệnh B. Y thay đổi trong từng ứng viên trước khi Drop-Cutter. Với bàn A dọc Y, hướng dọc và ngang đổi tương ứng.
- 5X hiện áp dụng máy bàn A + đầu B vuông góc. Mặc định tay đòn dọc trục dao trung hòa. Hồ sơ nâng cao nhận vectơ `headPivotFromTip` đã đo từ mũi dao đến tâm đầu B ở B=0, theo hệ Cartesian cục bộ có X dọc trục A và Z hướng lên. Vectơ này thay tay đòn mặc định khi tính kiểm chứng. Tọa độ cơ khí suy ra từ mũi, chênh lệch tay đòn so với B = 0 và workOffsetMachine [X,Y,Z] nhập trực tiếp ở Hồ sơ máy. Cả 4X và 5X chỉ ghi tệp NC khi người vận hành xác nhận offset và hồ sơ có giới hạn Z. Máy đầu–đầu, bàn–bàn và G43.5 chưa được mô hình hóa.
- Các profile CM600, Syntec và Fanuc xuất mẫu G43.4 H1/G49 theo quy ước XYZ mũi trong hệ phôi. Profile Siemens dùng TRAORI/TRAFOOF theo cùng mô hình. Profile không RTCP xuất XYZ đã bù chênh tay đòn. Các mẫu chưa được chạy trên firmware thực tế tương ứng. A/B xuất theo dấu cấu hình.
- CAMWorker tính cao trình từ bán kính quay của phôi và khoảng nhấc. Post phát lệnh Z độc lập trước lệnh di chuyển XY/A/B đầu tiên. G00 dịch ngang hoặc đổi A/B yêu cầu cao trình an toàn. G01 dùng cho cắt xoắn ốc đồng thời. Phần mềm giả định đầu dao ở tư thế trung hòa, gốc G54 đã thiết lập và hành trình nâng ban đầu không vướng đồ gá. Cần đối chiếu thực tế bật/hủy RTCP, hành trình, gốc máy, thủ tục thay dao và gá trước khi đưa vào sản xuất.
- Sau khi lưu, ứng dụng tự đọc lại chính tệp vừa ghi. **Đọc lại NC** cũng cho phép mở file đã lưu và xác minh với STL, dao và máy đang chọn. Bộ đọc chấp nhận tập con phát sinh của ArtisanAxis gồm G00/G01 tuyệt đối, nhấc Z ban đầu, lệnh bù, spindle, tool và kết thúc. Nó dựng lại từ số đã làm tròn trong NC, xét hành trình và kiểm tra sweep trên STL. Lỗi có số dòng. Chu trình tổng quát, arc và macro chưa được hỗ trợ. Mô hình nội suy dùng XYZ và góc đồng bộ trong khối. Quy tắc nội suy firmware vẫn cần đối chiếu.

## Đường dao, mô phỏng và nhật ký

- Preset 4 mặt xử lý toàn bộ lượt quét ở từng cao độ trước khi hạ lớp. STL và lượng dư xác định nơi được phép cắt. Khi giới hạn hành trình Z giữ lại vật liệu ở dưới, CAMWorker báo mức Z cục bộ trong nhật ký. Mặc định F phá mặt, phá tròn cận tinh và tinh lần lượt là 450, 550 và 600 mm/phút. Người vận hành có thể đặt tốc độ cho từng công đoạn.

1. Quy hoạch động trên đồ thị C-Space nhiều lớp tìm chuỗi A liên tục, Y lệch tâm và B dẫn hướng bởi pháp tuyến mặt bọc. Tọa độ mặt dẫn đặt tia chiếu. Drop-Cutter tìm tiếp xúc trên STL ở đúng tư thế ứng viên. Đặt Lead/Lean trong hộp thoại Đường dao 5X. Lõi ánh xạ hướng vào A/B khả thi, làm mượt bằng SLERP, chiếu lại tiếp xúc và kiểm tra hai đoạn nối trước khi nhận thay đổi. Bù cầu nằm trong nghiệm Drop-Cutter và post không cộng bán kính thêm. Điểm tiếp xúc tinh lấy đúng nghiệm Drop-Cutter 0,00 mm. Lõi chia nhỏ dây cung khi cần, giữ điểm chèn trên mặt bao tiếp xúc. Chỉ bộ đọc lại NC dùng dung sai 0,004 mm ở phần lưỡi chạm mẫu để đối chiếu NC 3 chữ số. Phần côn, thân và collet giữ kiểm tra riêng.
2. SDF hỗ trợ lọc khoảng cách, BVH kiểm tra mặt STL, bao côn chia vành và kiểm tra cùng bao thân/collet. Đoạn nối dùng mô hình nội suy của post, thử lướt thấp khi phù hợp. Cận dịch chuyển và chia thích nghi dùng để chứng minh khoảng hở. Nếu không nối được, tách lượt và lưu tọa độ, nguyên nhân trong **Nhật ký NC**. Vùng không tiếp cận được thống kê theo lớp không tìm được tư thế. Chưa có bản đồ chứng minh phủ toàn bộ mặt trong khe tượng thật.
3. **Nhập mặt Rhino** dùng pháp tuyến của mặt đầu tiên nội suy từ lưới tham số 49×49 vào bộ sinh hướng. Cần căn chung hệ tọa độ và phương tham số quanh thân/dọc trục. STL vẫn quyết định hình học tiếp xúc. Hồ sơ mặt nhập cần trùng định hướng với mẫu, chưa tự đăng ký hình học.
4. Chọn **Sau phá mặt / Sau cận tinh / Sau chạy tinh** để xem phôi còn lại. Màu cam là dư bề mặt, đỏ là voxel mục tiêu đã bị bóc. Bảng báo kích thước ô và số voxel. Voxel dư có thể bao gồm lượng dư chủ động. Phôi được mô phỏng trên các mẫu chuyển động đọc lại từ NC. Bộ mô phỏng phát hiện một số trường hợp thân/collet hoặc chạy nhanh quẹt phôi và khóa xuất nếu phát hiện. Đây là phép kiểm tra lấy mẫu theo độ phân giải voxel, chưa có chứng minh va chạm liên tục với phôi và chưa nhận hình CAD đồ gá.
5. Giới hạn quy mô đồ thị có báo lỗi. Dùng `tools/qa-dilac` để chạy bài thử chẩn đoán trên STL thật. Số liệu lần bàn giao và các bước còn cần nghiệm thu nằm trong NGHIEM_THU_FINAL.md.

Bảng đối chiếu bản nền và thiết kế nâng cấp nằm trong DOI_CHIEU_LOI_CAM.md. Số liệu từ dự án và chương trình đã chạy thử của xưởng nằm trong DOI_CHIEU_CM600.md. Hướng dẫn cấu hình căn bản và chế độ phân độ cũ có trong README_MOC2.md. Kết quả của Mốc 3 được lưu riêng ở KIEM_THU_MOC3.md.

Xem NGHIEM_THU_FINAL.md và HUONG_DAN_CAI_DAT_VA_CAP_NHAT.md để phân biệt các kết quả đã chạy với phạm vi cần Windows/GitHub và máy thật.

Dữ liệu phiên làm việc (liên kết STL/mặt dẫn, biến đổi, phôi, dao, đường dao và thiết lập) và hồ sơ máy lưu theo người dùng ngoài thư mục cài đặt. Kết quả NC đã tính không được tự khôi phục để tránh dùng chung với một cấu hình mới.
