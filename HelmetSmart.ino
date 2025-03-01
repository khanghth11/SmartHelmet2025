#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// Cấu hình chân GPIO cho buzzer
#define BUZZER_PIN 23

int scanTime = 5;  // Thời gian quét (giây)
BLEScan *pBLEScan;

// Địa chỉ BLE của thiết bị bạn muốn tìm
BLEAddress targetAddress("19:ce:a9:93:71:4e");  // Thay thế bằng địa chỉ BLE thực tế

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
  Serial.println("Scanning for target BLE device...");

  // Cấu hình chân buzzer là OUTPUT
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);  // Đảm bảo buzzer tắt lúc khởi động

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();  // Tạo một lần quét mới
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);  // Quét chủ động (nhanh hơn nhưng tốn năng lượng hơn)
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // Giá trị nhỏ hơn hoặc bằng setInterval
}

void loop() {
  // Bắt đầu quét và kiểm tra kết quả
  BLEScanResults *foundDevices = pBLEScan->start(scanTime, false);
  Serial.println("Scan done!");
  pBLEScan->clearResults();  // Xóa kết quả để giải phóng bộ nhớ
  delay(2000);  // Đợi 2 giây trước khi quét lại
}