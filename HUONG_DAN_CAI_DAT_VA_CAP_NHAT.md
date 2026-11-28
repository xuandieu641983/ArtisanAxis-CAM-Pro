# Cài đặt và cập nhật ArtisanAxis CAM Pro

## Tại xưởng

1. Tải **Setup.exe** từ GitHub Releases của xưởng. Chạy bộ cài và mở biểu tượng **ArtisanAxis CAM Pro** trên Desktop. Máy xưởng không cần Visual Studio hay bộ SDK Qt. Windows có thể hỏi quyền quản trị nếu cần cài Visual C++ Runtime đi kèm.
2. Khi có nút **Có bản mới**, bấm để đọc ghi chú. Dùng **Cập nhật 1-Click** để kiểm tra theo yêu cầu. Khi CAMWorker đã tính xong, bấm **Sao lưu và cài bản mới** để xác nhận. Trình cập nhật chuẩn bị bộ cài trước khi đóng ứng dụng, rồi mở lại sau khi cài. Nếu bản mới không xác nhận khởi động, trình cập nhật khôi phục thư mục và thông tin gỡ cài đặt bản trước.
3. Giữ STL, dự án và NC ngoài thư mục cài. Hồ sơ máy dùng thư mục cấu hình người dùng.

## Tạo Setup.exe và phát hành

1. Đưa **nội dung đã giải nén trong thư mục mã nguồn**, bao gồm **.github**, vào thư mục gốc của kho GitHub. Phiên bản lấy từ `CMakeLists.txt`, mỗi phiên bản chỉ phát hành một lần.
2. Trong **Actions → Build Windows x64 and Release → Run workflow**, để **publish** tắt nếu chỉ lấy bộ cài thử. Giữ **rhino** bật để nhập mặt dẫn 3DM. Sau khi job đạt, tải artifact chứa **Setup.exe**, **update-feed.json** và **Setup.exe.sha256**.
3. Bật **publish** để đăng lên GitHub Releases từ kho công khai. Có thể để trống secret ký mã. Nếu xưởng cấu hình **ARTISANAXIS_SIGNING_PFX_BASE64** và mật khẩu **ARTISANAXIS_SIGNING_PFX_PASSWORD** khi cần, workflow tự ký và bật kiểm tra Authenticode cho bộ cài của kênh đó. Các bản cập nhật của bộ cài đã ghim chứng chỉ phải tiếp tục dùng cùng chứng chỉ.

## Cấu hình Cập nhật

Mở **Ứng dụng → Cấu hình Cập nhật**. Có thể bật hoặc tắt kiểm tra khi mở ArtisanAxis. Việc kiểm tra chỉ hiện thông báo. Cài đặt cần xác nhận riêng.

| Nguồn | Giá trị cần nhập |
| --- | --- |
| Mặc định của bản cài | Dùng kho GitHub hoặc địa chỉ Google Sheets ghi trong bộ cài. |
| GitHub Releases | Tên kho theo dạng `chủ-sở-hữu/tên-kho`. Bản phát hành cần có `Setup.exe` và `update-feed.json`. Ghi chú lấy từ manifest hoặc nội dung Release. |
| Google Sheets | URL CSV công khai HTTPS trên docs.google.com. Bảng có cột `channel,version,url,sha256`, có thể thêm `notes`, và có đúng một dòng `stable`. |
| Manifest JSON qua HTTPS | URL của `update-feed.json`. Manifest có `channel` bằng `stable`, `version` dạng `1.2.0`, `url` HTTPS, `sha256` của bộ cài và trường `notes` tùy chọn. |
| Thư mục gói cập nhật | Thư mục có `Setup.exe` và `update-feed.json` đi cùng một bản phát hành. Giải nén hai tệp vào cùng thư mục trước khi chọn. |

Dùng nguồn của xưởng. Trình cập nhật chỉ cài phiên bản mới hơn, đối chiếu SHA-256 của bộ cài và xác minh Authenticode khi bản đang cài ghim chứng chỉ. Cấu hình người dùng nằm ở `%LOCALAPPDATA%\XuanDieuCNC\ArtisanAxis\update_config.json`. Dữ liệu và hồ sơ máy được giữ ngoài thư mục cài.

ArtisanAxis sao lưu bản đang dùng trước khi cài. Chọn **Giữ bản sao sau khi cập nhật thành công** nếu muốn lưu phiên bản trước cạnh thư mục cài. Tên thư mục có hậu tố `.backup-v<phiên bản>-<mã>` và chứa bản xuất thông tin gỡ cài đặt. Nhật ký cài đặt và đường dẫn bản sao nằm ở `%LOCALAPPDATA%\XuanDieuCNC\ArtisanAxis\UpdateLogs`.
