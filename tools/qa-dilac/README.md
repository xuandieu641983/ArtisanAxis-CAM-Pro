# Thử CAMWorker trên Di Lặc 250

Dùng bản CAMWorker Release và STL nhị phân do xưởng cung cấp. Trình thử này lưu yêu cầu, phản hồi và tiến độ riêng theo từng chế độ. Tọa độ mẫu được tỉ lệ riêng theo X về 250 mm, giữ nguyên bề ngang và chiều cao. Hồ sơ kiểm tra giữ offset CM600 tham chiếu ở trạng thái chưa xác nhận, nên kết quả dùng để xem trước và kiểm tra.

```powershell
python tools/qa-dilac/run.py --worker build/Release/CAMWorker.exe --stl "di lac 250.stl" --out qa-dilac --stage surface
python tools/qa-dilac/run.py --worker build/Release/CAMWorker.exe --stl "di lac 250.stl" --out qa-dilac --stage full --axes 4
python tools/qa-dilac/run.py --worker build/Release/CAMWorker.exe --stl "di lac 250.stl" --out qa-dilac --stage full --axes 5
```

`--stage strip` kiểm tra một dải X dài 5 mm ở giữa mẫu với bước tinh 0,22 mm. `--stage raster` kiểm tra bốn mặt trước khi làm thử toàn bộ quy trình.

Dùng `--lead 5 --lean 8` để thử góc dẫn trong chế độ 5X. `--point-spacing 1.5` đổi khoảng cách lấy mẫu quanh chu vi và vẫn giữ bước dọc 0,22 mm. Có thể thêm `--clip 120 125` để giới hạn các lượt cắt vào một dải kiểm tra. Ghi rõ các tùy chọn này khi so sánh thời gian hoặc độ phủ.

Để đăng ký hai ca toàn chiều dài trong CTest, cấu hình thêm `-DARTISAN_DI_LAC_STL="C:/mau/di lac 250.stl"` và chạy `ctest --test-dir build -C Release -L real-stl -V`. Nhóm mở rộng báo lỗi khi bộ kiểm chứng từ chối. Tệp STL được đọc tại đường dẫn đã chọn.

Dùng `--pitch 2.5` cho lượt chẩn đoán toàn hình với bước dọc thưa. Ca này kiểm tra các liên kết và giới hạn nhưng chưa đánh giá chất lượng tinh ở bước 0,22 mm.

`results-v1.2.0.json` ghi kết quả lần nghiệm thu và các dòng NC bị bộ kiểm chứng báo lỗi trên hồ sơ tham chiếu. Phần trăm tiến độ cuối chỉ là mốc đã chạy của ca chưa hoàn tất.
