# ArtisanAxis CAM Pro · Mốc 2 · Xuân Diệu CNC

Mã nguồn native C++20 / Qt 6 trên nền Mốc 1. Worker sinh các lượt chạy raster 2/4 mặt và các lượt xoay phân độ 4X trên tâm, dựng lớp bọc hoặc nhập mặt Rhino. Nút Xuất NC tạo tệp NC đối chiếu từ phiên tính đang còn hiệu lực. Mở mẫu, chọn preset, lưu hộp thoại và thay đổi máy chỉ sửa cấu hình, không khởi động tính đường dao.

## Build Windows x64

Từ Developer PowerShell của Visual Studio 2022 có Desktop development with C++, cài Qt 6 MSVC x64, CMake và Inno Setup 6. Chỉnh đường dẫn Qt theo máy.

```powershell
.\scripts\Build-Windows.ps1 -QtRoot C:\Qt\6.8.3\msvc2022_64
```

Thêm `-Rhino3dm` để tải và biên dịch tĩnh openNURBS ở commit đã ghim. Cần Git và mạng khi tải lần đầu. Có thể dùng một checkout đúng commit với `-DFETCHCONTENT_SOURCE_DIR_ARTISAN_OPENNURBS=<checkout>` khi chạy CMake. Lõi BVH, SDF, Drop-Cutter, B-spline, phôi còn lại và post đều dùng C++20 trong gói. Bản mặc định chỉ cần Qt 6, không yêu cầu Embree, OpenVDB, VTK hoặc DLL của openNURBS. Script đóng gói ArtisanAxis.exe, CAMWorker.exe và Setup.exe sau khi chạy kiểm thử. Gói này bàn giao mã nguồn, bộ cài Windows chưa được chạy trên máy Windows trong lần kiểm tra này.

## Thử trong giao diện

1. Mở STL. Chọn máy có bàn A dọc X hoặc Y, đo tâm quay và chiều quay, chọn post 4X. Chọn chế độ 4 trục ở Trợ lý, rồi chọn bộ mẫu 2/4 mặt.
2. Đo và nhập chiều dài lưỡi, chiều nhô và đường kính collet tại Dao. Cập nhật phôi, điểm gốc và các công đoạn. Mục Đường dao cho phép bật/tắt từng lượt.
3. **Tính 4X** chạy CAMWorker. Thanh trạng thái báo số điểm và số vị trí bị bộ lọc loại. Khung nhìn nạp các đoạn xem trước, giảm mật độ hiển thị nếu vượt giới hạn khung nhìn.
4. **Thiết lập → Dựng mặt bọc STL** hiển thị lớp bọc bán trong suốt. Bản build có Rhino cho phép **Nhập mặt Rhino .3dm**. Hỗ trợ mặt độc lập và mặt BRep phủ toàn miền tham số, đổi đơn vị sang mm. BRep có biên cắt tùy ý và instance được bỏ qua. Mặt nhập hiện là lớp hiển thị trong hệ tọa độ của tệp.
5. **Xuất NC** lưu tệp đối chiếu sau khi xác nhận thông báo. Kiểm tra gá, gốc máy, hành trình, tốc độ, thứ tự thay dao và phương ngữ trên máy thật trước mọi quyết định chạy máy.

## Thuật toán và phạm vi xác nhận

- BVH phẳng chia song song các nhánh trên của cây và tái sử dụng cây khi truy vấn ở góc khác. Drop-Cutter cầu/dao đáy phẳng lấy cực đại trên mặt, cạnh và đỉnh. Với cầu, nghiệm trả thẳng tọa độ **mũi**. Không cộng bù cầu lần nữa ở bộ xuất.
- Phá mặt chạy 0°, 180°, thêm 90° và 270° ở chế độ 4 mặt. Phá 2 mặt có thể tới −1 mm ở nơi đạt kiểm tra. Mốc này mô hình hai vùng giữ phôi ở đầu trục với vùng dự trữ 2 mm cộng bao collet, không nhận hình CAD gá thật. Đường xoay tại tâm ngang bằng 0 trong hệ quay. R góc chỉ dùng để định hướng lấy mẫu và xác định mức nhấc, không làm sàn giới hạn Z khi xoay.
- Kiểm tra cầu theo Drop-Cutter, côn theo các vành bao, thân và collet bằng truy vấn BVH và phôi còn lại. Phôi voxel chỉ bỏ ô đã được chứng minh nằm trọn trong thể tích lưỡi đi qua. Mô hình có thể loại dư một số tư thế khi ô bao phủ chỗ vừa cắt hoặc phần bao chạm STL. Đây là kiểm tra hình học của mô hình đã khai báo, không phải chứng nhận va chạm cho máy thực tế. Lưới SDF lấy dấu theo chẵn lẻ cho vật thể kín, có bài thử riêng và chưa tham gia quyết định an toàn.
- Để luôn nhấc trước khi đổi A, Mốc 2 chạy **4X phân độ**. Với dao đáy phẳng, có thể nối điểm cùng cao độ khi đĩa bao cả đoạn đạt kiểm tra, nếu không dao rút lên. Mốc này chưa nội suy A đồng thời khi đang cắt. Với chương trình lớn, giới hạn số điểm có cảnh báo. Chưa đo thời gian toàn chương trình với tượng 1–3 triệu tam giác của xưởng.
- Mặt bọc tạo từ lát X, bao lồi YZ và B-spline bicubic tuần hoàn/kẹp hai đầu, trọng số bằng 1. Taubin giảm co trên lưới điều khiển, không bảo đảm giữ từng tiết diện hoặc thể tích. Đạo hàm riêng của spline cho pháp tuyến giải tích. Mặt này dẫn hướng/xem trước, điểm tiếp xúc 4X vẫn lấy trên STL.
- Có bảy lựa chọn đầu ra 4X, mỗi lựa chọn đi qua bài kiểm tra header, từ lệnh trục, đổi góc, dừng/thay dao và kết thúc. Các lựa chọn hiện dùng tập lệnh XYZA chung, khác ở chế độ thay dao tay/tự động. Chưa đối chiếu đầy đủ modal, chức năng thay dao, đơn vị và dialect trên từng bộ điều khiển. Tọa độ Z nhấc là theo gốc công việc, chưa kiểm chứng giới hạn tọa độ máy từ G54 và offset dao. Tệp ghi NC REVIEW ONLY. Mô phỏng kiểm chứng toàn quỹ đạo và đồ gá thực để dành cho bước tiếp theo.

## Kiểm tra lại

```text
cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt-kit>
cmake --build build --config Release
ctest --test-dir build -C Release -V
```

Ba nhóm kiểm thử gồm cấu hình/phôi, hình học/7 đầu ra NC và luồng worker từ STL mẫu đến NC/mặt bọc. Mẫu STL kín `tests/workshop_block.stl` dùng cho kiểm tra worker. Số liệu chạy lần bàn giao có trong `KIEM_THU.md`. Các luồng cấu hình máy và cập nhật Mốc 1 có thêm thông tin trong `README_MOC1.md`.

Tài liệu thư viện đọc 3DM: [McNeel openNURBS](https://github.com/mcneel/opennurbs).
