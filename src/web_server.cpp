#include "web_server.h"

WebServerManager::WebServerManager(SensorID &sensorId, CaptureEngine &captureEngine)
    : _server(WEBSERVER_PORT), _ws("/ws"), _sensorId(sensorId), _captureEngine(captureEngine) {}

void WebServerManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[LittleFS] Erreur d'initialisation !");
    } else {
        Serial.println("[LittleFS] Système de fichiers monté avec succès.");
    }

    setupWiFi();
    setupWebSocket();
    setupRoutes();

    _server.begin();
    Serial.println("[HTTP] Serveur Web démarré sur le port 80.");
}

void WebServerManager::setupWiFi() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    Serial.print("[Wi-Fi] Point d'accès démarré. IP : ");
    Serial.println(WiFi.softAPIP());
}

void WebServerManager::setupWebSocket() {
    _ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        this->onWsEvent(server, client, type, arg, data, len);
    });
    _server.addHandler(&_ws);
}

// 🛠️ FIX : Rattachée à la classe, inclut CONNECT/DISCONNECT et protège contre nullptr
void WebServerManager::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[WebSocket] Client connecté : %u\n", client->id());
        client->text(serializeStatusJSON(_sensorId.readFormat(), _captureEngine.getState()));
    } 
    else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[WebSocket] Client déconnecté : %u\n", client->id());
    } 
    else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len) {
            char jsonBuf[256];
            size_t copyLen = min(len, sizeof(jsonBuf) - 1);
            memcpy(jsonBuf, data, copyLen);
            jsonBuf[copyLen] = '\0';

            StaticJsonDocument<256> doc;
            DeserializationError err = deserializeJson(doc, jsonBuf);
            if (err) return;

            const char* command = doc["cmd"] | "";

            if (strcmp(command, "arm") == 0) {
                float targetSec = doc["targetSec"] | 0.002f; 
                if (!doc.containsKey("targetSec") && doc.containsKey("targetMs")) {
                    targetSec = (doc["targetMs"] | 2.0f) / 1000.0f;
                }
                CaptureState state = _captureEngine.getState();
                if (state != CAPTURE_BUSY) {
                    _captureEngine.arm(targetSec);
                    Serial.printf("[CaptureEngine] Moteur armé avec vitesse cible : %f s\n", targetSec);
                } else {
                    Serial.println("[CaptureEngine] Ordre d'armement ignoré : capture en cours !");
                }
            }
        }
    }
}

void WebServerManager::setupRoutes() {
    _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    _server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Page non trouvée");
    });
}

void WebServerManager::update() {
    _ws.cleanupClients();
}

String WebServerManager::serializeStatusJSON(SensorFormat currentFormat, CaptureState state) {
    DynamicJsonDocument doc(256);
    doc["type"] = "status";
    doc["format"] = _sensorId.getFormatName(currentFormat);
    doc["formatId"] = (int)currentFormat;
    doc["state"] = (int)state;
    doc["resistorOhm"] = _sensorId.getLastMeasuredResistor();

    String output;
    serializeJson(doc, output);
    return output;
}

String WebServerManager::serializeMeasurementJSON(const ShutterMeasurement &meas) {
    DynamicJsonDocument doc(1024);
    
    doc["type"] = "measurement";
    doc["timestamp"] = meas.captureTimestamp;
    
    doc["partial"] = !meas.allSensorsValid; 

    switch(meas.type) {
        case SHUTTER_CENTRAL:          doc["shutterType"] = "Central"; break;
        case SHUTTER_FOCAL_HORIZONTAL: doc["shutterType"] = "Focal Horiz."; break;
        case SHUTTER_FOCAL_VERTICAL:   doc["shutterType"] = "Focal Vert."; break;
        default:                       doc["shutterType"] = "Inconnu"; break;
    }

    doc["durationCenterMs"] = meas.durationCenter_ms;
    doc["calculatedSpeedS"] = meas.calculatedSpeed_s;
    doc["nominalSpeedS"]    = meas.nominalSpeed_s;
    doc["deltaEV"]          = meas.deltaEV;

    doc["curtain1SkewMs"]   = meas.curtain1_skew_ms;
    doc["curtain2SkewMs"]   = meas.curtain2_skew_ms;
    doc["speedR1Mps"]       = meas.curtain1_speed_mps;
    doc["speedR2Mps"]       = meas.curtain2_speed_mps;
    doc["gapDivergencePct"] = meas.gapDivergence_percent;

    JsonArray sensorsArr = doc.createNestedArray("sensors");
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        JsonObject s = sensorsArr.createNestedObject();
        s["rise"] = meas.sensors[i].riseTime;
        s["fall"] = meas.sensors[i].fallTime;
        s["valid"] = meas.sensors[i].isValid;
    }

    String output;
    serializeJson(doc, output);
    return output;
}

void WebServerManager::broadcastMeasurement(const ShutterMeasurement &meas) {
    String jsonStr = serializeMeasurementJSON(meas);
    _ws.textAll(jsonStr);
}

void WebServerManager::broadcastStatus(SensorFormat currentFormat, CaptureState state) {
    String jsonStr = serializeStatusJSON(currentFormat, state);
    _ws.textAll(jsonStr);
}