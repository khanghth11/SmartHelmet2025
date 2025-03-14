#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <HardwareSerial.h>
#include <math.h>
#include <string.h>
#include <Wire.h>
#include <MPU6050.h>

// GPIO Configuration
#define BUZZER_PIN    23       
#define PIEZO_PIN     34        
#define SIM_RX_PIN    16       
#define SIM_TX_PIN    17       
#define IR_SENSOR_PIN 36    

// BLE Configuration
int scanTime = 2; 
BLEScan *pBLEScan;
BLEAddress targetAddress("dc:47:5d:13:b7:41");
bool bleAlertActive = false;

// Impact Detection Variables
float emaMagnitude = 0;
float emaPiezo = 0;
const float alpha = 0.1;
int compositeThreshold;
bool impact_detected = false;
unsigned long impact_time;
const unsigned long alert_delay = 5000;

// IR Sensor Variables
int irThreshold = 2000; 
bool eyeClosed = false;
unsigned long eyeClosedTime = 0;
const unsigned long sleepThreshold = 3000;

// SIM 4G A7680C Configuration
HardwareSerial SIM7680(1);
const char number1[] = "0xxxxxx";
enum SimState { SIM_IDLE, SIM_CMGF, SIM_CMGS, SIM_SEND };
SimState simState = SIM_IDLE;
unsigned long simTimeout = 0;
int retryCount = 0;
char simResponse[256];       // Buffer cho phản hồi từ SIM
int simResponseIndex = 0;    // Vị trí index hiện tại

// MPU6050 Configuration
MPU6050 mpu;
float baselineX, baselineY, baselineZ;

// System Variables
unsigned long lastScanMillis = 0;
const long scanInterval = 5000;

// Hàm prototype
void calibrateSensors();
bool checkResponse(const char* target);
void updateSMSSending();
void updateBuzzer();
void readSIMResponse();
void checkEyeState();
void detectImpact();

// Hàm tính khoảng cách dựa trên RSSI
float calculateDistance(int rssi, int txPower, float n) {
  return pow(10, ((float)(txPower - rssi)) / (10 * n));
}

// BLE Callback
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    int rssi = advertisedDevice.getRSSI();
    float distance = calculateDistance(rssi, -59, 2.5); // Giả sử txPower = -59, n = 2.5
    Serial.print("Device ");
    Serial.print(advertisedDevice.getAddress().toString().c_str());
    Serial.print(" RSSI: ");
    Serial.print(rssi);
    Serial.print(" | Estimated Distance: ");
    Serial.print(distance);
    Serial.println(" m");

    // Nếu cần, vẫn có thể xử lý riêng cho targetAddress:
    if (advertisedDevice.getAddress().equals(targetAddress)) {
      bleAlertActive = (advertisedDevice.getRSSI() < -51);
    }
  }
};

void setup() {
  Serial.begin(115200);
  SIM7680.begin(115200, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
  
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PIEZO_PIN, INPUT);
  pinMode(IR_SENSOR_PIN, INPUT);
  
  // Khởi tạo MPU6050
  Wire.begin();
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 connection failed");
    while (1);
  }
  Serial.println("MPU6050 connection successful");

  // Khởi tạo BLE
  BLEDevice::init("HelmetSmart");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  
  calibrateSensors();
  Serial.println("System initialized.");
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Đọc phản hồi từ SIM
  readSIMResponse();
  
  // Quét BLE theo chu kỳ không blocking
  if (currentMillis - lastScanMillis >= scanInterval) {
    lastScanMillis = currentMillis;
    BLEScanResults* results = pBLEScan->start(scanTime, false);
    pBLEScan->clearResults();
  }
  
  detectImpact();
  checkEyeState();
  
  if (impact_detected) {
    updateSMSSending();
  }
  
  updateBuzzer();
}

