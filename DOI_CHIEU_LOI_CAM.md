# Đối chiếu và thiết kế nâng cấp lõi ArtisanAxis CAM Pro

Đường cơ sở là mã native C++20 và Qt 6 của ArtisanAxis v1.1.0. Bản HTML v6.2 của xưởng được dùng để đối chiếu các thao tác và cách biểu diễn Surface. SHA-256 của bản HTML mới đính kèm trùng bản đã lưu trong dự án.

| Mảng | Có trong bản nền | Điểm cần nâng cấp |
|---|---|---|
| Mặt dẫn và quy trình 4X | `cam/wrap.cpp` cắt mesh qua BVH, lấy bao lồi, dựng B-spline tuần hoàn và pháp tuyến giải tích. CAMWorker có lệnh dựng, đọc 3DM và vẽ lưới. Raster, phá tròn và chạy tinh có sẵn trong preset. | Bao tự dựng cố định theo X, chưa dùng đầy đủ trục và tâm quay từ hồ sơ máy. Tiết diện khuyết có thể bị thay bằng hình hộp. Thiếu tay điều khiển hiện/ẩn trực tiếp tại cây. |
| Động học và chống gouge | `cam/motion.cpp` chiếu Drop-Cutter lên STL, duyệt đồ thị ứng viên A/Y/B, kiểm tra các span thân dao, làm mịn dây cung và kiểm chứng NC. A xoắn ốc mở liên tục. | Thiếu Lead/Lean đặt được. Góc B lấy từ một tập ứng viên nhỏ. Cần làm mượt hướng đã chọn, chiếu lại lên STL và kiểm chứng lại theo nội suy của post. Phải đánh giá mũi, thân và collet trên chuyển động, đồng thời kiểm tra phôi còn lại khi mô phỏng. |
| CAMWorker và post | QProcess tách khỏi UI, BVH chia sẻ giữa luồng, đồ thị theo cửa sổ, tiến độ JSON, hủy tiến trình, xuất và đọc lại XYZA/XYZAB. Post 5X CM600 kích hoạt RTCP G43.4. | Raster hiện duyệt các độ sâu bên trong từng điểm, cần tổ chức lại thành lớp Z. Các thông số nhấc, F và an toàn phải đi xuyên suốt từ hồ sơ/lượt đến tệp NC. Cần bài tích hợp đủ ba giai đoạn. |
| Cập nhật | Có GitHub Releases/Google Sheet, so bản, SHA-256 và xác minh Authenticode khi ghim. Helper dùng bản sao dự phòng và khôi phục khi lần khởi động mới thất bại. | Nút cũ áp dụng ngay, chưa kiểm tra nền lúc mở phần mềm, chưa hiện nội dung nâng cấp và chỉ sửa được URL CSV. Cần nguồn HTTPS/thư mục cập nhật và chế độ giữ bản sao sau thành công. |

## Kiến trúc nâng cấp

Giữ `Workshop`, cây dự án, OpenGL, hồ sơ máy, cơ chế QProcess CAMWorker, mô phỏng và xem NC. Mặt dẫn được dựng trong hệ trục quay và trả về tọa độ mô hình để hiển thị. Hình học STL vẫn là chuẩn cho tiếp xúc và kiểm tra khoét phạm.

CAMWorker đọc tham số từng lượt, tính tiếp xúc thật tại tư thế ứng viên, kiểm tra chuyển động và chỉ giao tệp NC qua bộ kiểm chứng. Với bàn A và đầu B, Lead/Lean phải được biểu diễn bằng hai bậc tự do của máy và xác minh sau khi giới hạn góc. Đường nội suy mượt dùng khi chọn điểm đích. Bộ đọc NC vẫn kiểm tra theo quy tắc nội suy dự kiến của bộ điều khiển.

Nguồn cập nhật do người vận hành chọn trong cửa sổ cấu hình. Kiểm tra lúc mở là thao tác chỉ đọc. Sao lưu, xác minh bộ cài và phục hồi vẫn nằm ở tiến trình cài đặt riêng.

## Phạm vi chứng minh tương đương

Tài liệu Autodesk công khai mô tả Rotary Finishing, Lead/Lean và tự nghiêng tránh va chạm thân/bầu. Tài liệu Cyclmotion ghi nhận hỗ trợ RTCP G43.4. Bản HTML v6.2 thể hiện workflow mà xưởng sử dụng. Những nguồn này xác định hành vi để đối chiếu chức năng. Mốc nghiệm thu cần thêm tệp STL thật, dữ liệu máy và NC tham chiếu của xưởng để định lượng độ phủ, chất lượng đường và khả năng chạy trên máy.  
[Rotary Finishing](https://www.autodesk.com/support/technical/article/caas/sfdcarticles/sfdcarticles/How-to-create-a-4-axis-toolpath-in-PowerMill.html) · [Tự nghiêng tránh va chạm](https://help.autodesk.com/cloudhelp/2022/ENU/PWRM-ReferenceHelp/files/GUID-35CFC2A8-553F-43CB-9909-67C08927B3DA.htm) · [Tài liệu Cyclmotion](https://www.cyclmotion.com/static/upload/file/20240608/1717813547753431.pdf)

Video công khai về HongFa NC Programming System 1.73 mô tả luồng chiếu mặt 4 trục. Đối chiếu này dùng thao tác quan sát được và mã v6.2 do xưởng gửi.  
[Ví dụ chiếu mặt 4 trục HongFa](https://www.youtube.com/watch?v=sOLswyPXoEo) · [Projection Surface Finishing của PowerMill](https://help.autodesk.com/cloudhelp/2024/ENU/PWRM-ReferenceHelp/files/GUID-4189AB05-DFE0-4614-B4B5-C17532627CBA.htm)


## Đối chiếu bổ sung về kiểm chứng

Autodesk phân biệt kiểm tra thân và bầu dao với mô hình và kiểm tra với cả phôi đang gia công. Kiểm tra tự động có thể tạo khoảng trống trên đường dao khi gặp vùng không an toàn. ArtisanAxis vì vậy giữ hai kiểm tra riêng, gồm bao dao liên tục trên STL và mô phỏng vật liệu còn lại. Kết quả chưa chứng minh được khoảng hở vẫn khóa xuất NC.  
[Tài liệu Automatic Verification của Autodesk](https://help.autodesk.com/cloudhelp/2024/ENU/PWRM-ReferenceHelp/files/GUID-10D4F2FF-1D84-4F7C-8BD7-DF14C9FD5D1E.htm)

Trang hãng Cyclmotion cung cấp tài liệu bộ điều khiển. RTCP G43.4 và các mục thiết lập giới hạn mềm là chức năng được tài liệu mô tả. Offset và giới hạn của máy lắp tại xưởng cần lấy từ chính cấu hình máy để nghiệm thu.  
[Tài liệu phần mềm Cyclmotion](https://www.cyclmotion.com/static/upload/file/20240608/1717813547753431.pdf) · [Trung tâm tài liệu và phần cứng CM600](https://www.cyclmotion.com/Download/)
