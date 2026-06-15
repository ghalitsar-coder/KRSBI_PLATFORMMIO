#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

/*
 * =========================================================================
 * ROBOT KRSBI - FIRMWARE FINAL V1.0 (PRO)
 * =========================================================================
 * - No Bootstrapping Pin Conflicts
 * - 3-Wheel Omni Kinematics
 * - Safety Auto-Capitan Logic
 * - Watchdog Motor Auto-Stop
 * =========================================================================
 */

// 1. WIFI & MQTT CONFIG
const char* ssid = "neo8";         
const char* password = "asd123123"; 
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

// Topics
const char* topic_drive_vector = "robot/drive/vector";
const char* topic_drive_rotate = "robot/drive/rotate";
const char* topic_action_kick = "robot/action/kick";
const char* topic_action_dribble = "robot/action/dribble";
const char* topic_status_ultrasonic = "robot/status/ultrasonic";

// 2. PIN ASSIGNMENT (SESUAI WIRING HARDWARE ASLI SAAT INI)
// ---------------------------------------------------------
// Motor Kiri (M1) - Pakai Driver 1 (Aman)
const int ENA_M1 = 25;  
const int IN1_M1 = 26;  
const int IN2_M1 = 27;  

// Motor Belakang (M3) - Pakai Driver 1
const int ENA_M3 = 13;  
const int IN1_M3 = 14;  // WARNING: Pin 12 adalah MTDI (Bootstrapping). Jika gagal boot, cabut pin ini saat dinyalakan.
const int IN2_M3 = 12;  

// Motor Kanan (M2) - Pakai Driver 2
const int ENA_M2 = 4;   
const int IN1_M2 = 15;  // Dibalik biar crab kanan jalan bener
const int IN2_M2 = 2;   // Dibalik biar crab kanan jalan bener

// Ultrasonik (Sensor)
const int TRIG_PIN = 18; // Sesuai lancah.txt asli
const int ECHO_PIN = 19; 

// Actuators
const int KICK_PIN = 23;
const int SERVO_KIRI_PIN = 29;
const int SERVO_KANAN_PIN = 17; // Dikembalikan ke layout awal yang tidak conflict

// 3. OBJECTS & STATE
WiFiClient espClient;
PubSubClient client(espClient);
Servo servoKiri;
Servo servoKanan;

// PWM Params
const int freq = 20000;
const int res = 8;
const int chanM1 = 8;
const int chanM2 = 9;
const int chanM3 = 10;

// Safety & Tracking
unsigned long lastCmdTime = 0;
unsigned long lastTeleTime = 0;
bool capitanTerbuka = true;
bool manualOverride = false;
float smoothedDist = 100.0;
float targetVx = 0.0, targetVy = 0.0, targetOmega = 0.0;

// 4. MOTOR CONTROL
void setMotor(int m1, int m2, int m3) {
    // M1 (Kiri)
    if (m1 == 0) {
        digitalWrite(IN1_M1, LOW);
        digitalWrite(IN2_M1, LOW);
    } else {
        digitalWrite(IN1_M1, m1 >= 0 ? HIGH : LOW);
        digitalWrite(IN2_M1, m1 >= 0 ? LOW : HIGH);
    }
    ledcWrite(chanM1, abs(m1));

    // M2 (Kanan)
    if (m2 == 0) {
        digitalWrite(IN1_M2, LOW);
        digitalWrite(IN2_M2, LOW);
    } else {
        digitalWrite(IN1_M2, m2 >= 0 ? HIGH : LOW);
        digitalWrite(IN2_M2, m2 >= 0 ? LOW : HIGH);
    }
    ledcWrite(chanM2, abs(m2));

    // M3 (Belakang)
    if (m3 == 0) {
        digitalWrite(IN1_M3, LOW);
        digitalWrite(IN2_M3, LOW);
    } else {
        digitalWrite(IN1_M3, m3 >= 0 ? HIGH : LOW);
        digitalWrite(IN2_M3, m3 >= 0 ? LOW : HIGH);
    }
    ledcWrite(chanM3, abs(m3));
}

void kinematikaOmni(float vx, float vy, float omega = 0.0) {
    // Kinematika 3 roda omni 120 derajat + rotasi
    // Konfigurasi: M1=150° (kiri-depan), M2=30° (kanan-depan), M3=270° (belakang tegak lurus)
    // omega: positif = CW, negatif = CCW (range -1.0 s/d 1.0)
    float v1 = (-1.0 * vx) + (0.866 * vy) + omega; // Kiri (150°)
    float v2 = (-1.0 * vx) - (0.866 * vy) + omega; // Kanan (30°)
    float v3 = vx + omega;                         // Belakang (270°)

    int s1 = constrain(v1 * 255, -255, 255);
    int s2 = constrain(v2 * 255, -255, 255);
    int s3 = constrain(v3 * 255, -255, 255);

    setMotor(s1, s2, s3);
    lastCmdTime = millis();
}

