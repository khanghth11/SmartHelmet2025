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

// BLE Configuration
int scanTime = 5; 
BLEScan *pBLEScan;
BLEAddress targetAddress("19:ce:a9:93:71:4e");

// Impact Detection Variables
int xaxis, yaxis, zaxis;
int deltx, delty, deltz;
int vibration = 2, devibrate = 10;
int magnitude = 0;
int sensitivity = 150;      
bool impact_detected = false;
unsigned long impact_time;
const unsigned long alert_delay = 5000;

// Piezoelectric Sensor
int piezoThreshold = 500;   

// SIM 4G A7680C Configuration
HardwareSerial SIM7680(1);
String number1 = "0xxxxxxxxx";

// BLE Callback
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        Serial.printf("Found BLE device: %s\n", advertisedDevice.toString().c_str());
        if (advertisedDevice.haveServiceUUID()) {
            BLEUUID uuid = advertisedDevice.getServiceUUID();
            Serial.printf("Service UUID: %s\n", uuid.toString().c_str());
        }
        if (advertisedDevice.getAddress().equals(targetAddress)) {
            int rssi = advertisedDevice.getRSSI();
            Serial.printf("Found target device: %s, RSSI: %d\n", 
                          advertisedDevice.getAddress().toString().c_str(), 
                          rssi);
            digitalWrite(BUZZER_PIN, rssi < -51 ? HIGH : LOW);
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
    
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    
    xaxis = analogRead(xPin);
    yaxis = analogRead(yPin);
    zaxis = analogRead(zPin);
    Serial.println("System initialized.");
}

void loop() {
    scanBLE();
    checkPiezoVibration();
    detectCollision();
    if (impact_detected && millis() - impact_time >= alert_delay) {
        sendAlert();
    }
    delay(2000);
}

void scanBLE() {
    pBLEScan->start(scanTime, false);
    Serial.println("BLE scan done!");
    pBLEScan->clearResults();
}

void checkPiezoVibration() {
    int piezoValue = analogRead(PIEZO_PIN);
    if (piezoValue > piezoThreshold) {
        Serial.println("Piezo vibration detected!");
        digitalWrite(BUZZER_PIN, HIGH);
        delay(1000);
        digitalWrite(BUZZER_PIN, LOW);
    }
}

void detectCollision() {
    static unsigned long lastCheck = 0;
    if (micros() - lastCheck < 2000) return;
    lastCheck = micros();
    
    int oldx = xaxis, oldy = yaxis, oldz = zaxis;
    xaxis = analogRead(xPin);
    yaxis = analogRead(yPin);
    zaxis = analogRead(zPin);
    
    deltx = xaxis - oldx;
    delty = yaxis - oldy;
    deltz = zaxis - oldz;
    magnitude = sqrt(sq(deltx) + sq(delty) + sq(deltz));
    
    if (magnitude >= sensitivity) {
        impact_detected = true;
        impact_time = millis();
        digitalWrite(BUZZER_PIN, HIGH);
    }
}

void sendAlert() {
    digitalWrite(BUZZER_PIN, LOW);
    SIM7680.println("AT+CMGF=1");
    delay(500);
    SIM7680.println("AT+CMGS=\"" + number1 + "\"");
    delay(500);
    SIM7680.print("NGUOI BI VA CHAM KHI THAM GIA GIAO THONG");
    delay(500);
    SIM7680.write(26);
    delay(500);
    Serial.println("SMS sent!");
    impact_detected = false;
}