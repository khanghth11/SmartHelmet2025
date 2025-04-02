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
void printDebugInfo();
// Impact Detection Variables
int compositeThreshold;
bool impact_detected = false;
unsigned long impact_time;
const unsigned long alert_delay = 5000;

// IR Sensor Variables
int irThreshold = 2500; 
bool eyeClosed = false;
unsigned long eyeClosedTime = 0;
const unsigned long sleepThreshold = 4000;

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
        // Chỉ xử lý thiết bị mục tiêu
        if (advertisedDevice.getAddress().equals(targetAddress)) {
            int rssi = advertisedDevice.getRSSI();
            Serial.print("Target Device ");
            Serial.print(advertisedDevice.getAddress().toString().c_str());
            Serial.print(" RSSI: ");
            Serial.println(rssi); // In RSSI để dễ theo dõi

            // <<<--- ĐIỀU CHỈNH NGƯỠNG RSSI Ở ĐÂY NẾU CẦN ---<<<
            int rssiThreshold = -75; // Bắt đầu thử nghiệm với giá trị này (hoặc -80, -70 tùy môi trường)

            if (rssi < rssiThreshold) { // Thiết bị ở xa (tín hiệu yếu hơn ngưỡng)
                if (!bleAlertActive) { // Chỉ cập nhật và log nếu trạng thái thay đổi từ false -> true
                    Serial.println("!!! BLE Alert: Target device OUT OF RANGE !!!");
                    bleAlertActive = true; // Đặt thành TRUE khi ra xa
                }
            } else { // Thiết bị ở gần (tín hiệu mạnh hơn hoặc bằng ngưỡng)
                if (bleAlertActive) { // Chỉ cập nhật và log nếu trạng thái thay đổi từ true -> false
                    Serial.println("--- BLE Alert: Target device BACK IN RANGE ---");
                    bleAlertActive = false; // Quan trọng: Đặt lại thành FALSE khi quay lại gần
                }
            }
        }
        // Optional: Log other devices if needed for debugging
        // else {
        //   Serial.print("Other Device ");
        //   Serial.print(advertisedDevice.getAddress().toString().c_str());
        //   Serial.print(" RSSI: ");
        //   Serial.println(advertisedDevice.getRSSI());
        // }
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

  // BLE Scan (chỉ khi chống trộm bật)
  if (antiTheftEnabled && (currentMillis - lastScanMillis >= scanInterval)) {
    lastScanMillis = currentMillis;
    Serial.println("Starting BLE Scan (Non-Blocking)..."); // Thêm log

    // Xóa kết quả từ lần quét trước (nên làm trước khi bắt đầu)
    pBLEScan->clearResults();

    // Bắt đầu quét không chặn, chạy trong 'scanTime' giây.
    // Kết quả được xử lý trong callback onResult.
    // Không cần lưu giá trị trả về khi dùng non-blocking kiểu này.
    pBLEScan->start(scanTime, false); // false = không chặn

    Serial.println("BLE Scan initiated."); // Log rằng lệnh quét đã được gọi

  } // Kết thúc khối if quét BLE

  detectImpact();
  checkEyeState();

  // Xử lý gửi SMS nếu có va chạm VÀ SIM đang rảnh
  if (impact_detected && simState == SIM_IDLE) {
     Serial.println("Impact detected & SIM IDLE, starting SMS sequence."); // Thêm log
     updateSMSSending();
  }
  // Tiếp tục xử lý state machine của SIM nếu đang gửi
  if (simState != SIM_IDLE) {
     updateSMSSending();
  }

  updateBuzzer();
  handleButton();

  // --- PHẦN THÊM DEBUG ---
  static unsigned long lastPrintTime = 0;
  const unsigned long printInterval = 1000; // In mỗi 1000ms = 1 giây
  if (currentMillis - lastPrintTime >= printInterval) {
    lastPrintTime = currentMillis;
    printDebugInfo(); // Gọi hàm in debug
  }
  // --- KẾT THÚC PHẦN DEBUG ---
} // Kết thúc hàm loop()

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
  bool anyAlertActive = bleAlertActive;

  // Bật buzzer khi phát hiện va chạm hoặc buồn ngủ, bất kể chế độ chống trộm
  if (impact_detected || eyeClosed) {
    if (buzzerStartTime == 0) {
      buzzerStartTime = millis();
    }
    digitalWrite(BUZZER_PIN, HIGH);

    // Tự động tắt sau 5 phút
    if (millis() - buzzerStartTime >= buzzerDuration) {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerStartTime = 0;
      impact_detected = false;  // Reset flag
      eyeClosed = false;
    }
  }
  // Điều khiển Buzzer khi bật chế độ chống trộm
  else if (antiTheftEnabled) {
    // Bật buzzer nếu có cảnh báo BLE
    if (anyAlertActive) {
      if (buzzerStartTime == 0) {
        buzzerStartTime = millis();
      }
      digitalWrite(BUZZER_PIN, HIGH);

      // Tự động tắt sau 5 phút
      if (millis() - buzzerStartTime >= buzzerDuration) {
        digitalWrite(BUZZER_PIN, LOW);
        buzzerStartTime = 0;
        bleAlertActive = false;
        antiTheftEnabled = false;
      }
    } else {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerStartTime = 0;
    }
  }
   else {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerStartTime = 0;
    }
}
void printDebugInfo() {
  unsigned long currentMillis = millis();
  Serial.println("\n----- DEBUG INFO -----");

  // === Trạng thái hệ thống ===
  Serial.print("Timestamp: "); Serial.println(currentMillis);
  Serial.print("Anti-Theft Mode: "); Serial.println(antiTheftEnabled ? "ENABLED" : "DISABLED");
  Serial.print("Button Active: "); Serial.println(buttonActive ? "YES" : "NO");
  Serial.print("Button Press Time: "); Serial.println(buttonPressTime);

  // === Trạng thái Buzzer ===
  bool isBuzzerOn = digitalRead(BUZZER_PIN); // Đọc trạng thái thực tế của pin
  Serial.print("Buzzer Pin State: "); Serial.println(isBuzzerOn ? "HIGH (ON)" : "LOW (OFF)");
  Serial.print("Buzzer Start Time: "); Serial.println(buzzerStartTime);
  if (isBuzzerOn && buzzerStartTime > 0) {
      Serial.print("Buzzer Active Duration: ");
      Serial.print((currentMillis - buzzerStartTime) / 1000);
      Serial.println(" s");
  }

  // === Cảnh báo BLE ===
  Serial.print("BLE Alert Active: "); Serial.println(bleAlertActive ? "YES" : "NO");
  // Lưu ý: Giá trị RSSI cuối cùng của target chỉ được cập nhật trong callback.
  // Để xem RSSI liên tục, bạn cần log trong callback hoặc lưu lại giá trị cuối cùng.

  // === Cảnh báo Va chạm ===
  Serial.print("Impact Detected Flag: "); Serial.println(impact_detected ? "YES" : "NO");
  if (impact_detected) {
    Serial.print("Impact Time: "); Serial.println(impact_time);
    Serial.print("Time Since Impact: ");
    Serial.print((currentMillis - impact_time) / 1000);
    Serial.println(" s");
  }
  // Đọc giá trị cảm biến liên quan đến va chạm
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float x = ax / 16384.0;
  float y = ay / 16384.0;
  float z = az / 16384.0;
  float magnitude = sqrt(sq(x) + sq(y) + sq(z));
  int piezoValue = analogRead(PIEZO_PIN);
  Serial.print("MPU Magnitude: "); Serial.print(magnitude);
  Serial.print(" (Threshold: "); Serial.print(MAX_MAGNITUDE); Serial.println(")");
  Serial.print("Piezo Value: "); Serial.print(piezoValue);
  // Bạn đang dùng maxVibration trong 5s, có thể in cả nó nếu muốn
  // static int lastMaxVibration = 0; // Cần sửa lại logic detectImpact để lấy maxVibration ra đây
  // Serial.print(" (Max Vibration in window: "); Serial.print(lastMaxVibration); Serial.print(")"); // Ví dụ
  Serial.print(" (Threshold: "); Serial.print(MAX_VIBRATION); Serial.println(")");


  // === Cảnh báo Buồn ngủ (Mắt nhắm) ===
  Serial.print("Eye Closed Flag: "); Serial.println(eyeClosed ? "YES" : "NO");
  int irValue = analogRead(IR_SENSOR_PIN);
  Serial.print("IR Sensor Value: "); Serial.print(irValue);
  Serial.print(" (Threshold: "); Serial.print(irThreshold); Serial.println(")");
   if (eyeClosedTime > 0) {
       Serial.print("Eye Closed Start Time: "); Serial.println(eyeClosedTime);
       Serial.print("Eye Closed Duration: ");
       Serial.print(currentMillis - eyeClosedTime);
       Serial.println(" ms");
   } else {
       // Serial.println("Eye is Open"); // Có thể thêm nếu muốn
   }

  // === Trạng thái SIM ===
  Serial.print("SIM State: "); Serial.println(simState); // (0=IDLE, 1=CMGF, 2=CMGS, 3=SEND)
  Serial.print("SIM Retry Count: "); Serial.println(retryCount);

  Serial.println("----------------------\n");
}
