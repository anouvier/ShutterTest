#include "sensor_id.h"

SensorID::SensorID() : _lastResistorOhm(0.0f) {}

void SensorID::begin() {
    // Sur Nano ESP32 / ESP32-S3, résolution de l'ADC configurée en 12 bits
    analogReadResolution(12);
    pinMode(PIN_SENSOR_ID, INPUT);
}

float SensorID::readAveragedADC(uint8_t samples) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < samples; i++) {
        sum += analogRead(PIN_SENSOR_ID);
        delayMicroseconds(100); // Petit délai pour stabiliser la lecture
    }
    return (float)sum / (float)samples;
}

float SensorID::getLastMeasuredResistor() {
    return _lastResistorOhm;
}

SensorFormat SensorID::readFormat() {
    float rawADC = readAveragedADC(32); // Moyenne sur 32 échantillons pour filtrer le bruit

    // Évite la division par zéro si la ligne est en court-circuit strict au 3.3V
    if (rawADC >= (ADC_RESOLUTION - 5.0f)) {
        _lastResistorOhm = 999999.0f; // Circuit ouvert / Non connecté
        return FORMAT_UNKNOWN;
    }

    // Calcul de R_module via la formule du pont diviseur
    _lastResistorOhm = ID_PULLUP_RESISTOR_OHM * (rawADC / (ADC_RESOLUTION - rawADC));

    // Comparaison avec la table des formats
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