# ArtisanAxis CAM Pro · Mốc 1 · Xuân Diệu CNC

Mã nguồn C++20 cho ứng dụng desktop Qt 6 Widgets và tiến trình CAMWorker. Mốc 1 tập trung vào dữ liệu và thao tác trên máy tính. Nút **Tính · kiểm tra STL** gửi một bản chụp cấu hình tới tiến trình riêng và kiểm tra lưới nguồn. CAMWorker ở mốc này đọc STL và tính hộp bao trên hai luồng. Nó chưa sinh đường dao hay tệp NC.

## Windows x64

Yêu cầu Visual Studio 2022 với Desktop development with C++, Qt 6 cho MSVC x64 (khuyến nghị Qt 6.8), CMake và Inno Setup 6 bản hiện hành. Chạy từ Developer PowerShell. Dùng đường dẫn thư mục Qt thực tế trên máy.

```powershell
.\scripts\Build-Windows.ps1 -QtRoot C:\Qt\6.8.3\msvc2022_64
```

Script biên dịch `ArtisanAxis.exe` và `CAMWorker.exe`, gom thư viện Qt và gọi Inno Setup để tạo `dist\Setup.exe`. Bộ cài dành cho từng người dùng và tạo lối tắt trên Desktop. Có thể ký cả hai EXE và bộ cài bằng `-CertificateThumbprint` với `signtool.exe`. Kiểm tra điều kiện phân phối Qt trước khi phát hành.

### Luồng thao tác Mốc 1

1. **Mở STL** nhận STL ASCII hoặc nhị phân, tọa độ được hiểu là mm. Dùng chuột trái để quay, chuột phải để dịch khung nhìn và con lăn để zoom.
2. **Biến đổi** chỉnh xoay, tỷ lệ XYZ, tịnh tiến và đối xứng. **Lấy gốc X0** dịch đáy theo Min X về 0 và căn giữa Y/Z. **Hoàn tác** khôi phục từng phép biến đổi của mẫu đang mở. Cấu hình đường dao phụ thuộc được đánh dấu cần tính lại.
3. **Phôi** chỉnh hình hộp, trụ hoặc chóp cụt. Sửa kích thước giữ Min cố định và cập nhật Max. Nở +mm dịch mỗi Min ra ngoài và mỗi Max ra ngoài đúng số mm ấy. Trụ/chóp cụt có tiết diện tròn khi hai kích thước ngang bằng nhau, nếu khác nhau là tiết diện elip. Xem trước biến đổi trực tiếp, Hủy trả hình phôi về cấu hình đã lưu. R góc tính từ tâm và hướng quay của hồ sơ máy. Với trục nghiêng tổng quát, tính cận trên bảo thủ cho tiết diện cong. Nhấc tối thiểu dùng chung là R + 50 mm.
4. **Máy / Post** chọn máy hoặc nhân bản preset. Sửa hình học, tâm, hướng xoay, dấu, tay đòn, hành trình và post trong thẻ nâng cao. Tâm/hướng là tọa độ tư thế 0 trong hệ phôi chung. CM600 Mode #7 là hồ sơ mặc định tham chiếu. Giá trị null nghĩa là chưa đo. Danh mục post phân loại 4X theo XYZA và 5X theo XYZAB. M1 chỉ có danh mục, cấu hình tương thích và xác thực dữ liệu, chưa có bộ sinh G-code của từng hãng.
5. **Trợ lý** dùng bộ nhận dạng câu tiếng Việt theo quy tắc chạy offline. Bấm **Đọc mô tả** để phân tích. Các ô đã chỉnh tay khóa đề xuất tự động, còn **Lưu & Đóng** dùng giá trị đang hiển thị. Ví dụ: “Phá 2 mặt, 4 trục, dao phá Ø10 dài 45 mm, tinh R0.4 góc 15, bước tinh 0.20 mm”. Chiều L được ghi vào cả overhang/outLen. Góc côn là góc một bên. Trợ lý chọn trong các post tương thích được khai báo trên hồ sơ máy.
6. Cây dự án mở Model, Stock, Tool Library, Surface, Path Settings, Machine/Post và AI. Bộ mẫu phá 2/4 mặt tạo cấu hình, nút **Tính** mới chạy worker. Các phần chưa chuyển từ v6.2 gồm nhập OBJ/.3dm, dựng Wrap, xác minh và xuất NC, mô phỏng, thư mục theo dõi ArtForm và một số thao tác phụ trên thanh công cụ.

### Cập nhật một lần bấm

Trước khi đóng gói, đặt nguồn phát hành và dấu vân tay SHA-1 của chứng chỉ ký mã trong `scripts/update-feed.json`. Tên cột Google Sheet CSV là `channel,version,url,sha256`, với một dòng `stable`. Nguồn GitHub cần release công khai, tag dạng `vX.Y.Z`, asset `Setup.exe` và SHA-256 digest từ API. Nếu khai báo cả hai, GitHub được ưu tiên.

Menu **Ứng dụng → Cập nhật phiên bản** kiểm tra phiên bản rồi tải, xác minh SHA-256, chữ ký Authenticode hợp lệ, chứng chỉ đúng và phiên bản của bộ cài đã ký. Chỉ đóng app sau khi chuẩn bị xong, sau đó trình trợ giúp đợi tiến trình kết thúc, chạy cài đặt và khởi động lại nếu thành công. Nguồn và chứng chỉ để trống trong mã bàn giao vì chưa có cấu hình phát hành của xưởng. Chu trình installer/updater và phục hồi phiên bản cần kiểm thử trên Windows trước khi phát hành. Phiên bản M1 chưa có rollback tự động.

## Cấu trúc và các mốc còn lại

| Mốc | Triển khai và điều kiện hoàn thành |
|---|---|
| 1 · Desktop và cấu hình | CMake, Qt/OpenGL native, STL, phôi tham số, hồ sơ 4/5 trục, cây dự án, cấu hình dao và luồng Tính qua worker. Xác nhận kích thước phôi, Undo, cập nhật phụ thuộc và kiểm thử bản cài Windows. |
| 2 · Hình học và 4X | Kết nối Embree/BVH cho STL gốc, Drop-Cutter, phôi sau phá và hồ sơ động học 4X. Bổ sung openNURBS cho Wrap/.3dm. Đối chiếu mẫu chuẩn có kích thước biết trước và từng post 4X, kiểm tra NC không có từ lệnh B. |
| 3 · 5X và mô phỏng | Kết nối OpenVDB/SDF, kiểm tra thể tích quét, tối ưu hướng dao, mô phỏng bóc vật liệu, động học RTCP và không RTCP. Xác nhận riêng từng cặp hồ sơ máy/post bằng tệp chuẩn, đọc lại NC, chạy khô có giám sát. |
| 4 · Phát hành | Hoàn tất công cụ v6.2 còn thiếu, đo STL 1–3 triệu tam giác trên PC xưởng, kiểm thử bộ cài ký số/cập nhật/rollback. |

M1 dùng OpenGL trực tiếp cho viewport. Tùy chọn `-DARTISAN_DISCOVER_CAM_SDK=ON` kiểm tra Embree, TBB, OpenVDB và openNURBS khi chuẩn bị môi trường tích hợp các mốc hình học. Các mô-đun tương ứng chưa được liên kết vào tính toán M1.

## Kiểm tra từ mã nguồn

```text
cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt-kit>
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`tests/sample.stl` là mẫu hình học tối giản để kiểm tra đọc lưới và worker. M1 chỉ dùng để thử giao diện, hồ sơ và cấu hình chuẩn bị gia công.
