#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <HardwareSerial.h>
#include <math.h>
#include <string.h>
#include <Wire.h>
#include <MPU6050.h>

#define BUZZER_PIN 23
#define PIEZO_PIN 34
#define SIM_RX_PIN 16
#define SIM_TX_PIN 17
#define IR_SENSOR_PIN 36
#define BUTTON_PIN 25
bool antiTheftEnabled = true;
unsigned long buttonPressTime = 0;                   // Thoi gian nhan nut
bool buttonActive = false;                           // Trang thai nut nhan
unsigned long buzzerStartTime = 0;                   // Thoi diem buzzer bat dau keu
const unsigned long buzzerDuration = 5 * 60 * 1000;  // 5 phut (tinh bang mili giay)
// BLE Configuration
int scanTime = 2;
BLEScan* pBLEScan;
BLEAddress targetAddress("dc:47:5d:13:b7:41");
bool bleAlertActive = false;
void printDebugInfo();
// Impact Detection Variables
int compositeThreshold;
bool impact_detected = false;
unsigned long impact_time;
const unsigned long alert_delay = 5000;

//IR Sensor Variables
int irThreshold = 2500;
bool eyeClosed = false;
unsigned long eyeClosedTime = 0;
const unsigned long sleepThreshold = 4000;

//SIM 4G A7680C Configuration
HardwareSerial SIM7680(2);
const char number1[] = "0933845687";
enum SimState { SIM_IDLE,
                SIM_CMGF,
                SIM_CMGS,
                SIM_SEND };
SimState simState = SIM_IDLE;
unsigned long simTimeout = 0;
int retryCount = 0;
char simResponse[256];
int simResponseIndex = 0;

//MPU6050 Configuration
MPU6050 mpu;
//System Variables
unsigned long lastScanMillis = 0;
const long scanInterval = 5000;

//Ham prototype
bool checkResponse(const char* target);
void updateSMSSending();
void updateBuzzer();
void readSIMResponse();
void checkEyeState();
void detectImpact();
void printSensorValues(); 

