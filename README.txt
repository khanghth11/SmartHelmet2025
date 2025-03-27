Đề tài đã có tiến độ như sau:
Về chức năng chống trộm : 
+ Hoàn Thành: Đã thành công quét và lấy được các địa chỉ và tính toán khoảng cách từ nguồn phát đến hệ thống cách bao nhiêu mét dựa vào RSSI.
*Code sử dụng thư viện BLE của ESP32 để quét các BLE. Trong callback, in ra địa chỉ và RSSI của từng thiết bị.
*Sau đó tính khoảng cách dựa vào công thức từ iotbymukund, 2016/10/07, How to Calculate Distance from the RSSI value of the BLE Beacon; distance = calculateDistance(rssi, -59, 2.5); // Giả sử txPower (hiệu chuẩn ở 1m) = -59, suy hao môi trường n = 2.5
*Nếu thiết bị quét được khớp với targetAddress, hệ thống kiểm tra mức tín hiệu để kích hoạt trạng thái cảnh báo BLE.
**Đánh giá tiến độ: 100/100%

**************************************************
Về phát hiện buồn ngủ : 
+ Hoàn Thành: Sử dụng cảm biến vật cản hồng ngoại (IR sensor) để xác định trạng thái “buồn ngủ” của người dùng khi tròng mắt bị che khuất (hoặc đóng quá lâu).
*Code đọc giá trị analog từ cảm biến hồng ngoại (chân IR_SENSOR_PIN).
*Nếu giá trị IR vượt ngưỡng (irThreshold = 2000) – mặc dù giá trị đo được hiện tại trong code nhỏ (do hiệu chuẩn của cảm biến) – thì hệ thống cho rằng “mắt đang đóng”.
*Khi cảm biến phát hiện giá trị cao, thời gian được ghi nhận (eyeClosedTime). Nếu mắt được che khuất liên tục vượt quá sleepThreshold (3000 ms), trạng thái "eyeClosed" được kích hoạt và kích hoạt cảnh báo (buzzer).
+ Chưa Hoàn Thành: Chưa hiệu chỉnh sai số thực tế, có thể phải áp dụng các thuật toán hoặc kết hợp sensor gia tốc nhằm đảm bảo hệ thống phát hiện buồn ngủ hợp lí hơn.
Đánh giá tiến độ: 80/100%

**************************************************
Về tính năng phát hiện va chạm và kêu gọi trợ giúp:
+ Hoàn Thành:
*Tích hợp sensor gia tốc + piezo.
*Tự động gửi SMS qua 4G khi phát hiện va chạm.
*Đọc giá trị từ accelerometer qua các chân xPin, yPin, zPin (các giá trị raw).
*Đọc giá trị từ cảm biến piezo (vibration) qua chân PIEZO_PIN.
*Gửi sms khi xử lí giá trị MPU6050 vượt quá một ngưỡng xác định,nếu vượt qua ngưỡng MPU6050 tiếp đó xét tiếp đến giá trị maxVibration được khởi tạo. Biến này sẽ lưu giá trị vibration cao nhất trong vòng 5 giây, sau 5 giây lại reset về 0. Nếu Maxvibration cũng vượt một ngưỡng xác định thì gửi sms.
*Khi va chạm được phát hiện (impact_detected = true), state machine của module SIM 4G A7680C được kích hoạt.
*Các trạng thái bao gồm SIM_IDLE, SIM_CMGF, SIM_CMGS, SIM_SEND, xử lý các lệnh AT để gửi SMS thông báo “NGUOI BI VA CHAM KHI THAM GIAO THONG”.
+ Chưa Hoàn Thành: Hiệu chỉnh thông số thực tế sau khi gắn lên nón bảo hiểm.
Đánh giá tiến độ: 90/100%

Cần thêm một nút nhấn vào hệ thống để thực hiện ba chức năng:
*Tắt/Bật chế độ chống trộm: Ấn giữ 5 giây để tắt, buzzer kêu 1 tiếng; ấn giữ 5 giây để bật lại, buzzer kêu 2 tiếng khối block chống trộm.
*Tắt buzzer khi buzzer đang kích hoạt: Ấn giữ 3 giây khi buzzer đang kêu để tắt buzzer.

**************************************************
Dự kiến đã hoàn thành 85% tiến trình project vào 20/03/2025

https://iotandelectronics.wordpress.com/2016/10/07/how-to-calculate-distance-from-the-rssi-value-of-the-ble-beacon/
**************************************************
Sơ đồ Kết nối phần cứng:
*ESP32:
GND –TERMINAL GND
Vin - TERMINAL VCC

*MPU6050:
SCL – I2C SCL GPIO 22
SDA – I2C SDA GPIO 21
VCC – TERMINAL VCC
GND – TERMINAL GND

*MODULE SIM A7680C:
TX – RX2 UART GPIO16
RX – TX2 UART GPIO17
VCC – TERMINAL VCC
GND – TERMINAL GND

*MODULE Cảm Biến Rung
Analog Output - GPIO 34
VCC – TERMINAL VCC
GND – TERMINAL GND

*MODULE IR Dò Line
Analog Output - GPIO 36
VCC – TERMINAL VCC
GND – TERMINAL GND

*Buzzer
GND – TERMINAL GND
Chân + - GPIO 23

*Button
GND – TERMINAL GND
Chân còn lại - GPIO 25

