#include "capture_engine.h"

CaptureState CaptureEngine::_state = CAPTURE_IDLE;
SensorTiming CaptureEngine::_rawTimings[NUM_SENSORS];
uint32_t CaptureEngine::_captureStartTime = 0;

void IRAM_ATTR isrChannel0() { CaptureEngine::handleISR(0, digitalRead(PIN_SENSOR_TOP_LEFT)); }
void IRAM_ATTR isrChannel1() { CaptureEngine::handleISR(1, digitalRead(PIN_SENSOR_BOT_LEFT)); }
void IRAM_ATTR isrChannel2() { CaptureEngine::handleISR(2, digitalRead(PIN_SENSOR_CENTER)); }
void IRAM_ATTR isrChannel3() { CaptureEngine::handleISR(3, digitalRead(PIN_SENSOR_TOP_RIGHT)); }
void IRAM_ATTR isrChannel4() { CaptureEngine::handleISR(4, digitalRead(PIN_SENSOR_BOT_RIGHT)); }

typedef void (*ISRFunc)();
const ISRFunc ISR_TABLE[NUM_SENSORS] = { isrChannel0, isrChannel1, isrChannel2, isrChannel3, isrChannel4 };

CaptureEngine::CaptureEngine() {}

void CaptureEngine::begin() {
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        pinMode(SENSOR_PINS[i], INPUT);
    }
    arm();
}

void CaptureEngine::arm(float targetSec) {
    _targetSpeedSec = targetSec;
    _state = CAPTURE_ARMED;
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        _rawTimings[i].riseTime = 0;
        _rawTimings[i].fallTime = 0;
        _rawTimings[i].isValid = false;
        attachInterrupt(digitalPinToInterrupt(SENSOR_PINS[i]), ISR_TABLE[i], CHANGE);
    }
}

void CaptureEngine::stop() {
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        detachInterrupt(digitalPinToInterrupt(SENSOR_PINS[i]));
    }
    _state = CAPTURE_IDLE;
}

CaptureState CaptureEngine::getState() {
    if (_state == CAPTURE_BUSY && (micros() - _captureStartTime > 3000000)) {
        stop();
        _state = CAPTURE_COMPLETE;
    }
    return _state;
}

void IRAM_ATTR CaptureEngine::handleISR(uint8_t index, bool isHigh) {
    uint32_t now = micros();

    if (_state == CAPTURE_ARMED && isHigh) {
        _state = CAPTURE_BUSY;
        _captureStartTime = now;
    }

    if (_state == CAPTURE_BUSY) {
        if (isHigh && _rawTimings[index].riseTime == 0) {
            _rawTimings[index].riseTime = now;
        } else if (!isHigh && _rawTimings[index].riseTime != 0 && _rawTimings[index].fallTime == 0) {
            _rawTimings[index].fallTime = now;
            _rawTimings[index].isValid = true;

            bool allComplete = true;
            for (uint8_t i = 0; i < NUM_SENSORS; i++) {
                if (!_rawTimings[i].isValid) {
                    allComplete = false;
                    break;
                }
            }
            if (allComplete) _state = CAPTURE_COMPLETE;
        }
    }
}