// Hàm hiệu chỉnh cảm biến: tính baseline và composite threshold
void calibrateSensors() {
  const int samples = 50;
  float sumX = 0, sumY = 0, sumZ = 0;
  
  for (int i = 0; i < samples; i++) {
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    sumX += ax / 16384.0;
    sumY += ay / 16384.0;
    sumZ += az / 16384.0;
    delay(10);
  }
  
  baselineX = sumX / samples;
  baselineY = sumY / samples;
  baselineZ = sumZ / samples;
  
  // Tính ngưỡng động
  float sumMagnitude = 0;
  long sumPiezo = 0;
  for (int i = 0; i < samples; i++) {
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    float x = ax / 16384.0;
    float y = ay / 16384.0;
    float z = az / 16384.0;
    sumMagnitude += sqrt(sq(x - baselineX) + sq(y - baselineY) + sq(z - baselineZ));
    sumPiezo += analogRead(PIEZO_PIN);
    delay(10);
  }
  
  float avgMagnitude = sumMagnitude / samples;
  float avgPiezo = sumPiezo / samples;
  compositeThreshold = (avgMagnitude * 0.7 + avgPiezo * 0.3) * 2.5;
  Serial.print("Calibration done. Baselines - X: ");
  Serial.print(baselineX);
  Serial.print(" Y: ");
  Serial.print(baselineY);
  Serial.print(" Z: ");
  Serial.print(baselineZ);
  Serial.print(" | Composite Threshold: ");
  Serial.println(compositeThreshold);
}

// Phát hiện va chạm: đọc cảm biến gia tốc, piezo và tính composite value
void detectImpact() {
  static unsigned long lastCheck = 0;
  static unsigned long lastDebugPrint = 0;
  if (millis() - lastCheck < 10) return;
  lastCheck = millis();

  // Đọc dữ liệu gia tốc từ MPU6050
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float x = ax / 16384.0;
  float y = ay / 16384.0;
  float z = az / 16384.0;

  // Tính độ lớn của vector gia tốc
  float magnitude = sqrt(sq(x - baselineX) + sq(y - baselineY) + sq(z - baselineZ));

  // Đọc giá trị từ cảm biến piezo
  int piezoValue = analogRead(PIEZO_PIN);

  // Lọc EMA
  emaMagnitude = alpha * magnitude + (1 - alpha) * emaMagnitude;
  emaPiezo = alpha * piezoValue + (1 - alpha) * emaPiezo;

  float compositeValue = 0.7 * emaMagnitude + 0.3 * emaPiezo;

  // In ra các giá trị cảm biến mỗi 1 giây
  if (millis() - lastDebugPrint >= 300) {
    lastDebugPrint = millis();
    Serial.print("Accel: x=");
    Serial.print(x);
    Serial.print(" y=");
    Serial.print(y);
    Serial.print(" z=");
    Serial.print(z);
    Serial.print(" | Mag=");
    Serial.print(magnitude);
    Serial.print(" | Piezo=");
    Serial.print(piezoValue);
    Serial.print(" | Composite=");
    Serial.print(compositeValue);
    Serial.print(" | Thresh=");
    Serial.println(compositeThreshold);
  }

  if (compositeValue >= compositeThreshold && !impact_detected) {
    impact_detected = true;
    impact_time = millis();
    simState = SIM_IDLE;
    retryCount = 0;
    Serial.println("Impact detected!");
  }
}

// Kiểm tra trạng thái cảm biến IR để xác định mắt đóng quá lâu
void checkEyeState() {
  static unsigned long lastIRCheck = 0;
  if (millis() - lastIRCheck < 100) return;
  lastIRCheck = millis();
  
  int irValue = analogRead(IR_SENSOR_PIN);
  Serial.print("IR sensor: ");
  Serial.println(irValue);
  
  // Nếu giá trị IR vượt ngưỡng, coi như mắt đang đóng
  if (irValue > irThreshold) {
    if (eyeClosedTime == 0) {
      eyeClosedTime = millis();
      Serial.println("Eye closed detected. Starting timer...");
    }
  } else {
    if (eyeClosedTime != 0) {
      Serial.println("Eye opened. Resetting timer.");
    }
    eyeClosedTime = 0;
  }
  
  // Xác định nếu mắt đã đóng quá thời gian quy định
  if (eyeClosedTime != 0 && (millis() - eyeClosedTime >= sleepThreshold)) {
    if (!eyeClosed) {
      Serial.println("Eye has been closed for too long. Triggering alert.");
    }
    eyeClosed = true;
  } else {
    eyeClosed = false;
  }
}

