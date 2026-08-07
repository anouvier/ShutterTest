#include "capture_engine.h"

// Initialisation des membres statiques
CaptureState CaptureEngine::_state = CAPTURE_IDLE;
SensorTiming CaptureEngine::_rawTimings[NUM_SENSORS];
uint32_t CaptureEngine::_captureStartTime = 0;

// Routines d'interruption génériques stockées en RAM (IRAM_ATTR) pour zéro latence
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

void CaptureEngine::arm() {
    _state = CAPTURE_ARMED;
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        _rawTimings[i].riseTime = 0;
        _rawTimings[i].fallTime = 0;
        _rawTimings[i].isValid = false;
        
        // Attache les interruptions sur CHANGEMENT d'état (Front montant + descendant)
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
    // Timeout de sécurité : Si une capture a commencé mais ne s'est pas terminée après 3 secondes, on valide
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

            // Si les 5 capteurs ont capturé un front descendant, le tir est terminé
            bool allComplete = true;
            for (uint8_t i = 0; i < NUM_SENSORS; i++) {
                if (!_rawTimings[i].isValid) {
                    allComplete = false;
                    break;
                }
            }
            if (allComplete) {
                _state = CAPTURE_COMPLETE;
            }
        }
    }
}

float CaptureEngine::findClosestNominalSpeed(float measuredSpeed_s) {
    // Vitesses standards en secondes : 1/1000, 1/500, 1/250, 1/125, 1/60, 1/30, 1/15, 1/8, 1/4, 1/2, 1s
    const float standardSpeeds[] = {
        1.0f/1000.0f, 1.0f/500.0f, 1.0f/250.0f, 1.0f/125.0f, 
        1.0f/60.0f,   1.0f/30.0f,  1.0f/15.0f,  1.0f/8.0f, 
        1.0f/4.0f,    1.0f/2.0f,   1.0f
    };
    
    float closest = standardSpeeds[0];
    float minDiff = fabs(measuredSpeed_s - closest);

    for (size_t i = 1; i < sizeof(standardSpeeds)/sizeof(standardSpeeds[0]); i++) {
        float diff = fabs(measuredSpeed_s - standardSpeeds[i]);
        if (diff < minDiff) {
            minDiff = diff;
            closest = standardSpeeds[i];
        }
    }
    return closest;
}

