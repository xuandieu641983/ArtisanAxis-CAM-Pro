# Đối chiếu hồ sơ và chương trình đã chạy thử trên CM600

Ngày 28/11/2026 · Xuân Diệu CNC

Phân tích chỉ đọc trên `thong so moi tinh chinh.mprj`, `mat phat to 5 truc mui 1.tap` và `Văn bản đã dán (1).txt`. Hai tệp NC có cùng SHA-256. Các nhóm tham số dưới đây đọc từ dữ liệu nhị phân có nhãn đi kèm trong bản sao dự án. Không dùng các dòng chương trình, nhật ký lỗi hay nhãn trống để suy ra hiệu chuẩn.

## Nhóm tham số trong dự án

| Trường có nhãn trong MPRJ | Giá trị đọc được |
|---|---|
| Machining structure | **7**. Tài liệu Cyclmotion mô tả 7 là bàn A quay và đầu B nghiêng. |
| Swingarm length | **232,9175 mm** |
| Pendulum X axis eccentricity | **78,7 mm**. Giá trị lưu của Y và Z là 0. |
| 4th axis coordinate bias XYZ | **249,8568 / 455,2568 / −468,6725 mm** |
| G54 coordinate system zero offset XYZ | **399,0737 / −0,8894 / −214,8150 mm** |
| 5th axis coordinate bias XYZ | **0 / 0 / −40 mm** |
| Dải Z trong bảng trục | **−480 đến +3 mm** |
| Dải A và B trong bảng trục | A **−180° đến +180°**, B **−180° đến +180°**. Trường bật giới hạn mềm trong các bảng trục này lưu giá trị 0. Đây là giá trị trong tệp, trạng thái hiệu lực cần đối chiếu khi máy đang hoạt động. |

Tâm bàn quay A và gốc G54 là hai bộ dữ liệu riêng. Khoảng cách từ mũi dao đến tâm B ở B=0 còn cần xác định theo dao lắp thực tế. Số lệch tâm 78,7 mm và độ lệch trục thứ năm −40 mm không được cộng tự động thành một vectơ. Tài liệu hãng tách bước đặt gốc trục quay và đặt G54, đồng thời yêu cầu hiệu chỉnh theo kết cấu máy.  
[Tài liệu Cyclmotion, phần cấu trúc và hiệu chuẩn máy](https://www.cyclmotion.com/static/upload/file/20240608/1717813547753431.pdf)

## Chương trình đã chạy thử

- **152.738 dòng**, post phát G54, G90, G94 và G43.4 H1.
- A bắt đầu **−177,044°**, kết thúc **−152.366,041°**, tương đương **422,747 vòng** tích lũy trong tệp.
- B được lập trình từ **−20,001° đến +20,000°**. Phạm vi X từ **−0,109 đến +78,713 mm**.
- Có dòng modal lược trục giữ nguyên và các từ lệnh viết liền. Bộ đọc kiểm chứng của ArtisanAxis hiện kiểm tra phương ngữ do ArtisanAxis xuất, nên tệp này dùng để đối chiếu vai trò trục, dấu quay và lệnh bù. Mẫu này không cung cấp chứng cứ rằng chương trình tinh cho STL Di Lặc 250 có thể chạy.

## Tích hợp vào ArtisanAxis

Hồ sơ nâng cao hỗ trợ `headPivotFromTip` dưới dạng vectơ đã đo từ mũi dao đến tâm đầu B tại tư thế B=0, theo mm. Trục X của hệ Cartesian cục bộ dọc trục A và Z hướng lên. Lõi dùng hiệu **R(B)v − v** để kiểm hành trình máy, và dùng cùng hiệu khi dựng lại NC ở chế độ bù trực tiếp. Chế độ RTCP vẫn gửi tọa độ mũi dao; vectơ được dùng trong kiểm tra tọa độ cơ khí. Hồi quy kiểm chứng ảnh hưởng bất đối xứng giữa B+20° và B−20°.

Các giá trị của tệp tham khảo không ghi đè mặc định hay bật xác nhận hiệu chuẩn. Ca so sánh phần mềm dùng tâm A đã lưu và dải Z đã lưu như giả định chẩn đoán. Hai dấu của vectơ lệch tâm được thử riêng chỉ để đo độ nhạy của mô hình. Việc xuất NC chạy máy cho Di Lặc vẫn phụ thuộc vào đối chiếu hệ tọa độ khi máy đang chạy, vectơ đã đo theo dao lắp, cấu hình phôi/đồ gá và kết quả kiểm chứng đường chạy.
