#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SD.h>
#include <sys/time.h>

#define BATTERY_ADC_PIN 17
#define ADC_REFERENCE_VOLTAGE 3.3
#define ADC_RESOLUTION 4096.0
#define VOLTAGE_DIVIDER_RATIO 2.0

#define TFT_SCLK 9
#define TFT_MOSI 10
#define TFT_DC 8
#define TFT_CS 14
#define TFT_RST 18
#define TFT_BLK 13

#define SD_MISO 1
#define SD_MOSI 2
#define SD_SCLK 3
#define SD_CS 46

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;

SPIClass SPI_TFT(HSPI);
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI_TFT, TFT_CS, TFT_DC, TFT_RST);
SPIClass SPI_SD(HSPI);

unsigned long lastLogTime = 0;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { deviceConnected = true; }
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->setMinPreferred(0x20);
        BLEDevice::startAdvertising();
    }
};

void setupTime() {
    struct tm timeinfo;
    timeinfo.tm_year = 2025 - 1900;  // 年份 - 1900
    timeinfo.tm_mon = 2;             // 3 月（从 0 开始）
    timeinfo.tm_mday = 17;
    timeinfo.tm_hour = 14;
    timeinfo.tm_min = 30;
    timeinfo.tm_sec = 0;
    time_t t = mktime(&timeinfo);
    struct timeval now = { .tv_sec = t };
    settimeofday(&now, NULL);
}

String getFormattedTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "0000-00-00 00:00:00";
    }
    char timeString[30];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeString);
}

void setup() {
    Serial.begin(115200);
    SPI_TFT.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    SPI_SD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    pinMode(TFT_BLK, OUTPUT);
    digitalWrite(TFT_BLK, HIGH);

    tft.init(240, 240, SPI_MODE2);
    tft.setRotation(3);
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(20, 20);
    tft.println("ESP32S3 - Battery Voltage");

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    BLEDevice::init("ESP32S3_Battery");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pCharacteristic->addDescriptor(new BLE2902());
    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    BLEDevice::startAdvertising();

    Serial.println("Setting system time...");
    setupTime();
    Serial.println("Time set successfully");

    Serial.println("Initializing SD card...");
    if (!SD.begin(SD_CS, SPI_SD, 1000000)) {
        Serial.println("SD initialization failed!");
        while (1);
    }
    Serial.println("SD initialized successfully");
}

void logDataToSD(float voltage) {
    String timestamp = getFormattedTime();
    for (int i = 0; i < 3; i++) {
        File logFile = SD.open("/log.txt", FILE_APPEND);
        if (logFile) {
            logFile.printf("%s, %.2f\n", timestamp.c_str(), voltage);
            logFile.flush();
            logFile.close();
            Serial.println("Data saved to SD: " + timestamp);
            return;
        } else {
            Serial.println("Failed to open log file, retrying...");
            delay(100);
        }
    }
    Serial.println("Error: Could not write to SD after retries!");
}

String formatVoltageToJSON(float voltage) {
    return "{\"voltage\":" + String(voltage, 2) + "}";
}

void loop() {
    if (millis() - lastLogTime >= 2000) {
        lastLogTime = millis();
        float batteryVoltage = analogRead(BATTERY_ADC_PIN) * (ADC_REFERENCE_VOLTAGE / ADC_RESOLUTION) * VOLTAGE_DIVIDER_RATIO;

        tft.fillRect(20, 100, 200, 40, ST77XX_BLACK);
        tft.setCursor(20, 100);
        tft.setTextColor(ST77XX_WHITE);
        tft.setTextSize(2);
        tft.printf("Voltage: %.2f V", batteryVoltage);

        if (deviceConnected) {
            pCharacteristic->setValue(formatVoltageToJSON(batteryVoltage).c_str());
            pCharacteristic->notify();
        }

        logDataToSD(batteryVoltage);
    }
}
