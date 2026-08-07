#ifndef SENSOR_ID_H
#define SENSOR_ID_H

#include "config.h"

class SensorID {
public:
    SensorID();
    void begin();
    
    // Lit l'ADC, calcule la résistance et retourne le format
    SensorFormat readFormat();
    
    // Retourne le nom lisible du format actuel
    const char* getFormatName(SensorFormat format);
    
    // Utile pour le débug/calibration : retourne la résistance mesurée en Ohms
    float getLastMeasuredResistor();

private:
    float _lastResistorOhm;
    float readAveragedADC(uint8_t samples = 16);
};

#endif // SENSOR_ID_H