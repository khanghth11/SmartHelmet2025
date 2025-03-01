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
String number1 = "0xxxxxxxxx";
enum SimState { SIM_IDLE, SIM_CMGF, SIM_CMGS, SIM_MESSAGE, SIM_SEND };
SimState simState = SIM_IDLE;
unsigned long simTimeout = 0;
int retryCount = 0;
String simResponse;

// System Variables
unsigned long lastScanMillis = 0;
const long scanInterval = 5000;
int baselineX, baselineY, baselineZ;

void calibrateSensors();
bool checkResponse(const String& target);
void updateSMSSending();
void updateBuzzer();

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        if (advertisedDevice.getAddress().equals(targetAddress)) {
            int rssi = advertisedDevice.getRSSI();
            bleAlertActive = (rssi < -51);
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
    
    calibrateSensors();
    Serial.println("System initialized.");
}

void loop() {
    unsigned long currentMillis = millis();
    
    // Đọc phản hồi SIM đầu tiên
    readSIMResponse();
    
    // Non-blocking BLE scan
    if (currentMillis - lastScanMillis >= scanInterval) {
        lastScanMillis = currentMillis;
        BLEScanResults* results = pBLEScan->start(scanTime, false);
        pBLEScan->clearResults();
        delete results;
    }
    
    detectImpact();
    checkEyeState();
    
    if (impact_detected) {
        updateSMSSending();
    }
    
    updateBuzzer();
}

void calibrateSensors() {
 bool checkResponse(const String& target, unsigned long timeout);
}

void detectImpact() {
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 10) return;
    lastCheck = millis();

      int x = analogRead(xPin);
    int y = analogRead(yPin);
    int z = analogRead(zPin);
    
    float magnitude = sqrt(sq(x - baselineX) + sq(y - baselineY) + sq(z - baselineZ));
    int piezoValue = analogRead(PIEZO_PIN);
    
    // Apply EMA filters
    emaMagnitude = alpha * magnitude + (1 - alpha) * emaMagnitude;
    emaPiezo = alpha * piezoValue + (1 - alpha) * emaPiezo;
    
    float compositeValue = 0.7 * emaMagnitude + 0.3 * emaPiezo;
    
    if (compositeValue >= compositeThreshold && !impact_detected) {
        impact_detected = true;
        impact_time = millis();
        simState = SIM_IDLE;
        retryCount = 0;
    }
}

void checkEyeState() {
    static unsigned long lastIRCheck = 0;
    if (millis() - lastIRCheck < 100) return;
    lastIRCheck = millis();
    
    int irValue = analogRead(IR_SENSOR_PIN);
    eyeClosed = (irValue > irThreshold);
    
    if (eyeClosed && (millis() - eyeClosedTime < sleepThreshold)) {
        eyeClosedTime = millis();
    }
}

void readSIMResponse() {
    while (SIM7680.available()) {
        char c = SIM7680.read();
        simResponse += c;
    }
}

bool checkResponse(const String& target) {
    if (simResponse.indexOf(target) != -1) {
        simResponse = "";
        return true;
    }
    return false;
}

void updateSMSSending() {
    switch(simState) {
        case SIM_IDLE:
            SIM7680.println("AT+CMGF=1");
            simState = SIM_CMGF;
            simTimeout = millis();
            break;
            
        case SIM_CMGF:
            if (checkResponse("OK")) {
                SIM7680.print("AT+CMGS=\"");
                SIM7680.print(number1);
                SIM7680.println("\"");
                simState = SIM_CMGS;
                simTimeout = millis();
            } else if (millis() - simTimeout > 1000) {
                if (++retryCount >= 3) {
                    impact_detected = false;
                    Serial.println("Error: Failed to set SMS format");
                } else simState = SIM_IDLE;
            }
            break;
            
        case SIM_CMGS:
            if (checkResponse(">")) {
                SIM7680.print("NGUOI BI VA CHAM KHI THAM GIA GIAO THONG");
                SIM7680.write(26);
                simState = SIM_SEND;
                simTimeout = millis();
            } else if (millis() - simTimeout > 1000) {
                if (++retryCount >= 3) {
                    impact_detected = false;
                    Serial.println("Error: Failed to prepare SMS");
                } else simState = SIM_IDLE;
            }
            break;
            
        case SIM_SEND:
            if (checkResponse("+CMGS:")) {
                impact_detected = false;
                simState = SIM_IDLE;
            } else if (millis() - simTimeout > 5000) {
                if (++retryCount >= 3) {
                    impact_detected = false;
                    Serial.println("Error: Failed to send SMS");
                } else simState = SIM_IDLE;
            }
            break;
    }
}

void updateBuzzer() {
    bool shouldBuzz = false;
    
    // Priority order: Impact > BLE > Eye
    if (impact_detected) {
        shouldBuzz = true;
    } else if (bleAlertActive) {
        shouldBuzz = true;
    } else if (eyeClosed && (millis() - eyeClosedTime >= sleepThreshold)) {
        shouldBuzz = true;
    }
    
    digitalWrite(BUZZER_PIN, shouldBuzz ? HIGH : LOW);
}
