#include <unity.h>
#include "ArduinoMock.h"
#include "capture_engine.h"

// Fonction exécutée AVANT chaque test
void setUp(void) {
    // Initialisation si besoin
}

// Fonction exécutée APRÈS chaque test
void tearDown(void) {
    // Nettoyage si besoin
}

void test_horizontal_shutter_reverse_direction(void) {
    ShutterMeasurement meas;
    meas.type = SHUTTER_FOCAL_HORIZONTAL;

    // Simulation rideau Droite -> Gauche (TOP_RIGHT éclairé en premier)
    meas.sensors[INDEX_TOP_RIGHT].riseTime = 1000;
    meas.sensors[INDEX_TOP_LEFT].riseTime  = 3000;
    meas.sensors[INDEX_BOT_LEFT].riseTime  = 3200; // Skew de +0.2ms

    float w_mm = 36.0f;

    // Calculs
    meas.curtain1_travelTime_ms = abs((int32_t)meas.sensors[INDEX_TOP_RIGHT].riseTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].riseTime) / 1000.0f;
    meas.curtain1_speed_mps = w_mm / meas.curtain1_travelTime_ms;
    meas.curtain1_skew_ms = ((int32_t)meas.sensors[INDEX_BOT_LEFT].riseTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].riseTime) / 1000.0f;

    // Assertions Unity
    TEST_ASSERT_EQUAL_FLOAT(2.0f, meas.curtain1_travelTime_ms);
    TEST_ASSERT_EQUAL_FLOAT(18.0f, meas.curtain1_speed_mps);
    TEST_ASSERT_EQUAL_FLOAT(0.2f, meas.curtain1_skew_ms);
}

void test_vertical_shutter_skew(void) {
    ShutterMeasurement meas;
    meas.type = SHUTTER_FOCAL_VERTICAL;

    // Test d'un rideau vertical avec skew négatif
    meas.sensors[INDEX_TOP_LEFT].riseTime  = 2000;
    meas.sensors[INDEX_TOP_RIGHT].riseTime = 1800; // TOP_RIGHT touché en premier -> skew négatif

    meas.curtain1_skew_ms = ((int32_t)meas.sensors[INDEX_TOP_RIGHT].riseTime - (int32_t)meas.sensors[INDEX_TOP_LEFT].riseTime) / 1000.0f;

    TEST_ASSERT_EQUAL_FLOAT(-0.2f, meas.curtain1_skew_ms);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_horizontal_shutter_reverse_direction);
    RUN_TEST(test_vertical_shutter_skew);
    return UNITY_END();
}