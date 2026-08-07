#ifndef CAPTURE_ENGINE_H
#define CAPTURE_ENGINE_H

#include "config.h"

// États possibles de la machine à états de capture
enum CaptureState {
    CAPTURE_IDLE,        // En attente d'un déclenchement
    CAPTURE_ARMED,       // Prêt à capturer un tir
    CAPTURE_BUSY,        // Capture en cours (lumière détectée sur au moins 1 capteur)
    CAPTURE_COMPLETE     // Capture terminée, données prêtes à être lues/calculées
};

class CaptureEngine {
public:
    CaptureEngine();
    void begin();

    // Gestion du cycle de mesure
    void arm();
    void stop();
    CaptureState getState();

    // Récupération et traitement des résultats
    ShutterMeasurement getMeasurement();
    void computeResults(ShutterMeasurement &meas, float targetSpeed_s = 0.002f); // Défaut 1/500s

    // Mode simulation (Génère une fausse mesure pour tester sans hardware)
    void simulateShot(float speed_s = 0.002f, float curtainTravel_ms = 2.5f);

    // Fonction de rappel appelée par les ISR (Interrupt Service Routines)
    static void handleISR(uint8_t sensorIndex, bool isRising);

private:
    static CaptureState _state;
    static SensorTiming _rawTimings[NUM_SENSORS];
    static uint32_t _captureStartTime;
    
    // Détermination de la vitesse théorique normalisée la plus proche
    float findClosestNominalSpeed(float measuredSpeed_s);
};

#endif // CAPTURE_ENGINE_H