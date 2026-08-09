#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>

#include "config.h"
#include "sensor_id.h"
#include "capture_engine.h"

class WebServerManager {
public:
    WebServerManager(SensorID &sensorId, CaptureEngine &captureEngine);
    void begin();
    void update(); // À appeler dans le loop() pour traiter les événements réseau

    // Diffusion de données JSON vers tous les clients Web connectés
    void broadcastMeasurement(const ShutterMeasurement &meas);
    void broadcastStatus(SensorFormat currentFormat, CaptureState state);

private:
    AsyncWebServer _server;
    AsyncWebSocket _ws;
    
    SensorID &_sensorId;
    CaptureEngine &_captureEngine;

    void setupWiFi();
    void setupRoutes();
    void setupWebSocket();
    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
    
    // Génération des payloads JSON
    String serializeMeasurementJSON(const ShutterMeasurement &meas);
    String serializeStatusJSON(SensorFormat currentFormat, CaptureState state);
    
    void setupListRoutes();
    void handleAddListValue(AsyncWebServerRequest* request, JsonVariant& json,
                             const char* path, const char* defaultJson);
};

#endif // WEB_SERVER_H