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
// System Variables
unsigned long lastScanMillis = 0;
const long scanInterval = 5000;

// Hàm prototype
bool checkResponse(const char* target);
void updateSMSSending();
void updateBuzzer();
void readSIMResponse();
void checkEyeState();
void detectImpact();
void printSensorValues(); // Hàm mới để in giá trị cảm biến

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
  SIM7680.println("AT");
  delay(1000);
  SIM7680.println("AT+CMEE=1");
  delay(1000);
  BLEDevice::init("HelmetSmart");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  Serial.println("System initialized.");
}



unsigned long lastSMSTime = 0;
const unsigned long SMS_COOLDOWN = 30000; // 30s
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
   if (impact_detected) {
   updateSMSSending();
}
  updateBuzzer();
  handleButton();

  // In giá trị cảm biến mỗi 500ms
  static unsigned long lastPrintTime = 0;
  if (currentMillis - lastPrintTime >= 500) {
    lastPrintTime = currentMillis;
    printSensorValues();
  }
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
#define MAX_VIBRATION 300  // Ngưỡng cảm biến rung

void detectImpact() {
    static unsigned long lastCheck = 0;
    static int maxVibration = 0;
    static unsigned long lastResetTime = 0;

    if (millis() - lastCheck < 50) return;
    lastCheck = millis();

    // Đọc giá trị gia tốc từ MPU6050
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    float x = ax / 16384.0;
    float y = ay / 16384.0;
    float z = az / 16384.0;

    float magnitude = sqrt(sq(x) + sq(y) + sq(z));

    // Đọc giá trị từ cảm biến rung
    int piezoValue = analogRead(PIEZO_PIN);

    // Cập nhật giá trị lớn nhất trong khoảng thời gian 5 giây
    if (piezoValue > maxVibration) {
        maxVibration = piezoValue;
    }

    // Reset giá trị cảm biến rung mỗi 5 giây
    if (millis() - lastResetTime > 5000) {
        maxVibration = 0;
        lastResetTime = millis();
    }

    // Phát hiện va chạm khi magnitude hvà maxVibration vượt ngưỡng
    if (magnitude > MAX_MAGNITUDE && maxVibration > MAX_VIBRATION) {
        impact_detected = true;
        impact_time = millis();
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

bool checkResponse(const char* target) {
  if (strstr(simResponse, target) != NULL) {
    // Reset buffer sau khi tìm thấy phản hồi
    simResponseIndex = 0;
    simResponse[0] = '\0';
    return true;
  }
  return false;
}
  
// Gửi SMS khi có va chạm
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
          impact_detected = false; // Đặt lại trạng thái nếu thất bại
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
          impact_detected = false; // Đặt lại trạng thái nếu thất bại
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
        impact_detected = false; // Đặt lại trạng thái sau khi gửi SMS thành công
        simState = SIM_IDLE;
      } else if (millis() - simTimeout > 5000) {
        if (++retryCount >= 3) {
          impact_detected = false; // Đặt lại trạng thái nếu thất bại
          Serial.println("Error: Failed to send SMS");
        } else {
          simState = SIM_IDLE;
          Serial.println("Retrying SMS sending...");
        }
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
void printSensorValues() {
  // Đọc giá trị từ cảm biến IR
  int irValue = analogRead(IR_SENSOR_PIN);
  Serial.print("IR Sensor Value: ");
  Serial.println(irValue);

  // Đọc giá trị từ cảm biến rung (Piezo)
  int piezoValue = analogRead(PIEZO_PIN);
  Serial.print("Piezo Sensor Value: ");
  Serial.println(piezoValue);

  // Đọc giá trị gia tốc từ MPU6050
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float x = ax / 16384.0;
  float y = ay / 16384.0;
  float z = az / 16384.0;
  float magnitude = sqrt(sq(x) + sq(y) + sq(z));

  Serial.print("MPU6050 Acceleration (X, Y, Z): ");
  Serial.print(x); Serial.print(", ");
  Serial.print(y); Serial.print(", ");
  Serial.println(z);
  
  Serial.print("Magnitude: ");
  Serial.print(magnitude);
  Serial.print(" | Threshold: ");
  Serial.println(2.5);

  // Xử lý giá trị rung cực đại trong 5 giây
  static int maxVibration = 0;
  static unsigned long lastResetTime = 0;
  if (millis() - lastResetTime > 5000) {
    maxVibration = piezoValue;  // Cập nhật lại từ giá trị mới nhất
    lastResetTime = millis();
  } else {
    if (piezoValue > maxVibration) {
      maxVibration = piezoValue;
    }
  }
  Serial.print("Max Vibration in last 5s: ");
  Serial.println(maxVibration);

  // Xác định va chạm dựa trên gia tốc
  impact_detected = (magnitude > MAX_MAGNITUDE && maxVibration > MAX_VIBRATION);

  Serial.print("Impact Detected: ");
  Serial.println(impact_detected ? "Yes" : "No");

  // In trạng thái mắt
  Serial.print("Eye State: ");
  Serial.println(eyeClosed ? "Closed" : "Open");

  // In trạng thái BLE Alert
  Serial.print("BLE Alert Active: ");
  Serial.println(bleAlertActive ? "Yes" : "No");

  // In trạng thái chống trộm
  Serial.print("Anti-Theft Mode: ");
  Serial.println(antiTheftEnabled ? "Enabled" : "Disabled");

  // Kiểm tra bật/tắt còi báo động
  if (impact_detected || bleAlertActive) {
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }

  Serial.print("Buzzer State: ");
  Serial.println(digitalRead(BUZZER_PIN) == HIGH ? "ON" : "OFF");

  Serial.println("-----------------------------");
}
