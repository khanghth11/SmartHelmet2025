#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <HardwareSerial.h>
#include <math.h>
#include <string.h>
#include <Wire.h>
#include <MPU6050.h>

#define BUZZER_PIN    23       
#define PIEZO_PIN     34        
#define SIM_RX_PIN    16       
#define SIM_TX_PIN    17       
#define IR_SENSOR_PIN 36    
#define BUTTON_PIN 25  // Chân kết nối nút nhấn
bool antiTheftEnabled = true;  // Trạng thái chế độ chống trộm
unsigned long buttonPressTime = 0;  // Thời gian nhấn nút
bool buttonActive = false;  // Trạng thái nút nhấn
unsigned long buzzerStartTime = 0;  // Thời điểm buzzer bắt đầu kêu
const unsigned long buzzerDuration = 5 * 60 * 1000;  // 5 phút (tính bằng mili giây)
// BLE Configuration
int scanTime = 2; 
BLEScan *pBLEScan;
BLEAddress targetAddress("dc:47:5d:13:b7:41");
bool bleAlertActive = false;

// Impact Detection Variables
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
HardwareSerial SIM7680(2);
const char number1[] = "0933845687";
enum SimState { SIM_IDLE, SIM_CMGF, SIM_CMGS, SIM_SEND };
SimState simState = SIM_IDLE;
unsigned long simTimeout = 0;
int retryCount = 0;
char simResponse[256];
int simResponseIndex = 0;

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

float calculateDistance(int rssi, int txPower, float n) {
  return pow(10, ((float)(txPower - rssi)) / (10 * n));
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    int rssi = advertisedDevice.getRSSI();
    float distance = calculateDistance(rssi, -59, 2.5);
    Serial.print("Device ");
    Serial.print(advertisedDevice.getAddress().toString().c_str());
    Serial.print(" RSSI: ");
    Serial.print(rssi);
    Serial.print(" | Distance: ");
    Serial.print(distance);
    Serial.println(" m");

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
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Sử dụng điện trở kéo lên
  
  Wire.begin();
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 connection failed");
    while (1);
  }
  
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
  readSIMResponse();
  if (currentMillis - lastScanMillis >= scanInterval && antiTheftEnabled) {
    lastScanMillis = currentMillis;
    BLEScanResults* results = pBLEScan->start(scanTime, false);
    pBLEScan->clearResults();
  }
  detectImpact();
  checkEyeState();
  if (impact_detected && antiTheftEnabled) {
    updateSMSSending();
  }
  updateBuzzer();
  handleButton();
}

void calibrateSensors() {
  const int samples = 100;
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
  compositeThreshold = (avgMagnitude * 0.7 + avgPiezo * 0.3) * 4.0;
  if (compositeThreshold < 0.5) {
    compositeThreshold = 0.5;  // Đảm bảo ngưỡng không quá thấp
}
  Serial.print("Calibration done. Threshold: ");
  Serial.println(compositeThreshold);
}

void handleButton() {
  static bool buzzerState = false;
  static unsigned long lastBuzzerTime = 0;
  
  if (digitalRead(BUTTON_PIN) == LOW) {  // Nút được nhấn
    if (!buttonActive) {
      buttonActive = true;
      buttonPressTime = millis();
    }
    
    if (millis() - buttonPressTime >= 5000) {  // Nhấn giữ 5 giây
      antiTheftEnabled = !antiTheftEnabled;
      if (antiTheftEnabled) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(100);
        digitalWrite(BUZZER_PIN, LOW);
        delay(100);
        digitalWrite(BUZZER_PIN, HIGH);
        delay(100);
        digitalWrite(BUZZER_PIN, LOW);
      } else {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(100);
        digitalWrite(BUZZER_PIN, LOW);
      }
      buttonActive = false;
    } else if (millis() - buttonPressTime >= 3000 && (impact_detected || bleAlertActive || eyeClosed)) {  // Nhấn giữ 3 giây khi buzzer đang kêu
      digitalWrite(BUZZER_PIN, LOW);
      impact_detected = false;
      bleAlertActive = false;
      eyeClosed = false;
      buttonActive = false;
    }
  } else {
    buttonActive = false;
  }
}

#define MAX_MAGNITUDE 2.5  // Ngưỡng gia tốc (tùy chỉnh theo thực tế)
#define MAX_VIBRATION 800  // Ngưỡng cảm biến rung

