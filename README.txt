Đề tài đã hoàn thành: Về Cảnh Báo Tai Nạn : Gửi sms (ModuleSim4G) cho số điện thoại khi phát hiện thấy tai nạn thông qua cảm biến rung và cảm biến gia tốc.
Về chống trộm : Đã hoàn thành quét BLE lấy thông tin RSSI từ module BLE.
Về phát hiện buồn ngủ : Chưa hoàn thành
Dự kiến đã hoàn thành 60% tiến trình project vào 01/03/2025
Accel: x=144 y=866 z=551
Đây là các giá trị thô (raw readings) từ cảm biến gia tốc (accelerometer) của các trục X, Y, Z.

Mag=523.94
Đây là giá trị độ lớn (magnitude) của vector gia tốc được tính toán từ hiệu số so với baseline của mỗi trục ( dùng công thức sqrt((x-baselineX)² + (y-baselineY)² + (z-baselineZ)²)).

Piezo=0
Giá trị đọc từ cảm biến piezo (dùng để phát hiện va chạm) ở thời điểm đó là 0.

Composite=277.82
Đây là giá trị composite được tính bằng cách kết hợp giá trị lọc EMA của accelerometer (magnitude) và cảm biến piezo theo tỉ lệ 0.7 và 0.3. Giá trị này được so sánh với ngưỡng để xác định có va chạm hay không.

Thresh=574
Đây là ngưỡng composite (compositeThreshold) đã được hiệu chuẩn từ dữ liệu ban đầu. Nếu giá trị composite vượt qua ngưỡng này, hệ thống sẽ xem đó là một va chạm.



Kết nối phần cứng:
Module SIM 4G A7680C:

VCC: 3.7V-4.2V.

GND: GND.

TX: GPIO 16 (RX của ESP32).

RX: GPIO 17 (TX của ESP32).

Cảm biến rung piezoelectric:

Kết nối chân tín hiệu với GPIO 34 (PIEZO_PIN).

Kết nối GND với GND của ESP32.

Cảm biến gia tốc:

Kết nối các chân analog (xPin, yPin, zPin) với cảm biến gia tốc.

Buzzer:

Kết nối chân BUZZER_PIN (GPIO 23) với buzzer.