void CaptureEngine::computeResults(ShutterMeasurement &meas, float targetSpeed_s) {
    meas.captureTimestamp = millis();

    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        meas.sensors[i] = _rawTimings[i];
    }

    // 1. Calcul de base au centre (Capteur Index 2)
    if (meas.sensors[INDEX_CENTER].isValid) {
        uint32_t durationUs = meas.sensors[INDEX_CENTER].fallTime - meas.sensors[INDEX_CENTER].riseTime;
        meas.durationCenter_ms = durationUs / 1000.0f;
        meas.calculatedSpeed_s = durationUs / 1000000.0f;
    } else {
        meas.durationCenter_ms = 0.0f;
        meas.calculatedSpeed_s = 0.0f;
    }

    // Calcul de la vitesse consigne et de l'écart en EV au centre
    meas.nominalSpeed_s = (targetSpeed_s > 0) ? targetSpeed_s : findClosestNominalSpeed(meas.calculatedSpeed_s);
    if (meas.calculatedSpeed_s > 0 && meas.nominalSpeed_s > 0) {
        meas.deltaEV = log2f(meas.calculatedSpeed_s / meas.nominalSpeed_s);
    } else {
        meas.deltaEV = 0.0f;
    }

    // Vérification de la validité de l'ensemble des capteurs
    bool allValid = true;
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        if (!meas.sensors[i].isValid) { allValid = false; break; }
    }

    if (!allValid) {
        meas.type = SHUTTER_UNKNOWN;
        return;
    }

    // 2. Détection automatique du type d'obturateur
    // On calcule les deltas de temps d'apparition de la lumière (fronts montants)
    int32_t dtHorizUs = abs((int32_t)meas.sensors[INDEX_TOP_RIGHT].riseTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].riseTime);
    int32_t dtVertUs  = abs((int32_t)meas.sensors[INDEX_BOT_LEFT].riseTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].riseTime);

    // Si l'écart de déclenchement entre les bords est < 200 µs, c'est un obturateur central (pales)
    if (dtHorizUs < 200 && dtVertUs < 200) {
        meas.type = SHUTTER_CENTRAL;
    } else if (dtHorizUs >= dtVertUs) {
        meas.type = SHUTTER_FOCAL_HORIZONTAL;
    } else {
        meas.type = SHUTTER_FOCAL_VERTICAL;
    }

    // 3. Calculs spécifiques selon le type d'obturateur

    // A. OBTURATEUR CENTRAL
    if (meas.type == SHUTTER_CENTRAL) {
        meas.curtain1_travelTime_ms = 0.0f;
        meas.curtain2_travelTime_ms = 0.0f;
        meas.curtain1_skew_ms = 0.0f;
        meas.curtain2_skew_ms = 0.0f;

        // Pour un obturateur central, la divergence mesure l'homogénéité d'ouverture du diaphragme/pales
        // (Comparaison temps d'exposition Bord vs Centre)
        float expCenter = (float)(meas.sensors[INDEX_CENTER].fallTime - meas.sensors[INDEX_CENTER].riseTime);
        float expCorner = (float)(meas.sensors[INDEX_TOP_LEFT].fallTime - meas.sensors[INDEX_TOP_LEFT].riseTime);
        if (expCenter > 0) {
            meas.gapDivergence_percent = ((expCorner - expCenter) / expCenter) * 100.0f;
        } else {
            meas.gapDivergence_percent = 0.0f;
        }
    }

    // B. OBTURATEUR À RIDEAUX HORIZONTAUX (Défilement de Gauche à Droite)
    else if (meas.type == SHUTTER_FOCAL_HORIZONTAL) {
        // Temps de trajet du Rideau 1 (Ouverture) : Top-Left -> Top-Right
        uint32_t c1_travel = meas.sensors[INDEX_TOP_RIGHT].riseTime - meas.sensors[INDEX_TOP_LEFT].riseTime;
        meas.curtain1_travelTime_ms = c1_travel / 1000.0f;

        // Temps de trajet du Rideau 2 (Fermeture) : Top-Left -> Top-Right
        uint32_t c2_travel = meas.sensors[INDEX_TOP_RIGHT].fallTime - meas.sensors[INDEX_TOP_LEFT].fallTime;
        meas.curtain2_travelTime_ms = c2_travel / 1000.0f;

        // Parallélisme Rideau 1 (Décalage Haut vs Bas au départ)
        int32_t c1_skewUs = (int32_t)meas.sensors[INDEX_BOT_LEFT].riseTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].riseTime;
        meas.curtain1_skew_ms = c1_skewUs / 1000.0f;

        // Parallélisme Rideau 2 (Décalage Haut vs Bas à la fermeture)
        int32_t c2_skewUs = (int32_t)meas.sensors[INDEX_BOT_LEFT].fallTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].fallTime;
        meas.curtain2_skew_ms = c2_skewUs / 1000.0f;

        // Divergence de la fente (Temps d'exposition Top-Left vs Bot-Left)
        float expTopLeft = (float)(meas.sensors[INDEX_TOP_LEFT].fallTime - meas.sensors[INDEX_TOP_LEFT].riseTime);
        float expBotLeft = (float)(meas.sensors[INDEX_BOT_LEFT].fallTime - meas.sensors[INDEX_BOT_LEFT].riseTime);
        if (expTopLeft > 0) {
            meas.gapDivergence_percent = ((expBotLeft - expTopLeft) / expTopLeft) * 100.0f;
        } else {
            meas.gapDivergence_percent = 0.0f;
        }
    }

    // C. OBTURATEUR À RIDEAUX VERTICAUX (Défilement du Haut vers le Bas)
    else if (meas.type == SHUTTER_FOCAL_VERTICAL) {
        // Temps de trajet du Rideau 1 (Ouverture) : Top-Left -> Bot-Left
        uint32_t c1_travel = meas.sensors[INDEX_BOT_LEFT].riseTime - meas.sensors[INDEX_TOP_LEFT].riseTime;
        meas.curtain1_travelTime_ms = c1_travel / 1000.0f;

        // Temps de trajet du Rideau 2 (Fermeture) : Top-Left -> Bot-Left
        uint32_t c2_travel = meas.sensors[INDEX_BOT_LEFT].fallTime - meas.sensors[INDEX_TOP_LEFT].fallTime;
        meas.curtain2_travelTime_ms = c2_travel / 1000.0f;

        // Parallélisme Rideau 1 (Décalage Gauche vs Droite en Haut)
        int32_t c1_skewUs = (int32_t)meas.sensors[INDEX_TOP_RIGHT].riseTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].riseTime;
        meas.curtain1_skew_ms = c1_skewUs / 1000.0f;

        // Parallélisme Rideau 2 (Décalage Gauche vs Droite en Haut)
        int32_t c2_skewUs = (int32_t)meas.sensors[INDEX_TOP_RIGHT].fallTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].fallTime;
        meas.curtain2_skew_ms = c2_skewUs / 1000.0f;

        // Divergence de la fente (Temps d'exposition Top-Left vs Top-Right)
        float expTopLeft  = (float)(meas.sensors[INDEX_TOP_LEFT].fallTime - meas.sensors[INDEX_TOP_LEFT].riseTime);
        float expTopRight = (float)(meas.sensors[INDEX_TOP_RIGHT].fallTime - meas.sensors[INDEX_TOP_RIGHT].riseTime);
        if (expTopLeft > 0) {
            meas.gapDivergence_percent = ((expTopRight - expTopLeft) / expTopLeft) * 100.0f;
        } else {
            meas.gapDivergence_percent = 0.0f;
        }
    }
}

ShutterMeasurement CaptureEngine::getMeasurement() {
    ShutterMeasurement meas;
    computeResults(meas);
    return meas;
}

// SIMULATION : Génère un faux tir d'obturateur à rideaux horizontaux
void CaptureEngine::simulateShot(float speed_s, float curtainTravel_ms) {
    stop();
    uint32_t now = micros();
    uint32_t speedUs = (uint32_t)(speed_s * 1000000.0f);
    uint32_t travelUs = (uint32_t)(curtainTravel_ms * 1000.0f);

    // Décalage temporel des capteurs imitant le passage d'un rideau de gauche à droite
    // Gauche (0ms) -> Centre (travelUs/2) -> Droite (travelUs)
    uint32_t offsetsUs[NUM_SENSORS] = {
        0,                  // Top Left
        0,                  // Bot Left
        travelUs / 2,       // Center
        travelUs,           // Top Right
        travelUs            // Bot Right
    };

    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        _rawTimings[i].riseTime = now + offsetsUs[i];
        _rawTimings[i].fallTime = _rawTimings[i].riseTime + speedUs;
        _rawTimings[i].isValid = true;
    }

    _state = CAPTURE_COMPLETE;
}