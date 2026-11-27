# Cài đặt và cập nhật ArtisanAxis CAM Pro

## Tại xưởng

1. Tải **Setup.exe** từ GitHub Releases của xưởng. Chạy bộ cài và mở biểu tượng **ArtisanAxis CAM Pro** trên Desktop. Máy xưởng không cần Visual Studio hay bộ SDK Qt. Windows có thể hỏi quyền quản trị nếu cần cài Visual C++ Runtime đi kèm.
2. Bấm **Cập nhật 1-Click** khi CAMWorker đã tính xong. Nếu có bản mới hợp lệ, ứng dụng tự đóng, cài đè rồi mở lại. Nếu bản mới không xác nhận khởi động, trình cập nhật khôi phục thư mục và thông tin gỡ cài đặt bản trước. Nhật ký nằm ở `%LOCALAPPDATA%\XuanDieuCNC\ArtisanAxis\UpdateLogs`.
3. Giữ STL, dự án và NC ngoài thư mục cài. Hồ sơ máy dùng thư mục cấu hình người dùng.

## Tạo Setup.exe và phát hành

1. Đưa **nội dung đã giải nén trong thư mục mã nguồn**, bao gồm **.github**, vào thư mục gốc của kho GitHub. Phiên bản lấy từ `CMakeLists.txt`, mỗi phiên bản chỉ phát hành một lần.
2. Trong **Actions → Build Windows x64 and Release → Run workflow**, để **publish** tắt nếu chỉ lấy bộ cài thử. Bật **rhino** nếu cần nhập 3DM. Sau khi job đạt, tải artifact chứa **Setup.exe**, **update-feed.json** và **Setup.exe.sha256**.
3. Bật **publish** để đăng lên GitHub Releases từ kho công khai. Có thể để trống secret ký mã. Nếu xưởng cấu hình **ARTISANAXIS_SIGNING_PFX_BASE64** và mật khẩu **ARTISANAXIS_SIGNING_PFX_PASSWORD** khi cần, workflow tự ký và bật kiểm tra Authenticode cho bộ cài của kênh đó. Các bản cập nhật của bộ cài đã ghim chứng chỉ phải tiếp tục dùng cùng chứng chỉ.

## Nguồn Google Sheet tùy chọn

Tạo bảng CSV công khai trên docs.google.com với các cột channel,version,url,sha256 và đúng một dòng stable (phiên bản dạng 1.0.1). URL trỏ tới bộ cài HTTPS của xưởng. Nhập liên kết CSV tại **Ứng dụng → Nguồn thông báo Google Sheet**. Để trống để trở về GitHub. Bộ cài tải về luôn phải khớp SHA256 và có phiên bản mới hơn. Kiểm tra thêm chữ ký khi bản đang cài đã ghim chứng chỉ. Với kênh SHA256, hãy dùng kho hoặc bảng CSV do xưởng kiểm soát vì mã băm lấy từ nguồn cập nhật.
