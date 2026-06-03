#include "zwin_vm.h"
#include <Arduino.h>

// Dummy stub for LLM client access if compiled as a standalone repo
struct DummyLLM {
    bool isConfigured() { return false; }
    String chat(const String&) { return "Gemini mock response (standalone)"; }
} dummyLLM;

// Helper to write analog PWM to motor pins
static void setMotorPWM(int pin, int duty) {
    if (pin < 0) return;
    pinMode(pin, OUTPUT);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(pin, 5000, 8);
    ledcWrite(pin, duty);
#else
    static uint8_t nextCh = 0;
    uint8_t ch = nextCh;
    if (++nextCh > 15) nextCh = 0;
    ledcSetup(ch, 5000, 8);
    ledcAttachPin(pin, ch);
    ledcWrite(ch, duty);
#endif
}

// ---------------------------------------------------------------------------
// 1. status_led Namespace
// ---------------------------------------------------------------------------

static String zwin_led_write(const String& params) {
    int state = params.indexOf("state=1") >= 0 ? 1 : 0;
    pinMode(2, OUTPUT);
    digitalWrite(2, state ? HIGH : LOW);
    Serial.printf("[ZWIN SDK] LED set to %s\n", state ? "ON" : "OFF");
    return "1";
}

static String zwin_led_blink(const String& params) {
    int count = params.indexOf("count=") >= 0 ? params.substring(params.indexOf("count=") + 6).toInt() : 3;
    if (count <= 0) count = 3;
    
    int delay_ms = params.indexOf("delay_ms=") >= 0 ? params.substring(params.indexOf("delay_ms=") + 9).toInt() : 200;
    if (delay_ms <= 0) delay_ms = 200;

    pinMode(2, OUTPUT);
    for (int i = 0; i < count; ++i) {
        digitalWrite(2, HIGH);
        delay(delay_ms);
        digitalWrite(2, LOW);
        delay(delay_ms);
    }
    Serial.printf("[ZWIN SDK] LED blinked %d times\n", count);
    return "1";
}

// ---------------------------------------------------------------------------
// 2. motors Namespace (MX1508 H-bridge motor driver configuration)
// ---------------------------------------------------------------------------

static String zwin_motors_drive(const String& params) {
    int leftVal = 0;
    int rightVal = 0;

    int leftIdx = params.indexOf("left=");
    if (leftIdx >= 0) {
        int commaIdx = params.indexOf(',', leftIdx);
        String sub = (commaIdx < 0) ? params.substring(leftIdx + 5) : params.substring(leftIdx + 5, commaIdx);
        leftVal = sub.toInt();
    }

    int rightIdx = params.indexOf("right=");
    if (rightIdx >= 0) {
        int commaIdx = params.indexOf(',', rightIdx);
        String sub = (commaIdx < 0) ? params.substring(rightIdx + 6) : params.substring(rightIdx + 6, commaIdx);
        rightVal = sub.toInt();
    }

    if (leftVal > 255) leftVal = 255;
    if (leftVal < -255) leftVal = -255;
    if (rightVal > 255) rightVal = 255;
    if (rightVal < -255) rightVal = -255;

    // Left Motor (GPIO 26, 27)
    if (leftVal >= 0) {
        setMotorPWM(26, leftVal);
        setMotorPWM(27, 0);
    } else {
        setMotorPWM(26, 0);
        setMotorPWM(27, -leftVal);
    }

    // Right Motor (GPIO 25, 33)
    if (rightVal >= 0) {
        setMotorPWM(25, rightVal);
        setMotorPWM(33, 0);
    } else {
        setMotorPWM(25, 0);
        setMotorPWM(33, -rightVal);
    }

    Serial.printf("[ZWIN SDK] Motors: left=%d, right=%d\n", leftVal, rightVal);
    return "1";
}

static String zwin_motors_stop(const String&) {
    setMotorPWM(26, 0);
    setMotorPWM(27, 0);
    setMotorPWM(25, 0);
    setMotorPWM(33, 0);
    Serial.println("[ZWIN SDK] Motors Stopped");
    return "1";
}

// ---------------------------------------------------------------------------
// 3. sonar Namespace
// ---------------------------------------------------------------------------

static String zwin_sonar_read(const String&) {
    pinMode(12, OUTPUT);
    digitalWrite(12, LOW);
    delayMicroseconds(2);
    digitalWrite(12, HIGH);
    delayMicroseconds(10);
    digitalWrite(12, LOW);

    pinMode(13, INPUT);
    long duration = pulseIn(13, HIGH, 30000);
    if (duration == 0) return "999";

    float distance = (duration * 0.0343f) / 2.0f;
    Serial.printf("[ZWIN SDK] Sonar reading: %.2f cm\n", distance);
    return String(distance, 2);
}

// ---------------------------------------------------------------------------
// 4. gemini Namespace (stub fallback for standalone SDK compilation)
// ---------------------------------------------------------------------------

static String zwin_gemini_chat(const String& params) {
    int promptIdx = params.indexOf("prompt=");
    if (promptIdx < 0) return "Error: Missing prompt parameter";

    String prompt = params.substring(promptIdx + 7);
    prompt.trim();
    if (prompt.startsWith("\"") && prompt.endsWith("\"")) {
        prompt = prompt.substring(1, prompt.length() - 1);
    }

    Serial.printf("[ZWIN SDK] Querying Gemini (Mock): '%s'\n", prompt.c_str());
    return "Gemini stub response for: " + prompt;
}

// ---------------------------------------------------------------------------
// Registration Entrypoint
// ---------------------------------------------------------------------------

void registerZwinSDK(ZwinVM& vm) {
    vm.registerFunction("status_led.write", zwin_led_write);
    vm.registerFunction("status_led.blink", zwin_led_blink);
    vm.registerFunction("motors.drive", zwin_motors_drive);
    vm.registerFunction("motors.stop", zwin_motors_stop);
    vm.registerFunction("sonar.read", zwin_sonar_read);
    vm.registerFunction("gemini.chat", zwin_gemini_chat);
    
    Serial.println("[ZWIN] SDK registered successfully.");
}
