#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// 1. MAPPING DES PINS (Arduino Nano ESP32 / ESP32-S3)
// ============================================================================

// Broche analogique pour l'Auto-ID des modules capteurs (Pin 8 DE-9)
// Sur la Nano ESP32, A0 correspond à la broche physique GPIO1 (ADC1_CH0)
#define PIN_SENSOR_ID           A0 

// Entrées logiques pour les 5 capteurs optiques (BPW34 + LM339/LM393)
// Remarque : Sur Nano ESP32, D2 à D6 supportent tous les interruptions matérielles.
#define PIN_SENSOR_TOP_LEFT     D2  // Pin 3 DE-9
#define PIN_SENSOR_BOT_LEFT     D3  // Pin 4 DE-9
#define PIN_SENSOR_CENTER       D4  // Pin 5 DE-9
#define PIN_SENSOR_TOP_RIGHT    D5  // Pin 6 DE-9
#define PIN_SENSOR_BOT_RIGHT    D6  // Pin 7 DE-9

// Nombre total de canaux d'acquisition
#define NUM_SENSORS             5

// Array pratique pour itérer sur les pins capteurs dans le code
const uint8_t SENSOR_PINS[NUM_SENSORS] = {
    PIN_SENSOR_TOP_LEFT,
    PIN_SENSOR_BOT_LEFT,
    PIN_SENSOR_CENTER,
    PIN_SENSOR_TOP_RIGHT,
    PIN_SENSOR_BOT_RIGHT
};

// ============================================================================
// 2. TYPES ET STRUCTURES DE DONNÉES
// ============================================================================

// Énumération des formats de boîtiers supportés
enum SensorFormat {
    FORMAT_UNKNOWN = 0,
    FORMAT_24X36,
    FORMAT_6X6,
    FORMAT_6X9,
    FORMAT_645,
    FORMAT_RESERVE
};

// Indexation des capteurs pour un accès lisible dans les tableaux
enum SensorIndex {
    INDEX_TOP_LEFT = 0,
    INDEX_BOT_LEFT,
    INDEX_CENTER,
    INDEX_TOP_RIGHT,
    INDEX_BOT_RIGHT
};

// Types d'obturateurs reconnus
enum ShutterType {
    SHUTTER_UNKNOWN = 0,
    SHUTTER_CENTRAL,             // Obturateur central / à pales
    SHUTTER_FOCAL_HORIZONTAL,    // Rideaux à défilement horizontal
    SHUTTER_FOCAL_VERTICAL       // Rideaux à défilement vertical
};

// Timestamps bruts capturés par microsecondes (micros()) pour un tir d'obturateur
struct SensorTiming {
    uint32_t riseTime;  // Moment où la lumière apparaît (Front montant)
    uint32_t fallTime;  // Moment où la lumière disparaît (Front descendant)
    bool isValid;       // Vrai si le capteur a bien vu la lumière durant la fenêtre
};

// Structure complète du résultat de mesure d'un tir d'obturateur
struct ShutterMeasurement {
    uint32_t captureTimestamp;
    SensorFormat format;
    SensorTiming sensors[NUM_SENSORS];

    // Classification
    ShutterType type;

    // Vitesses globales
    float durationCenter_ms;          // Temps au centre (ms)
    float calculatedSpeed_s;          // Vitesse au centre (ex: 1/500s)
    float nominalSpeed_s;             // Vitesse consigne théorique
    float deltaEV;                    // Écart global en EV au centre

    // Analyse dynamique des rideaux (en ms)
    float curtain1_travelTime_ms;     // Temps de trajet du 1er rideau (Ouverture)
    float curtain2_travelTime_ms;     // Temps de trajet du 2e rideau (Fermeture)

    // Analyse géométrique de parallélisme (Angles / Défauts)
    float curtain1_skew_ms;           // Décalage Haut vs Bas sur le 1er rideau
    float curtain2_skew_ms;           // Décalage Haut vs Bas sur le 2e rideau
    float gapDivergence_percent;      // Variation de largeur de fente (Top vs Bottom)
};

// ============================================================================
// 3. SEUILS ET CALIBRATION DÉTECTION MODULE (AUTO-ID)
// ============================================================================

// Résolution de l'ADC (12-bit par défaut sur ESP32 = 0 à 4095)
#define ADC_RESOLUTION          4095.0f
#define VREF_VOLTAGE            3.3f    // Tension de référence logique

// Résistance de pull-up interne ou externe sur la carte pour l'ID (ex: 10 kΩ vers 3.3V)
#define ID_PULLUP_RESISTOR_OHM  1000.0f

// Tolérance admise sur les plages d'identification résistive
#define AUTO_ID_TOLERANCE_PCT   0.15f   // ±15 %

// Profil d'identification d'un module capteur
struct ModuleIDConfig {
    SensorFormat format;
    const char* name;
    float targetResistorOhm; // Valeur théorique de la résistance sur Pin 8
};

// Table des résistances étagées définies dans le CDC
const ModuleIDConfig MODULE_CONFIGS[] = {
    { FORMAT_24X36,  "24x36",               1000.0f },
    { FORMAT_6X6,    "6x6 (Moyen Format)",  3300.0f },
    { FORMAT_6X9,    "6x9 (Moyen Format)", 10000.0f },
    { FORMAT_645,    "645 (Moyen Format)", 33000.0f },
    { FORMAT_RESERVE,"Réserve / Futur",   100000.0f }
};

const uint8_t NUM_MODULE_CONFIGS = sizeof(MODULE_CONFIGS) / sizeof(MODULE_CONFIGS[0]);

// ============================================================================
// 4. PARAMÈTRES DU RÉSEAU ET DU SERVEUR WEB
// ============================================================================

#define WIFI_AP_SSID            "Banc_Obturateur_ESP32"
#define WIFI_AP_PASSWORD        "123456789"  // Minimum 8 caractères
#define WEBSERVER_PORT          80
#define WEBSOCKET_PORT          81

#endif // CONFIG_H