void detectImpact() {
    static unsigned long lastCheck = 0;
    static int maxVibration = 0;
    static unsigned long lastResetTime = 0;

    if (millis() - lastCheck < 200) return;
    lastCheck = millis();

    // Đọc giá trị gia tốc từ MPU6050
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    float x = ax / 16384.0;
    float y = ay / 16384.0;
    float z = az / 16384.0;

    float magnitude = sqrt(sq(x - baselineX) + sq(y - baselineY) + sq(z - baselineZ));
    
    // Đọc giá trị từ cảm biến rung
    int piezoValue = analogRead(PIEZO_PIN);
    
    // Cập nhật giá trị lớn nhất trong khoảng thời gian 10 giây
    if (piezoValue > maxVibration) {
        maxVibration = piezoValue;
    }

    // Nếu vượt ngưỡng gia tốc, kiểm tra tiếp cảm biến rung
    if (magnitude > MAX_MAGNITUDE) {
        if (maxVibration > MAX_VIBRATION) {
            impact_detected = true;
            impact_time = millis();
            Serial.println("Impact detected!");
        }
    }

    // Reset giá trị cảm biến rung mỗi 5 giây
    if (millis() - lastResetTime > 5000) {
        maxVibration = 0;
        lastResetTime = millis();
    }

    // Reset lại trạng thái va chạm sau 5 giây
    if (impact_detected && millis() - impact_time > 5000) {
        impact_detected = false;
    }
}

void checkEyeState() {
  int irValue = analogRead(IR_SENSOR_PIN);
  
  if (irValue > irThreshold) {
    if (eyeClosedTime == 0) {
      eyeClosedTime = millis();
    }
  } else {
    eyeClosedTime = 0;
  }
  
  if (eyeClosedTime != 0 && (millis() - eyeClosedTime >= sleepThreshold)) {
    eyeClosed = true;
  } else {
    eyeClosed = false;
  }
}

// Đọc phản hồi từ SIM
void readSIMResponse() {
  while (SIM7680.available() && simResponseIndex < 255) {
    char c = SIM7680.read();
    if (c == '\r' || c == '\n') {
      if (simResponseIndex > 0) {
        simResponse[simResponseIndex] = '\0';
        Serial.print("SIM Response: ");
        Serial.println(simResponse);
        simResponseIndex = 0;
      }
    } else {
      simResponse[simResponseIndex++] = c;
    }
  }
}

bool checkResponse(const char* target) {
  return strstr(simResponse, target) != NULL;
}

// Gửi SMS khi có va chạm
void updateSMSSending() {
  switch(simState) {
    case SIM_IDLE:
      SIM7680.println("AT+CMGF=1");
      simState = SIM_CMGF;
      break;
      
    case SIM_CMGF:
      if (checkResponse("OK")) {
        SIM7680.print("AT+CMGS=\"");
        SIM7680.print(number1);
        SIM7680.println("\"");
        simState = SIM_CMGS;
      }
      break;
      
    case SIM_CMGS:
      if (checkResponse(">")) {
        SIM7680.print("NGUOI BI VA CHAM KHI THAM GIAO THONG");
        SIM7680.write(26);
        simState = SIM_SEND;
      }
      break;
      
    case SIM_SEND:
      if (checkResponse("+CMGS:")) {
        impact_detected = false;
        simState = SIM_IDLE;
      }
      break;
  }
}

// Cập nhật buzzer
void updateBuzzer() {
  bool anyAlertActive = bleAlertActive || impact_detected || eyeClosed;
  
  if (antiTheftEnabled) {
    // Bật buzzer nếu có bất kỳ cảnh báo nào
    if (anyAlertActive) {
      if (buzzerStartTime == 0) {
        buzzerStartTime = millis(); // Bắt đầu đếm thời gian
      }
      digitalWrite(BUZZER_PIN, HIGH);
      
      // Tự động tắt sau 5 phút
      if (millis() - buzzerStartTime >= buzzerDuration) {
        digitalWrite(BUZZER_PIN, LOW);
        buzzerStartTime = 0;
        antiTheftEnabled = false;
        bleAlertActive = false;
        impact_detected = false;
        eyeClosed = false;
      }
    } 
    else {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerStartTime = 0; // Reset timer khi không có cảnh báo
    }
  } 
  else {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerStartTime = 0; // Đảm bảo tắt buzzer khi hệ thống vô hiệu hóa
  }
}
