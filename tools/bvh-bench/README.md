# Đo BVH với STL 1–3 triệu tam giác

Bài đo dùng trình đọc STL của ArtisanAxis và lớp `cam::Tree` từ mã nguồn hiện tại. Tệp thử là STL nhị phân của mặt lượn sóng tổng hợp 100 × 45 mm, với mật độ chia lưới tăng theo số tam giác. Script sinh tệp tạm và xác minh cả kích thước tệp lẫn số tam giác thực đọc được.

## Chạy lại

Cần CMake, bộ biên dịch C++20, Qt 6 Core và Python 3. Đứng trong thư mục gốc mã nguồn.

```text
cmake -S tools/bvh-bench -B build-bvh -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<thu_muc_Qt>
cmake --build build-bvh --config Release
python tools/bvh-bench/run.py build-bvh/ArtisanBVHBenchmark --repeat 3
```

Với bộ tạo Visual Studio trên Windows, truyền đường dẫn `build-bvh/Release/ArtisanBVHBenchmark.exe` cho script và để thư mục `bin` của Qt trong PATH. Mỗi kích thước cần tối đa khoảng 150 MB thư mục tạm cho STL.

## Cách tính

- Mỗi kích thước chạy ba tiến trình độc lập. Số liệu thời gian là trung vị, đỉnh RAM là giá trị lớn nhất theo báo cáo hệ điều hành.
- Đồng hồ dựng BVH bắt đầu sau đọc STL, chuyển sang tam giác tọa độ double và giải phóng bộ đệm float. Peak RSS bao gồm các bước đó và toàn bộ tiến trình. Windows lấy Peak Working Set, Linux lấy Peak RSS.
- Sau khi dựng cây, đo đúng 10.000 truy vấn Drop-Cutter cầu R0,40 mm độc lập. Các tọa độ truy vấn giả ngẫu nhiên có hạt giống cố định và được giữ như nhau ở mọi kích thước. Kết quả truy vấn phải hữu hạn.
- STL được tạo ngay trước khi chạy nên kết quả đọc có thể được hưởng bộ đệm tệp của hệ điều hành. Số liệu này chỉ mô tả phần đọc lưới, dựng cây và truy vấn, chưa đo thời gian hoặc RAM của toàn chương trình CAM.

## Kết quả đã đo

Linux x86_64, GCC 14.2, Release, AMD EPYC 9V74. Chi tiết từng lần chạy trong `results-linux-x64.jsonl`.

| Tam giác | Đọc và chuyển đổi | Dựng BVH | 10.000 truy vấn | Truy vấn/giây | RAM đỉnh |
|---:|---:|---:|---:|---:|---:|
| 1.000.000 | 0,048 s | 0,100 s | 0,347 s | 28.815 | 110,16 MiB |
| 2.000.000 | 0,109 s | 0,219 s | 0,613 s | 16.318 | 213,16 MiB |
| 3.000.000 | 0,188 s | 0,351 s | 0,801 s | 12.486 | 316,16 MiB |
