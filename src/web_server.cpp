#include "web_server.h"

WebServerManager::WebServerManager(SensorID &sensorId, CaptureEngine &captureEngine)
    : _server(WEBSERVER_PORT), _ws("/ws"), _sensorId(sensorId), _captureEngine(captureEngine) {}

void WebServerManager::begin() {
    // Initialisation du système de fichiers LittleFS pour l'interface Web
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

void WebServerManager::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[WebSocket] Client #%u connecté depuis %s\n", client->id(), client->remoteIP().toString().c_str());
        // Envoie l'état actuel immédiatement à la connexion
        client->text(serializeStatusJSON(_sensorId.readFormat(), _captureEngine.getState()));
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[WebSocket] Client #%u déconnecté\n", client->id());
    } else if (type == WS_EVT_DATA) {
        // Traitement des commandes reçues de la tablette (ex: Lancer un simu, réarmer)
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->opcode == WS_TEXT) {
            data[len] = 0; // Null-terminate string
            DynamicJsonDocument doc(512);
            DeserializationError err = deserializeJson(doc, (char*)data);
            if (!err) {
                const char* command = doc["cmd"];
                if (strcmp(command, "arm") == 0) {
                    _captureEngine.arm();
                } else if (strcmp(command, "simulate") == 0) {
                    float speed = doc["speed"] | 0.002f; // 1/500s par défaut
                    float travel = doc["travel"] | 2.5f;  // 2.5ms par défaut
                    _captureEngine.simulateShot(speed, travel);
                }
            }
        }
    }
}

void WebServerManager::setupRoutes() {
    // Route racine : sert index.html depuis LittleFS
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
    
    // Type d'obturateur
    switch(meas.type) {
        case SHUTTER_CENTRAL:          doc["shutterType"] = "Central"; break;
        case SHUTTER_FOCAL_HORIZONTAL: doc["shutterType"] = "Focal Horiz."; break;
        case SHUTTER_FOCAL_VERTICAL:   doc["shutterType"] = "Focal Vert."; break;
        default:                        doc["shutterType"] = "Inconnu"; break;
    }

    // Vitesses et écarts
    doc["durationCenterMs"] = meas.durationCenter_ms;
    doc["calculatedSpeedS"] = meas.calculatedSpeed_s;
    doc["nominalSpeedS"]    = meas.nominalSpeed_s;
    doc["deltaEV"]           = meas.deltaEV;

    // Métriques géométriques rideaux
    doc["curtain1TravelMs"] = meas.curtain1_travelTime_ms;
    doc["curtain2TravelMs"] = meas.curtain2_travelTime_ms;
    doc["curtain1SkewMs"]   = meas.curtain1_skew_ms;
    doc["curtain2SkewMs"]   = meas.curtain2_skew_ms;
    doc["gapDivergencePct"] = meas.gapDivergence_percent;

    // Timestamps bruts des 5 capteurs (pour tracé des chronogrammes dans le JS)
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