#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <HardwareSerial.h>
#include <math.h>

// GPIO Configuration
#define BUZZER_PIN 23       
#define PIEZO_PIN 34        
#define xPin 35             
#define yPin 32             
#define zPin 33             
#define SIM_RX_PIN 16       
#define SIM_TX_PIN 17       
#define IR_SENSOR_PIN 36    

// BLE Configuration
int scanTime = 2; 
BLEScan *pBLEScan;
BLEAddress targetAddress("19:ce:a9:93:71:4e");
bool bleAlertActive = false; // Thêm biến debounce BLE

// Impact Detection Variables
int xaxis, yaxis, zaxis;
int deltx, delty, deltz;
int magnitude = 0;
int piezoValue = 0;
float compositeValue = 0; 
const int compositeThreshold = 1000; 
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
String number1 = "0xxxxxxxxx";

// Non-blocking delay variables
unsigned long previousMillis = 0;
const long scanInterval = 5000; 

// BLE Callback
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        if (advertisedDevice.getAddress().equals(targetAddress)) {
            int rssi = advertisedDevice.getRSSI();
            if (rssi < -51 && !bleAlertActive) {
                digitalWrite(BUZZER_PIN, HIGH);
                bleAlertActive = true;
            } else if (rssi >= -51 && bleAlertActive) {
                digitalWrite(BUZZER_PIN, LOW);
                bleAlertActive = false;
            }
        }
    }
};

void setup() {
    Serial.begin(115200);
    SIM7680.begin(115200, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
    
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(PIEZO_PIN, INPUT);
    pinMode(xPin, INPUT);
    pinMode(yPin, INPUT);
    pinMode(zPin, INPUT);
    pinMode(IR_SENSOR_PIN, INPUT);
    
    BLEDevice::init("HelmetSmart");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    
    xaxis = analogRead(xPin);
    yaxis = analogRead(yPin);
    zaxis = analogRead(zPin);
    Serial.println("System initialized.");
}

void loop() {
    unsigned long currentMillis = millis();
    
    // Quét BLE mỗi 5 giây không chặn loop
    if (currentMillis - previousMillis >= scanInterval) {
        previousMillis = currentMillis;
        scanBLE();
    }
    
    detectImpact();
    checkEyeState();
    
    if (impact_detected && millis() - impact_time >= alert_delay) {
        sendAlert();
    }
}

void scanBLE() {
    BLEScanResults* results = pBLEScan->start(scanTime, false);
    Serial.println("BLE scan done!");
    pBLEScan->clearResults();
    delete results;
}

void detectImpact() {
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 10) return;
    lastCheck = millis();
    
    int oldx = xaxis, oldy = yaxis, oldz = zaxis;
    xaxis = analogRead(xPin);
    yaxis = analogRead(yPin);
    zaxis = analogRead(zPin);
    
    deltx = xaxis - oldx;
    delty = yaxis - oldy;
    deltz = zaxis - oldz;
    magnitude = sqrt(sq(deltx) + sq(delty) + sq(deltz));
    
    piezoValue = analogRead(PIEZO_PIN);
    
    // Chuẩn hóa giá trị (giả sử cảm biến có range tương đồng)
    compositeValue = 0.7 * magnitude + 0.3 * piezoValue;
    
    if (compositeValue >= compositeThreshold && !impact_detected) {
        impact_detected = true;
        impact_time = millis();
        digitalWrite(BUZZER_PIN, HIGH);
    }
}

void checkEyeState() {
    static unsigned long lastIRCheck = 0;
    if (millis() - lastIRCheck < 100) return; // Kiểm tra mỗi 100ms
    lastIRCheck = millis();
    
    int irValue = analogRead(IR_SENSOR_PIN);
    bool currentEyeState = irValue > irThreshold;
    
    if (currentEyeState) {
        if (!eyeClosed) {
            eyeClosedTime = millis();
            eyeClosed = true;
        } else if (millis() - eyeClosedTime >= sleepThreshold) {
            digitalWrite(BUZZER_PIN, HIGH);
        }
    } else {
        digitalWrite(BUZZER_PIN, LOW);
        eyeClosed = false;
    }
}

bool waitForResponse(const char* target, unsigned long timeout) {
    unsigned long start = millis();
    String response;
    while (millis() - start < timeout) {
        while (SIM7680.available()) {
            char c = SIM7680.read();
            response += c;
            if (response.indexOf(target) != -1) return true;
        }
        delay(10); // Giảm CPU usage
    }
    return false;
}

void sendAlert() {
    bool sendSuccess = false;
    SIM7680.println("AT+CMGF=1");
    if (waitForResponse("OK", 1000)) {
        SIM7680.print("AT+CMGS=\"");
        SIM7680.print(number1);
        SIM7680.println("\"");
        if (waitForResponse(">", 1000)) {
            SIM7680.print("NGUOI BI VA CHAM KHI THAM GIA GIAO THONG");
            SIM7680.write(26);
            if (waitForResponse("+CMGS:", 5000)) {
                Serial.println("SMS sent!");
                sendSuccess = true;
            }
        }
    }
    
    impact_detected = !sendSuccess; // Chỉ reset nếu gửi thành công
    digitalWrite(BUZZER_PIN, LOW);
}
