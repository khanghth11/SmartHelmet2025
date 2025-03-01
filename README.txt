Đề tài đã hoàn thành: Về Cảnh Báo Tai Nạn : Gửi sms (ModuleSim4G) cho số điện thoại khi phát hiện thấy tai nạn thông qua cảm biến rung và cảm biến gia tốc.
Về chống trộm : Đã hoàn thành quét BLE lấy thông tin RSSI từ module BLE.
Về phát hiện buồn ngủ : Chưa hoàn thành
Dự kiến đã hoàn thành 60% tiến trình project vào 01/03/2025




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