float calculateDistance(int rssi, int txPower, float n) {
  return pow(10, ((float)(txPower - rssi)) / (10 * n));
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    int rssi = advertisedDevice.getRSSI();
    float distance = calculateDistance(rssi, -40, 2.5);
    Serial.print("Device ");
    Serial.print(advertisedDevice.getAddress().toString().c_str());
    Serial.print(" RSSI: ");
    Serial.print(rssi);
    Serial.print(" | Distance: ");
    Serial.print(distance);
    Serial.println(" m");

    if (advertisedDevice.getAddress().equals(targetAddress)) {
      bleAlertActive = (advertisedDevice.getRSSI() < -62);
    }
  }
};
void setup() {
  Serial.begin(115200);
  SIM7680.begin(115200, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PIEZO_PIN, INPUT);
  pinMode(IR_SENSOR_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);  

  Wire.begin();
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 connection failed");
    while (1)
      ;
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
const unsigned long SMS_COOLDOWN = 30000;  // 30s
void loop() {
  unsigned long currentMillis = millis();
  readSIMResponse();

  // BLE Scan (chi khi chong trom bat)
  if (antiTheftEnabled && (currentMillis - lastScanMillis >= scanInterval)) {
    lastScanMillis = currentMillis;
    Serial.println("Starting BLE Scan (Non-Blocking)..."); 
    pBLEScan->clearResults();
    pBLEScan->start(scanTime, false); 
    Serial.println("BLE Scan initiated.");  

  }  // Ket thuc khoi if quet BLE

  detectImpact();
  checkEyeState();

  // Xu ly gui SMS neu co va cham VA SIM dang ranh
  if (impact_detected && simState == SIM_IDLE) {
    Serial.println("Impact detected & SIM IDLE, starting SMS sequence.");  // Them log
    updateSMSSending();
  }
  // Tiep tuc xu ly state machine cua SIM neu dang gui
  if (simState != SIM_IDLE) {
    updateSMSSending();
  }

  updateBuzzer();
  handleButton();

  // --- PHAN THEM DEBUG ---
  static unsigned long lastPrintTime = 0;
  const unsigned long printInterval = 1000;  // In moi 1000ms = 1 giay
  if (currentMillis - lastPrintTime >= printInterval) {
    lastPrintTime = currentMillis;
    printDebugInfo();  // Goi ham in debug
  }
  
}  

void handleButton() {
  static bool buzzerState = false;
  static unsigned long lastBuzzerTime = 0;

  if (digitalRead(BUTTON_PIN) == LOW) {  // Nut duoc nhan
    if (!buttonActive) {
      buttonActive = true;
      buttonPressTime = millis();
    }

    if (millis() - buttonPressTime >= 5000) {  // Nhan giu 5 giay
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
    } else if (millis() - buttonPressTime >= 3000 && (impact_detected || bleAlertActive || eyeClosed)) {  // Nhan giu 3 giay khi buzzer dang keu
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



#define MAX_MAGNITUDE 2.5  // Nguong gia toc (tuy chinh theo thuc te)
#define MAX_VIBRATION 300  // Nguong cam bien rung

void detectImpact() {
  static unsigned long lastCheck = 0;
  static int maxVibration = 0;
  static unsigned long lastResetTime = 0;

  if (millis() - lastCheck < 50) return;
  lastCheck = millis();

  // Doc gia tri gia toc tu MPU6050
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float x = ax / 16384.0;
  float y = ay / 16384.0;
  float z = az / 16384.0;

  float magnitude = sqrt(sq(x) + sq(y) + sq(z));

  // Doc gia tri tu cam bien rung
  int piezoValue = analogRead(PIEZO_PIN);

  // Cap nhat gia tri lon nhat trong khoang thoi gian 5 giay
  if (piezoValue > maxVibration) {
    maxVibration = piezoValue;
  }

  // Reset gia tri cam bien rung moi 5 giay
  if (millis() - lastResetTime > 5000) {
    maxVibration = 0;
    lastResetTime = millis();
  }

  // Phat hien va cham khi magnitude hva maxVibration vuot nguong
  if (magnitude > MAX_MAGNITUDE && maxVibration > MAX_VIBRATION) {
    impact_detected = true;
    impact_time = millis();
  }

  // Reset lai trang thai va cham sau 5 giay
  if (impact_detected && millis() - impact_time > 5000) {
    impact_detected = false;
  }
}

void checkEyeState() {
  int irValue = analogRead(IR_SENSOR_PIN);

  if (irValue < irThreshold) {
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

// Doc phan hoi tu SIM
void readSIMResponse() {
  while (SIM7680.available() && simResponseIndex < 255) {
    char c = SIM7680.read();
    // Xu ly ket thuc dong
    if (c == '\r' || c == '\n') {
      if (simResponseIndex > 0) {              // Neu co du lieu
        simResponse[simResponseIndex] = '\0';  // Ket thuc chuoi
        Serial.print("SIM Response: ");
        Serial.println(simResponse);
        simResponseIndex = 0;  // Reset buffer sau khi xu ly
      }
    } else {
      simResponse[simResponseIndex++] = c;
    }
  }
}

bool checkResponse(const char* target) {
  if (strstr(simResponse, target) != NULL) {
    // Reset buffer sau khi tim thay phan hoi
    simResponseIndex = 0;
    simResponse[0] = '\0';
    return true;
  }
  return false;
}

// Gui SMS khi co va cham
void updateSMSSending() {
  switch (simState) {
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
          impact_detected = false;  // Dat lai trang thai neu that bai
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
          impact_detected = false;  // Dat lai trang thai neu that bai
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
        impact_detected = false;  // Dat lai trang thai sau khi gui SMS thanh cong
        simState = SIM_IDLE;
      } else if (millis() - simTimeout > 5000) {
        if (++retryCount >= 3) {
          impact_detected = false;  // Dat lai trang thai neu that bai
          Serial.println("Error: Failed to send SMS");
        } else {
          simState = SIM_IDLE;
          Serial.println("Retrying SMS sending...");
        }
      }
      break;
  }
}
// Cap nhat buzzer
void updateBuzzer() {
  bool anyAlertActive = bleAlertActive;

  // Bat buzzer khi phat hien va cham hoac buon ngu, bat ke che do chong trom
  if (impact_detected || eyeClosed) {
    if (buzzerStartTime == 0) {
      buzzerStartTime = millis();
    }
    digitalWrite(BUZZER_PIN, HIGH);

    // Tu dong tat sau 5 phut
    if (millis() - buzzerStartTime >= buzzerDuration) {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerStartTime = 0;
      impact_detected = false;  // Reset flag
      eyeClosed = false;
    }
  }
  // Dieu khien Buzzer khi bat che do chong trom
  else if (antiTheftEnabled) {
    // Bat buzzer neu co canh bao BLE
    if (anyAlertActive) {
      if (buzzerStartTime == 0) {
        buzzerStartTime = millis();
      }
      digitalWrite(BUZZER_PIN, HIGH);

      // Tu dong tat sau 5 phut
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
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerStartTime = 0;
  }
}
void printDebugInfo() {
  unsigned long currentMillis = millis();
  Serial.println("\n----- DEBUG INFO -----");

  //Trang thai he thong
  Serial.print("Timestamp: ");
  Serial.println(currentMillis);
  Serial.print("Anti-Theft Mode: ");
  Serial.println(antiTheftEnabled ? "ENABLED" : "DISABLED");
  Serial.print("Button Active: ");
  Serial.println(buttonActive ? "YES" : "NO");
  Serial.print("Button Press Time: ");
  Serial.println(buttonPressTime);

  //Trang thai Buzzer
  bool isBuzzerOn = digitalRead(BUZZER_PIN);  // Doc trang thai thuc te cua pin
  Serial.print("Buzzer Pin State: ");
  Serial.println(isBuzzerOn ? "HIGH (ON)" : "LOW (OFF)");
  Serial.print("Buzzer Start Time: ");
  Serial.println(buzzerStartTime);
  if (isBuzzerOn && buzzerStartTime > 0) {
    Serial.print("Buzzer Active Duration: ");
    Serial.print((currentMillis - buzzerStartTime) / 1000);
    Serial.println(" s");
  }

  //Canh bao BLE
  Serial.print("BLE Alert Active: ");
  Serial.println(bleAlertActive ? "YES" : "NO");

  //Canh bao Va cham
  Serial.print("Impact Detected Flag: ");
  Serial.println(impact_detected ? "YES" : "NO");
  if (impact_detected) {
    Serial.print("Impact Time: ");
    Serial.println(impact_time);
    Serial.print("Time Since Impact: ");
    Serial.print((currentMillis - impact_time) / 1000);
    Serial.println(" s");
  }
  //Doc gia tri cam bien lien quan den va cham
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float x = ax / 16384.0;
  float y = ay / 16384.0;
  float z = az / 16384.0;
  float magnitude = sqrt(sq(x) + sq(y) + sq(z));
  int piezoValue = analogRead(PIEZO_PIN);
  Serial.print("MPU Magnitude: ");
  Serial.print(magnitude);
  Serial.print(" (Threshold: ");
  Serial.print(MAX_MAGNITUDE);
  Serial.println(")");
  Serial.print("Piezo Value: ");
  Serial.print(piezoValue);
  Serial.print(" (Threshold: ");
  Serial.print(MAX_VIBRATION);
  Serial.println(")");


  //Canh bao Buon ngu (Mat nham)
  Serial.print("Eye Closed Flag: ");
  Serial.println(eyeClosed ? "YES" : "NO");
  int irValue = analogRead(IR_SENSOR_PIN);
  Serial.print("IR Sensor Value: ");
  Serial.print(irValue);
  Serial.print(" (Threshold: ");
  Serial.print(irThreshold);
  Serial.println(")");
  if (eyeClosedTime > 0) {
    Serial.print("Eye Closed Start Time: ");
    Serial.println(eyeClosedTime);
    Serial.print("Eye Closed Duration: ");
    Serial.print(currentMillis - eyeClosedTime);
    Serial.println(" ms");
  } else {
    // Serial.println("Eye is Open");
  }

  // Trang thai SIM
  Serial.print("SIM State: ");
  Serial.println(simState);  // (0=IDLE, 1=CMGF, 2=CMGS, 3=SEND)
  Serial.print("SIM Retry Count: ");
  Serial.println(retryCount);
}