Đề tài đã có tiến độ như sau:
Về chức năng chống trộm : 
+ Hoàn Thành: Đã thành công quét và lấy được các địa chỉ và tính toán khoảng cách từ nguồn phát đến hệ thống cách bao nhiêu mét dựa vào RSSI.
*Code sử dụng thư viện BLE của ESP32 để quét các BLE. Trong callback, in ra địa chỉ và RSSI của từng thiết bị.
*Sau đó tính khoảng cách dựa vào công thức từ iotbymukund, 2016/10/07, How to Calculate Distance from the RSSI value of the BLE Beacon; distance = calculateDistance(rssi, -59, 2.5); // Giả sử txPower (hiệu chuẩn ở 1m) = -59, suy hao môi trường n = 2.5
*Nếu thiết bị quét được khớp với targetAddress, hệ thống kiểm tra mức tín hiệu để kích hoạt trạng thái cảnh báo BLE.
+ Chưa hoàn thành: Hiệu chỉnh sai số thực tế (TxPower và hệ số suy hao n cần được đo đạc, hiệu chuẩn trong môi trường cụ thể).
**Đánh giá tiến độ: 80/100%

**************************************************
Về phát hiện buồn ngủ : 
+ Hoàn Thành: Sử dụng cảm biến vật cản hồng ngoại (IR sensor) để xác định trạng thái “buồn ngủ” của người dùng khi tròng mắt bị che khuất (hoặc đóng quá lâu).
*Code đọc giá trị analog từ cảm biến hồng ngoại (chân IR_SENSOR_PIN).
*Nếu giá trị IR vượt ngưỡng (irThreshold = 2000) – mặc dù giá trị đo được hiện tại trong code nhỏ (do hiệu chuẩn của cảm biến) – thì hệ thống cho rằng “mắt đang đóng”.
*Khi cảm biến phát hiện giá trị cao, thời gian được ghi nhận (eyeClosedTime). Nếu mắt được che khuất liên tục vượt quá sleepThreshold (3000 ms), trạng thái "eyeClosed" được kích hoạt và kích hoạt cảnh báo (buzzer).
+ Chưa Hoàn Thành: Chưa hiệu chỉnh sai số thực tế, có thể phải áp dụng các thuật toán hoặc kết hợp sensor gia tốc nhằm đảm bảo hệ thống phát hiện buồn ngủ hợp lí hơn.
Đánh giá tiến độ: 60/100%

**************************************************
Về tính năng phát hiện va chạm và kêu gọi trợ giúp:
+ Hoàn Thành:
*Tích hợp sensor gia tốc + piezo, có thuật toán lọc EMA và ngưỡng động.
*Tự động gửi SMS qua 4G khi phát hiện va chạm.
*Đọc giá trị từ accelerometer qua các chân xPin, yPin, zPin (các giá trị raw).
*Tính độ lớn (magnitude) của vector gia tốc bằng công thức: sumMagnitude += sqrt(sq(x - baselineX) + sq(y - baselineY) + sq(z - baselineZ));
*Đọc giá trị từ cảm biến piezo (vibration) qua chân PIEZO_PIN.
*Tính giá trị Composite theo tỉ lệ 0.7 (gia tốc) và 0.3 (piezo).{compositeThreshold = (avgMagnitude * 0.7 + avgPiezo * 0.3) * 2.5;}
*Khi va chạm được phát hiện (impact_detected = true), state machine của module SIM 4G A7680C được kích hoạt.
*Các trạng thái bao gồm SIM_IDLE, SIM_CMGF, SIM_CMGS, SIM_SEND, xử lý các lệnh AT để gửi SMS thông báo “NGUOI BI VA CHAM KHI THAM GIAO THONG”.
+ Chưa Hoàn Thành:
*Giao tiếp và hiệu chỉnh gửi SMS qua module SIM (thời gian timeout, số lần retry, phản hồi từ SIM) và cần hiệu chỉnh thêm các giá trị gia tốc, bộ lọc EMA trong điều kiện thực tế.
Đánh giá tiến độ: 60/100%

**************************************************
Dự kiến đã hoàn thành 60% tiến trình project vào 01/03/2025

https://iotandelectronics.wordpress.com/2016/10/07/how-to-calculate-distance-from-the-rssi-value-of-the-ble-beacon/
**************************************************
Sơ đồ Kết nối phần cứng:
Module SIM 4G A7680C:
VCC: 3.7V – 4.2V.
GND: Nối chung với GND của ESP32.
TX: Kết nối với GPIO 16 (đầu RX của ESP32).
RX: Kết nối với GPIO 17 (đầu TX của ESP32).
****************************************
Cảm biến rung Piezoelectric:
Chân tín hiệu: Kết nối với GPIO 34 (PIEZO_PIN).
GND: Nối với GND của ESP32.
****************************************
Cảm biến gia tốc:
Chân X: Kết nối với GPIO 35 (xPin).
Chân Y: Kết nối với GPIO 32 (yPin).
Chân Z: Kết nối với GPIO 33 (zPin).
****************************************
Buzzer:
Chân điều khiển: Kết nối với GPIO 23 (BUZZER_PIN).
****************************************
Cảm biến hồng ngoại (IR):
Chân tín hiệu: Kết nối với GPIO 36 (IR_SENSOR_PIN).
****************************************
Các chân VCC, 3v3, GND... sẽ được cấp nguồn từ mạch nguồn breadboard.