// 5. CALLBACK (MQTT HANDLING)
void callback(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (int i = 0; i < length; i++) msg += (char)payload[i];

    if (strcmp(topic, topic_drive_vector) == 0) {
        int comma1 = msg.indexOf(',');
        if (comma1 > 0) {
            float vx = msg.substring(0, comma1).toFloat();
            int comma2 = msg.indexOf(',', comma1 + 1);
            float vy = msg.substring(comma1 + 1, comma2 > 0 ? comma2 : msg.length()).toFloat();
            float omega = (comma2 > 0) ? msg.substring(comma2 + 1).toFloat() : 0.0;
            targetVx = vx; targetVy = vy; targetOmega = omega;
            kinematikaOmni(targetVx, targetVy, targetOmega);
            lastCmdTime = millis();
        }
    }
    else if (strcmp(topic, topic_drive_rotate) == 0) {
        targetOmega = msg.toFloat();
        kinematikaOmni(targetVx, targetVy, targetOmega);
        lastCmdTime = millis();
    }
    else if (strcmp(topic, topic_action_kick) == 0 && msg == "KICK") {
        if (capitanTerbuka) { // Safety: Kick only if open
            digitalWrite(KICK_PIN, HIGH);
            delay(100);
            digitalWrite(KICK_PIN, LOW);
            Serial.println("[ACTION] Kick Fired!");
        }
    }
    else if (strcmp(topic, topic_action_dribble) == 0) {
        if (msg == "LOCK") {
            manualOverride = true;
            servoKiri.write(90); servoKanan.write(90);
            capitanTerbuka = false;
        } else if (msg == "RELEASE") {
            manualOverride = true;
            servoKiri.write(0); servoKanan.write(180);
            capitanTerbuka = true;
        } else if (msg == "AUTO") {
            manualOverride = false;
        }
    }
}

// 6. SETUP & RECONNECT
void reconnect() {
    while (!client.connected()) {
        Serial.print("[MQTT] Reconnecting...");
        String id = "KRSBI-"; id += String(random(0xffff), HEX);
        if (client.connect(id.c_str())) {
            Serial.println("CONNECTED");
            client.subscribe(topic_drive_vector);
            client.subscribe(topic_drive_rotate);
            client.subscribe(topic_action_kick);
            client.subscribe(topic_action_dribble);
        } else {
            Serial.print("FAILED ("); Serial.print(client.state()); Serial.println(") retry in 5s");
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);

    // Pin Modes
    pinMode(IN1_M1, OUTPUT); pinMode(IN2_M1, OUTPUT);
    pinMode(IN1_M2, OUTPUT); pinMode(IN2_M2, OUTPUT);
    pinMode(IN1_M3, OUTPUT); pinMode(IN2_M3, OUTPUT);
    pinMode(KICK_PIN, OUTPUT);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    // LEDC (PWM) Setup
    ledcSetup(chanM1, freq, res); ledcAttachPin(ENA_M1, chanM1);
    ledcSetup(chanM2, freq, res); ledcAttachPin(ENA_M2, chanM2);
    ledcSetup(chanM3, freq, res); ledcAttachPin(ENA_M3, chanM3);

    // Servo Setup
    ESP32PWM::allocateTimer(0);
    servoKiri.setPeriodHertz(50);
    servoKanan.setPeriodHertz(50);
    servoKiri.attach(SERVO_KIRI_PIN, 500, 2400);
    servoKanan.attach(SERVO_KANAN_PIN, 500, 2400);

    // Initial State
    setMotor(0, 0, 0);
    digitalWrite(KICK_PIN, LOW);
    servoKiri.write(0); servoKanan.write(180);

    // Networking
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\n[WIFI] Connected");

    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
}

// 7. LOOP
void loop() {
    if (!client.connected()) reconnect();
    client.loop();

    // 1. Safety Watchdog (Auto-stop both if MQTT lost)
    if (millis() - lastCmdTime > 1000) {
        targetVx = 0; targetVy = 0; targetOmega = 0;
        setMotor(0, 0, 0);
    }

    // 2. Telemetry & Auto-Capitan Logic (10Hz)
    if (millis() - lastTeleTime > 100) {
        lastTeleTime = millis();

        // Distance Smoothing
        digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
        digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
        digitalWrite(TRIG_PIN, LOW);
        long duration = pulseIn(ECHO_PIN, HIGH, 30000);
        float currentDist = (duration == 0) ? 99.0 : duration * 0.034 / 2;
        smoothedDist = (smoothedDist * 0.7) + (currentDist * 0.3);

        // Auto-Capitan
        if (!manualOverride) {
            if (smoothedDist < 12.0) {
                servoKiri.write(90); servoKanan.write(90);
                capitanTerbuka = false;
            } else {
                servoKiri.write(0); servoKanan.write(180);
                capitanTerbuka = true;
            }
        }

        // Send Distance to UI
        char buf[8]; dtostrf(smoothedDist, 4, 2, buf);
        client.publish(topic_status_ultrasonic, buf);

        // --- TAMBAHAN LOG UNTUK PLATFORMIO ---
        Serial.print("[SENSOR] Jarak Mentah: ");
        Serial.print(currentDist);
        Serial.print(" cm | Jarak Halus (Smoothed): ");
        Serial.println(smoothedDist);
    }
}
