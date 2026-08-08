#ifndef CAPTURE_ENGINE_H
#define CAPTURE_ENGINE_H

#include "config.h"

enum CaptureState {
    CAPTURE_IDLE,
    CAPTURE_ARMED,
    CAPTURE_BUSY,
    CAPTURE_COMPLETE
};

class CaptureEngine {
public:
    CaptureEngine();
    void begin();

    void arm(float targetSec = 0.002f);
    void rearmSameTarget() { arm(_targetSpeedSec); }
    void stop();
    CaptureState getState();

    static void handleISR(uint8_t sensorIndex, bool isRising); 
    
    ShutterMeasurement getMeasurement(SensorFormat currentFormat) {
        ShutterMeasurement meas = {}; 
        computeResults(meas, currentFormat);
        return meas;
    }

private:
    float _targetSpeedSec = 0.002f;
    static CaptureState _state;
    static SensorTiming _rawTimings[NUM_SENSORS];
    static uint32_t _captureStartTime;

    void computeResults(ShutterMeasurement& meas, SensorFormat format);
    void simulateShot(float speed_s = 0.002f, float curtainTravel_ms = 2.5f);
};

#endif // CAPTURE_ENGINE_H