// Đọc phản hồi từ module SIM sử dụng buffer tĩnh
void readSIMResponse() {
  while (SIM7680.available() && simResponseIndex < 255) {
    char c = SIM7680.read();
    // Xử lý kết thúc dòng
    if (c == '\r' || c == '\n') {
      if (simResponseIndex > 0) { // Nếu có dữ liệu
        simResponse[simResponseIndex] = '\0'; // Kết thúc chuỗi
        Serial.print("SIM Response: ");
        Serial.println(simResponse);
        simResponseIndex = 0; // Reset buffer sau khi xử lý
      }
    } else {
      simResponse[simResponseIndex++] = c;
    }
  }
}

// Kiểm tra phản hồi từ SIM có chứa chuỗi target không
bool checkResponse(const char* target) {
  if (strstr(simResponse, target) != NULL) {
    // Reset buffer sau khi tìm thấy phản hồi
    simResponseIndex = 0;
    simResponse[0] = '\0';
    return true;
  }
  return false;
}

// Cập nhật quá trình gửi SMS theo state machine
void updateSMSSending() {
  switch(simState) {
    case SIM_IDLE:
      Serial.println("SIM_IDLE: Setting SMS format.");
      SIM7680.println("AT+CMGF=1");
      simState = SIM_CMGF;
      simTimeout = millis();
      break;
      
    case SIM_CMGF:
      if (checkResponse("OK")) {
        Serial.println("SIM_CMGF: Received OK, preparing SMS.");
        SIM7680.print("AT+CMGS=\"");
        SIM7680.print(number1);
        SIM7680.println("\"");
        simState = SIM_CMGS;
        simTimeout = millis();
      } else if (millis() - simTimeout > 1000) {
        if (++retryCount >= 3) {
          impact_detected = false;
          Serial.println("Error: Failed to set SMS format");
        } else {
          simState = SIM_IDLE;
          Serial.println("Retrying SIM configuration...");
        }
      }
      break;
      
    case SIM_CMGS:
      if (checkResponse(">")) {
        Serial.println("SIM_CMGS: Received '>', sending SMS message.");
        SIM7680.print("NGUOI BI VA CHAM KHI THAM GIAO THONG");
        SIM7680.write(26);
        simState = SIM_SEND;
        simTimeout = millis();
      } else if (millis() - simTimeout > 1000) {
        if (++retryCount >= 3) {
          impact_detected = false;
          Serial.println("Error: Failed to prepare SMS");
        } else {
          simState = SIM_IDLE;
          Serial.println("Retrying SMS sending...");
        }
      }
      break;
      
    case SIM_SEND:
      if (checkResponse("+CMGS:")) {
        Serial.println("SIM_SEND: SMS sent successfully.");
        impact_detected = false;
        simState = SIM_IDLE;
      } else if (millis() - simTimeout > 5000) {
        if (++retryCount >= 3) {
          impact_detected = false;
          Serial.println("Error: Failed to send SMS");
        } else {
          simState = SIM_IDLE;
          Serial.println("Retrying SMS sending...");
        }
      }
      break;
  }
}

// Cập nhật trạng thái buzzer dựa trên mức độ cảnh báo ưu tiên: Impact > BLE > Eye
void updateBuzzer() {
  static bool prevBuzz = false;
  bool shouldBuzz = false;
  
  if (impact_detected) {
    shouldBuzz = true;
  } else if (bleAlertActive) {
    shouldBuzz = true;
  } else if (eyeClosed) {
    shouldBuzz = true;
  }
  
  if (shouldBuzz != prevBuzz) {
    Serial.print("Buzzer state changed: ");
    Serial.println(shouldBuzz ? "ON" : "OFF");
    prevBuzz = shouldBuzz;
  }
  
  digitalWrite(BUZZER_PIN, shouldBuzz ? HIGH : LOW);
}
