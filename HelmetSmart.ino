#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <HardwareSerial.h>

// Cấu hình chân GPIO
#define BUZZER_PIN 23       // Chân kết nối buzzer
#define PIEZO_PIN 34        // Chân kết nối cảm biến rung piezoelectric
#define xPin 35             // Chân kết nối cảm biến gia tốc (trục X)
#define yPin 32             // Chân kết nối cảm biến gia tốc (trục Y)
#define zPin 33             // Chân kết nối cảm biến gia tốc (trục Z)
#define SIM_RX_PIN 16       // Chân RX của ESP32 kết nối với TX của module A7680C
#define SIM_TX_PIN 17       // Chân TX của ESP32 kết nối với RX của module A7680C

// Cấu hình BLE
int scanTime = 5;           // Thời gian quét BLE (giây)
BLEScan *pBLEScan;
BLEAddress targetAddress("19:ce:a9:93:71:4e");  // Địa chỉ BLE mục tiêu

// Cấu hình cảm biến va chạm
int xaxis = 0, yaxis = 0, zaxis = 0;
int deltx = 0, delty = 0, deltz = 0;
int vibration = 2, devibrate = 10;
int magnitude = 0;
int sensitivity = 150;      // Ngưỡng phát hiện va chạm
boolean impact_detected = false;
unsigned long time1;
unsigned long impact_time;
unsigned long alert_delay = 5000;  // Thời gian gửi cảnh báo sau va chạm

// Cấu hình cảm biến rung piezoelectric
int piezoThreshold = 500;   // Ngưỡng phát hiện rung

// Cấu hình module SIM 4G A7680C
HardwareSerial SIM7680(1);  // Sử dụng UART1 của ESP32
String number1 = "0912595637";  // Số điện thoại nhận cảnh báo

// Callback cho quét BLE
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // In thông tin về thiết bị BLE
    Serial.printf("Found BLE device: %s\n", advertisedDevice.toString().c_str());

    // Lấy UUID của thiết bị
    if (advertisedDevice.haveServiceUUID()) {
      BLEUUID uuid = advertisedDevice.getServiceUUID();
      Serial.printf("Service UUID: %s\n", uuid.toString().c_str());
    }

    // Kiểm tra thiết bị mục tiêu
    if (advertisedDevice.getAddress().equals(targetAddress)) {
      int rssi = advertisedDevice.getRSSI();
      Serial.printf("Found target device: %s, RSSI: %d\n", 
                    advertisedDevice.getAddress().toString().c_str(), 
                    rssi);

      // Kiểm tra RSSI và kích hoạt buzzer
      if (rssi < -51) {
        digitalWrite(BUZZER_PIN, HIGH);  // Bật buzzer
        Serial.println("Buzzer ON");
      } else {
        digitalWrite(BUZZER_PIN, LOW);   // Tắt buzzer
        Serial.println("Buzzer OFF");
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  SIM7680.begin(115200, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);  // Khởi tạo UART1 cho module A7680C

  // Cấu hình chân GPIO
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PIEZO_PIN, INPUT);
  pinMode(xPin, INPUT);
  pinMode(yPin, INPUT);
  pinMode(zPin, INPUT);

  // Khởi tạo BLE
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  // Khởi tạo cảm biến va chạm
  time1 = micros();
  xaxis = analogRead(xPin);
  yaxis = analogRead(yPin);
  zaxis = analogRead(zPin);

  Serial.println("System initialized.");
}

void loop() {
  // Quét BLE
  BLEScanResults *foundDevices = pBLEScan->start(scanTime, false);
  Serial.println("BLE scan done!");
  pBLEScan->clearResults();

  // Đọc giá trị cảm biến rung piezoelectric
  int piezoValue = analogRead(PIEZO_PIN);
  if (piezoValue > piezoThreshold) {
    Serial.println("Piezo vibration detected!");
    digitalWrite(BUZZER_PIN, HIGH);
    delay(1000);
    digitalWrite(BUZZER_PIN, LOW);
  }

  // Phát hiện va chạm bằng cảm biến gia tốc
  if (micros() - time1 > 1999) Va_cham();

  if (impact_detected) {
    if (millis() - impact_time >= alert_delay) {
      digitalWrite(BUZZER_PIN, LOW);
      sendSMS("NGUOI BI VA CHAM KHI THAM GIA GIAO THONG");
      impact_detected = false;
      impact_time = 0;
    }
  }

  delay(2000);  // Đợi 2 giây trước khi quét lại
}

// Hàm phát hiện va chạm bằng cảm biến gia tốc
void Va_cham() {
  time1 = micros();
  int oldx = xaxis, oldy = yaxis, oldz = zaxis;

  xaxis = analogRead(xPin);
  yaxis = analogRead(yPin);
  zaxis = analogRead(zPin);

  vibration--;
  if (vibration < 0) vibration = 0;

  if (vibration > 0) return;

  deltx = xaxis - oldx;
  delty = yaxis - oldy;
  deltz = zaxis - oldz;

  magnitude = sqrt(sq(deltx) + sq(delty) + sq(deltz));

  if (magnitude >= sensitivity) {
    impact_detected = true;
    impact_time = millis();
    digitalWrite(BUZZER_PIN, HIGH);
    vibration = devibrate;
  } else {
    magnitude = 0;
  }
}

// Hàm gửi SMS
void sendSMS(String message) {
  SIM7680.println("AT+CMGF=1");  // Chế độ text mode
  delay(500);

  SIM7680.println("AT+CMGS=\"" + number1 + "\"");  // Số điện thoại nhận SMS
  delay(500);

  SIM7680.print(message);  // Nội dung SMS
  delay(500);

  SIM7680.write(26);  // Gửi ký tự kết thúc (Ctrl+Z)
  delay(500);

  Serial.println("SMS sent: " + message);
}
