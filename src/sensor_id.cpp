#include "sensor_id.h"

SensorID::SensorID() : _lastResistorOhm(0.0f) {}

void SensorID::begin() {
    // 1. Résolution ADC standard sur 12 bits (0 - 4095)
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_SENSOR_ID, ADC_11db);
    
    // 2. Entrée analogique classique (le Pull-Up externe gère la tension)
    pinMode(PIN_SENSOR_ID, INPUT);
}

float SensorID::readAveragedADC(uint8_t samples) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < samples; i++) {
        sum += analogReadMilliVolts(PIN_SENSOR_ID);
        delayMicroseconds(200);
    }
    return (float)sum / (float)samples;
}

float SensorID::getLastMeasuredResistor() {
    return _lastResistorOhm;
}

SensorFormat SensorID::readFormat() {
    float measuredmV = readAveragedADC(32);
    float vccMilliVolts = 3300.0f;

    // 1. Module non connecté (A0 est tirée au 3.3V par la résistance externe)
    if (measuredmV >= 3150.0f) {
        _lastResistorOhm = 999999.0f;
        return FORMAT_UNKNOWN;
    }

    // 2. Sécurité : court-circuit direct au GND (< 10 mV)
    if (measuredmV <= 10.0f) {
        _lastResistorOhm = 0.0f;
        return FORMAT_UNKNOWN;
    }

    // 3. Calcul de la résistance du module (Pont diviseur avec Pull-Up externe de 10k/47k)
    // V_out = VCC * R_module / (R_pullup + R_module)
    // R_module = (R_pullup * V_out) / (VCC - V_out)
    _lastResistorOhm = (ID_PULLUP_RESISTOR_OHM * measuredmV) / (vccMilliVolts - measuredmV);

    // 4. Identification du format
    for (uint8_t i = 0; i < NUM_MODULE_CONFIGS; i++) {
        float target = MODULE_CONFIGS[i].targetResistorOhm;
        float minAllowed = target * (1.0f - AUTO_ID_TOLERANCE_PCT);
        float maxAllowed = target * (1.0f + AUTO_ID_TOLERANCE_PCT);

        if (_lastResistorOhm >= minAllowed && _lastResistorOhm <= maxAllowed) {
            return MODULE_CONFIGS[i].format;
        }
    }

    return FORMAT_UNKNOWN;
}

const char* SensorID::getFormatName(SensorFormat format) {
    for (uint8_t i = 0; i < NUM_MODULE_CONFIGS; i++) {
        if (MODULE_CONFIGS[i].format == format) {
            return MODULE_CONFIGS[i].name;
        }
    }
    return "Module non reconnu / Déconnecté";
}