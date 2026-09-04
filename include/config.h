#ifndef ARDUINO
  // Fallbacks pour l'environnement Native / Mock
  #define D2 2
  #define D3 3
  #define D4 4
  #define D5 5
  #define D6 6
  #define A0 0
  #define INPUT 0
  #define CHANGE 1
#endif

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// 1. MAPPING DES PINS (Arduino Nano ESP32 / ESP32-S3)
// ============================================================================

// #define PIN_SENSOR_ID           1   // Broche A0  (GPIO 1)  -> Pin 8 DE-9
#define PIN_SENSOR_ID           A0
#define PIN_SENSOR_TOP_LEFT     D2   // Broche D2  (GPIO 5)  -> Pin 3 DE-9
#define PIN_SENSOR_BOT_LEFT     D3   // Broche D3  (GPIO 6)  -> Pin 4 DE-9
#define PIN_SENSOR_CENTER       D4   // Broche D4  (GPIO 7)  -> Pin 5 DE-9
#define PIN_SENSOR_TOP_RIGHT    D5   // Broche D5  (GPIO 8)  -> Pin 6 DE-9
#define PIN_SENSOR_BOT_RIGHT    D6   // Broche D6  (GPIO 9)  -> Pin 7 DE-9

#define NUM_SENSORS             5

const uint8_t SENSOR_PINS[NUM_SENSORS] = {
    PIN_SENSOR_TOP_LEFT, PIN_SENSOR_BOT_LEFT, PIN_SENSOR_CENTER, 
    PIN_SENSOR_TOP_RIGHT, PIN_SENSOR_BOT_RIGHT
};

// ============================================================================
// 2. TYPES ET STRUCTURES DE DONNÉES
// ============================================================================

enum SensorFormat {
    FORMAT_UNKNOWN = 0,
    FORMAT_24X36,
    FORMAT_6X6,
    FORMAT_6X9,
    FORMAT_645,
    FORMAT_RESERVE
};

enum SensorIndex {
    INDEX_TOP_LEFT = 0, INDEX_BOT_LEFT, INDEX_CENTER, INDEX_TOP_RIGHT, INDEX_BOT_RIGHT
};

enum ShutterType {
    SHUTTER_UNKNOWN = 0,
    SHUTTER_CENTRAL,
    SHUTTER_FOCAL_HORIZONTAL,
    SHUTTER_FOCAL_VERTICAL
};

struct SensorTiming {
    uint32_t riseTime;
    uint32_t fallTime;
    bool isValid;
};

// 🛠️ FIX : Une seule définition propre et consolidée
struct ShutterMeasurement {
    uint32_t captureTimestamp;
    SensorFormat format;
    ShutterType type;
    bool allSensorsValid;             // Flag d'intégrité (génère le 'partial' en JSON)
    
    float durationCenter_ms;
    float calculatedSpeed_s;
    float nominalSpeed_s;             // Vitesse cible (ex: 0.002 pour 1/500s)
    float deltaEV;

    float curtain1_travelTime_ms;
    float curtain2_travelTime_ms;
    float curtain1_speed_mps;         // Vitesse physique (m/s)
    float curtain2_speed_mps;         // Vitesse physique (m/s)

    float curtain1_skew_ms;
    float curtain2_skew_ms;
    float gapDivergence_percent;
    
    SensorTiming sensors[NUM_SENSORS];
};

// ============================================================================
// 3. SEUILS ET CALIBRATION DÉTECTION MODULE (AUTO-ID)
// ============================================================================

#define ADC_RESOLUTION          4095.0f
#define VREF_VOLTAGE            3.3f
#define ID_PULLUP_RESISTOR_OHM  47000.0f
#define AUTO_ID_TOLERANCE_PCT   0.40f

struct ModuleIDConfig {
    SensorFormat format;
    const char* name;
    float targetResistorOhm;
    float width_mm;                   // 🛠️ NOUVEAU : Dimensions physiques pour le calcul m/s
    float height_mm;
};

const ModuleIDConfig MODULE_CONFIGS[] = {
    { FORMAT_24X36,  "24x36",               1000.0f,  36.0f, 24.0f },
    { FORMAT_6X6,    "6x6 (Moyen Format)",  3300.0f,  56.0f, 56.0f },
    { FORMAT_6X9,    "6x9 (Moyen Format)", 10000.0f,  84.0f, 56.0f },
    { FORMAT_645,    "645 (Moyen Format)", 33000.0f,  56.0f, 41.5f },
    { FORMAT_RESERVE,"Réserve / Futur",   100000.0f,   0.0f,  0.0f }
};
const uint8_t NUM_MODULE_CONFIGS = sizeof(MODULE_CONFIGS) / sizeof(MODULE_CONFIGS[0]);

// ============================================================================
// 4. PARAMÈTRES DU RÉSEAU ET DU SERVEUR WEB
// ============================================================================

#define WIFI_AP_SSID            "Banc_Obturateur_ESP32"
#define WIFI_AP_PASSWORD        "123456789"
#define WEBSERVER_PORT          80

#endif // CONFIG_H