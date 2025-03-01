#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <SoftwareSerial.h>

// Cấu hình chân GPIO cho buzzer
#define BUZZER_PIN 23

// Cấu hình chân GPIO cho cảm biến va chạm
#define xPin 34
#define yPin 35
#define zPin 32

// Cấu hình chân GPIO cho cảm biến cồn
#define CB_Con 33

// Cấu hình chân GPIO cho module SIM 4G A7680C
#define SIM_TX_PIN 16
#define SIM_RX_PIN 17

int scanTime = 5;  // Thời gian quét (giây)
BLEScan *pBLEScan;

// Địa chỉ BLE của thiết bị bạn muốn tìm
BLEAddress targetAddress("19:ce:a9:93:71:4e");  // Thay thế bằng địa chỉ BLE thực tế

// Khai báo biến cho cảm biến va chạm
int xaxis = 0, yaxis = 0, zaxis = 0;
int deltx = 0, delty = 0, deltz = 0;
int vibration = 2, devibrate = 10;
int magnitude = 0;
int sensitivity = 150;  // Cài đặt ngưỡng phát hiện cảnh báo va chạm

boolean impact_detected = false;
unsigned long time1;
unsigned long impact_time;
unsigned long alert_delay = 5000;  // Thời gian gửi báo va chạm

// Khai báo biến cho cảm biến cồn
float GT_cbcon;
float GT_nongdocon;

// Khai báo biến cho module SIM 4G A7680C
SoftwareSerial SIM800L(SIM_TX_PIN, SIM_RX_PIN);
String number1 = "0912595637";  // Số điện thoại nhận cảnh báo

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Kiểm tra xem thiết bị quét được có địa chỉ trùng với địa chỉ mục tiêu không
    if (advertisedDevice.getAddress().equals(targetAddress)) {
      int rssi = advertisedDevice.getRSSI();
      Serial.printf("Found target device: %s, RSSI: %d\n", 
                    advertisedDevice.getAddress().toString().c_str(), 
                    rssi);

      // Kiểm tra RSSI và kích hoạt buzzer nếu RSSI < -48
      if (rssi < -51) {
        digitalWrite(BUZZER_PIN, HIGH);  // Bật buzzer
        Serial.println("Buzzer ON");
      } else {
        digitalWrite(BUZZER_PIN, LOW);  // Tắt buzzer
        Serial.println("Buzzer OFF");
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  SIM800L.begin(9600);
  Serial.println("Scanning for target BLE device...");

  // Cấu hình chân buzzer là OUTPUT
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);  // Đảm bảo buzzer tắt lúc khởi động

  // Cấu hình chân cảm biến va chạm
  pinMode(xPin, INPUT);
  pinMode(yPin, INPUT);
  pinMode(zPin, INPUT);

  // Cấu hình chân cảm biến cồn
  pinMode(CB_Con, INPUT);

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();  // Tạo một lần quét mới
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);  // Quét chủ động (nhanh hơn nhưng tốn năng lượng hơn)
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // Giá trị nhỏ hơn hoặc bằng setInterval

  time1 = micros();
  xaxis = analogRead(xPin);
  yaxis = analogRead(yPin);
  zaxis = analogRead(zPin);
}

void loop() {
  // Bắt đầu quét và kiểm tra kết quả
  BLEScanResults *foundDevices = pBLEScan->start(scanTime, false);
  Serial.println("Scan done!");
  pBLEScan->clearResults();  // Xóa kết quả để giải phóng bộ nhớ

  // Đọc giá trị cảm biến cồn
  GT_cbcon = analogRead(CB_Con);
  GT_nongdocon = (((GT_cbcon / 1000) - 0.04));

  // Đọc cảm biến va chạm liên tục sau mỗi 2mS
  if (micros() - time1 > 1999) Va_cham();

  if (impact_detected) {
    if (millis() - impact_time >= alert_delay) {
      digitalWrite(BUZZER_PIN, LOW);
      call();
      delay(1000);
      message1();
      impact_detected = false;
      impact_time = 0;
    }
  }

  delay(2000);  // Đợi 2 giây trước khi quét lại
}

void Va_cham() {
  time1 = micros();
  int oldx = xaxis;
  int oldy = yaxis;
  int oldz = zaxis;

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

void call() {
  SIM800L.print(F("ATD"));
  SIM800L.print(number1);
  SIM800L.print(F(";\r\n"));
  delay(15000);
  SIM800L.println("ATH");
}

void message1() {
  SIM800L.println("AT+CMGF=1");
  delay(1000);
  SIM800L.println("AT+CMGS=\"" + number1 + "\"\r");
  delay(1000);
  String SMS = "NGUOI BI VA CHAM KHI THAM GIA GIAO THONG";
  SIM800L.println(SMS);
  delay(100);
  SIM800L.println((char)26);
  delay(1000);
}