void CaptureEngine::computeResults(ShutterMeasurement& meas, SensorFormat format) {
    meas.captureTimestamp = millis();
    meas.format = format;
    meas.nominalSpeed_s = _targetSpeedSec;

    bool allValid = true;
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        meas.sensors[i] = _rawTimings[i];
        if (!meas.sensors[i].isValid) allValid = false;
    }
    meas.allSensorsValid = allValid;

    // Calcul de survie si des capteurs manquent
    if (!allValid) {
        meas.type = SHUTTER_UNKNOWN;
        if (meas.sensors[INDEX_CENTER].isValid) {
            uint32_t durUs = meas.sensors[INDEX_CENTER].fallTime - meas.sensors[INDEX_CENTER].riseTime;
            meas.durationCenter_ms = durUs / 1000.0f;
            meas.calculatedSpeed_s = durUs / 1000000.0f;
            meas.deltaEV = log2f(meas.calculatedSpeed_s / _targetSpeedSec);
        }
        return; // Le reste est garanti à zéro grâce à `meas = {}`
    }

    // --- Mesure complète ---
    uint32_t centerDurUs = meas.sensors[INDEX_CENTER].fallTime - meas.sensors[INDEX_CENTER].riseTime;
    meas.durationCenter_ms = centerDurUs / 1000.0f;
    meas.calculatedSpeed_s = centerDurUs / 1000000.0f;
    meas.deltaEV = log2f(meas.calculatedSpeed_s / _targetSpeedSec);

    // Détection du type
    int32_t dtHorizUs = abs((int32_t)meas.sensors[INDEX_TOP_RIGHT].riseTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].riseTime);
    int32_t dtVertUs  = abs((int32_t)meas.sensors[INDEX_BOT_LEFT].riseTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].riseTime);

    if (dtHorizUs < 200 && dtVertUs < 200) {
        meas.type = SHUTTER_CENTRAL;
    } else if (dtHorizUs >= dtVertUs) {
        meas.type = SHUTTER_FOCAL_HORIZONTAL;
    } else {
        meas.type = SHUTTER_FOCAL_VERTICAL;
    }

    // Récupération des dimensions physiques pour le m/s
    float w_mm = 36.0f, h_mm = 24.0f;
    for (int i = 0; i < NUM_MODULE_CONFIGS; i++) {
        if (MODULE_CONFIGS[i].format == format) {
            w_mm = MODULE_CONFIGS[i].width_mm;
            h_mm = MODULE_CONFIGS[i].height_mm;
            break;
        }
    }

    // Analyse géométrique
    if (meas.type == SHUTTER_CENTRAL) {
        float expC = (float)(meas.sensors[INDEX_CENTER].fallTime - meas.sensors[INDEX_CENTER].riseTime);
        float expCorner = (float)(meas.sensors[INDEX_TOP_LEFT].fallTime - meas.sensors[INDEX_TOP_LEFT].riseTime);
        if (expC > 0) meas.gapDivergence_percent = ((expCorner - expC) / expC) * 100.0f;

    } else if (meas.type == SHUTTER_FOCAL_HORIZONTAL) {
        meas.curtain1_travelTime_ms = abs((int32_t)meas.sensors[INDEX_TOP_RIGHT].riseTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].riseTime) / 1000.0f;
        meas.curtain2_travelTime_ms = abs((int32_t)meas.sensors[INDEX_TOP_RIGHT].fallTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].fallTime) / 1000.0f;
        
        // 🛠️ Vitesse en m/s (mm / ms == m / s)
        if (meas.curtain1_travelTime_ms > 0) meas.curtain1_speed_mps = w_mm / meas.curtain1_travelTime_ms;
        if (meas.curtain2_travelTime_ms > 0) meas.curtain2_speed_mps = w_mm / meas.curtain2_travelTime_ms;

        meas.curtain1_skew_ms = ((int32_t)meas.sensors[INDEX_BOT_LEFT].riseTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].riseTime) / 1000.0f;
        meas.curtain2_skew_ms = ((int32_t)meas.sensors[INDEX_BOT_LEFT].fallTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].fallTime) / 1000.0f;

        float expTL = (float)(meas.sensors[INDEX_TOP_LEFT].fallTime - meas.sensors[INDEX_TOP_LEFT].riseTime);
        float expBL = (float)(meas.sensors[INDEX_BOT_LEFT].fallTime - meas.sensors[INDEX_BOT_LEFT].riseTime);
        if (expTL > 0) meas.gapDivergence_percent = ((expBL - expTL) / expTL) * 100.0f;

    } else if (meas.type == SHUTTER_FOCAL_VERTICAL) {
        // Temps de balayage avec abs() + cast int32_t (gère haut->bas et bas->haut)
        meas.curtain1_travelTime_ms = abs((int32_t)meas.sensors[INDEX_BOT_LEFT].riseTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].riseTime) / 1000.0f;
        meas.curtain2_travelTime_ms = abs((int32_t)meas.sensors[INDEX_BOT_LEFT].fallTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].fallTime) / 1000.0f;
        
        // Vitesse en m/s (Vertical = hauteur)
        if (meas.curtain1_travelTime_ms > 0) meas.curtain1_speed_mps = h_mm / meas.curtain1_travelTime_ms;
        if (meas.curtain2_travelTime_ms > 0) meas.curtain2_speed_mps = h_mm / meas.curtain2_travelTime_ms;

        meas.curtain1_skew_ms = ((int32_t)meas.sensors[INDEX_TOP_RIGHT].riseTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].riseTime) / 1000.0f;
        meas.curtain2_skew_ms = ((int32_t)meas.sensors[INDEX_TOP_RIGHT].fallTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].fallTime) / 1000.0f;

        float expTL = (float)(meas.sensors[INDEX_TOP_LEFT].fallTime - meas.sensors[INDEX_TOP_LEFT].riseTime);
        float expTR = (float)(meas.sensors[INDEX_TOP_RIGHT].fallTime - meas.sensors[INDEX_TOP_RIGHT].riseTime);
        if (expTL > 0) meas.gapDivergence_percent = ((expTR - expTL) / expTL) * 100.0f;
    }
}