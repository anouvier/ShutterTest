#include <Arduino.h>
#include "config.h"
#include "sensor_id.h"
#include "capture_engine.h"
#include "web_server.h"

SensorID sensorId;
CaptureEngine captureEngine;
WebServerManager webServer(sensorId, captureEngine);

SensorFormat lastFormat = FORMAT_UNKNOWN;
CaptureState lastState = CAPTURE_IDLE;
unsigned long lastStatusCheck = 0;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== ShutterTest - Banc de Test d'Obturateur ===");

    // Initialisation des modules
    sensorId.begin();
    captureEngine.begin();
    webServer.begin();

    lastFormat = sensorId.readFormat();
    Serial.printf("[Auto-ID] Module au démarrage : %s\n", sensorId.getFormatName(lastFormat));
}

void loop() {
    webServer.update();

    // 1. Détection périodique des changements de module (toutes les 500ms)
    if (millis() - lastStatusCheck > 500) {
        lastStatusCheck = millis();
        SensorFormat currentFormat = sensorId.readFormat();
        CaptureState currentState = captureEngine.getState();

        if (currentFormat != lastFormat || currentState != lastState) {
            lastFormat = currentFormat;
            lastState = currentState;
            webServer.broadcastStatus(currentFormat, currentState);
        }
    }

    // 2. Vérification si un déclenchement vient d'être capturé
    if (captureEngine.getState() == CAPTURE_COMPLETE) {
        Serial.println("[Capture] Déclenchement détecté. Calcul des résultats...");
        
        ShutterMeasurement meas = captureEngine.getMeasurement(lastFormat);
        
        // Log console pour débug
        Serial.printf(" Type : %d | Vitesse : 1/%.1fs | Écart EV : %.2f EV\n", 
                      meas.type, 1.0f / meas.calculatedSpeed_s, meas.deltaEV);
        Serial.printf(" Skew Rideau 1 : %.3f ms | Skew Rideau 2 : %.3f ms | Divergence : %.1f%%\n",
                      meas.curtain1_skew_ms, meas.curtain2_skew_ms, meas.gapDivergence_percent);

        // Envoi immédiat vers l'interface Web
        webServer.broadcastMeasurement(meas);

        // Réarmement automatique du moteur d'acquisition pour le prochain tir
        captureEngine.rearmSameTarget();
    }
}