#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// ==========================================
// 1. KONFIGURASI WIFI & HIVEMQ CLOUD
// ==========================================
const char* ssid = "neo8";         
const char* password = "asd123123"; 

// HiveMQ Public Broker (untuk testing - tanpa auth)
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883; // Non-TLS Port untuk public broker
const char* mqtt_user = ""; // Kosong untuk public broker
const char* mqtt_pass = ""; // Kosong untuk public broker

// MQTT Topics
const char* topic_drive_vector = "robot/drive/vector";
const char* topic_action_kick = "robot/action/kick";
const char* topic_action_dribble = "robot/action/dribble";
const char* topic_status_ultrasonic = "robot/status/ultrasonic";
const char* topic_status_wifi = "robot/status/wifi";
const char* topic_status_mqtt = "robot/status/mqtt";

// ==========================================
// 2. GLOBAL CLIENT OBJECTS
// ==========================================
WiFiClient espClient; // Pakai WiFiClient biasa untuk broker publik
PubSubClient client(espClient);

// ==========================================
// 3. DEFINISI PIN & HARDWARE (PERUBAHAN: SESUAI LANCAH.TXT)
// ==========================================
// Ultrasonik
const int TRIG_PIN = 5;
const int ECHO_PIN = 4;

// Motor Belakang (Back) - M3 sesuai lancah.txt: ENA_B=13, IN1_B=12, IN2_B=14
const int ENA_M3 = 13;  // ENA_B
const int IN1_M3 = 12;  // IN1_B
const int IN2_M3 = 14;  // IN2_B

// Motor Kiri (Left) - M1
const int ENA_M1 = 25;  // ENA_L
const int IN1_M1 = 27;  // IN1_L
const int IN2_M1 = 26;  // IN2_L

// Motor Kanan (Right) - M2
const int ENA_M2 = 4;   // ENA_R
const int IN1_M2 = 2;   // IN1_R
const int IN2_M2 = 15;  // IN2_R

// Solenoid (Penendang)
const int KICK_PIN = 21;

// Servo (Capitan)
const int SERVO_KIRI_PIN = 19;
const int SERVO_KANAN_PIN = 18;

// ==========================================
// 4. SERVO & PWM CONFIG
// ==========================================
Servo servoKiri;
Servo servoKanan;

const int freq = 1000;
const int resolution = 8;
const int pwmM1 = 0;      // Channel 0 untuk Motor 1 (Kiri)
const int pwmM2 = 1;      // Channel 1 untuk Motor 2 (Kanan)
const int pwmM3 = 2;      // Channel 2 untuk Motor 3 (Belakang)

// ==========================================
// 5. VARIABEL STATE
// ==========================================
unsigned long lastPublish = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastMqttReconnect = 0;
bool manualOverride = false;
bool capitanTerbuka = true; // State tracking agar tidak spam log
int reconnectAttempts = 0;
const int MAX_RECONNECT_ATTEMPTS = 5;

// ==========================================
// 6. FUNGSI HELPER LOGGING
// ==========================================
void logSerial(const char* prefix, const char* message) {
  Serial.print("[");
  Serial.print(prefix);
  Serial.print("] ");
  Serial.println(message);
}

void logSerialInt(const char* prefix, const char* message, int value) {
  Serial.print("[");
  Serial.print(prefix);
  Serial.print("] ");
  Serial.print(message);
  Serial.println(value);
}

// ==========================================
// 7. FUNGSI CAPITAN (SERVO)
// ==========================================
void tutupCapitan() {
  if (capitanTerbuka) {
    servoKiri.write(90);
    servoKanan.write(90);
    logSerial("SERVO", "Capitan: TUTUP");
    capitanTerbuka = false;
  }
}

void bukaCapitan() {
  if (!capitanTerbuka) {
    servoKiri.write(0);
    servoKanan.write(180);
    logSerial("SERVO", "Capitan: BUKA");
    capitanTerbuka = true;
  }
}

// ==========================================
// 8. KINEMATIKA 3 RODA OMNIWHEEL (120°)
// ==========================================
void setMotorSpeed(int m1, int m2, int m3) {
  // Motor 1 (Kiri)
  digitalWrite(IN1_M1, m1 > 0 ? HIGH : LOW);
  digitalWrite(IN2_M1, m1 > 0 ? LOW : HIGH);
  ledcWrite(pwmM1, abs(m1));

  // Motor 2 (Kanan)
  digitalWrite(IN1_M2, m2 > 0 ? HIGH : LOW);
  digitalWrite(IN2_M2, m2 > 0 ? LOW : HIGH);
  ledcWrite(pwmM2, abs(m2));

  // Motor 3 (Belakang)
  digitalWrite(IN1_M3, m3 > 0 ? HIGH : LOW);
  digitalWrite(IN2_M3, m3 > 0 ? LOW : HIGH);
  ledcWrite(pwmM3, abs(m3));

  Serial.print("[MOTOR] M1=");
  Serial.print(m1);
  Serial.print(" M2=");
  Serial.print(m2);
  Serial.print(" M3=");
  Serial.println(m3);
}

void kinematikaOmni(float vx, float vy) {
  float v1 = (-0.5 * vx) + (0.866 * vy); // Motor 1 (Kiri)
  float v2 = (-0.5 * vx) - (0.866 * vy); // Motor 2 (Kanan)
  float v3 = vx;                         // Motor 3 (Belakang)

  int speed1 = constrain(v1 * 255, -255, 255);
  int speed2 = constrain(v2 * 255, -255, 255);
  int speed3 = constrain(v3 * 255, -255, 255);

  setMotorSpeed(speed1, speed2, speed3);
}

// ==========================================
// 9. CALLBACK MQTT (TERIMA PERINTAH)
// ==========================================
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("[MQTT_RX] Topic: ");
  Serial.print(topic);
  Serial.print(" | Payload: ");
  Serial.println(message);

  // -- KICK SOLENOID --
  if (strcmp(topic, topic_action_kick) == 0 && message == "KICK") {
    logSerial("ACTION", "KICK!");
    digitalWrite(KICK_PIN, HIGH);
    delay(100);
    digitalWrite(KICK_PIN, LOW);
  }

  // -- CAPITAN CONTROL --
  else if (strcmp(topic, topic_action_dribble) == 0) {
    if (message == "LOCK") {
      manualOverride = true;
      tutupCapitan();
    } else if (message == "RELEASE") {
      manualOverride = true;
      bukaCapitan();
    } else if (message == "AUTO") {
      manualOverride = false;
      logSerial("DRIBBLE", "Mode AUTO");
    }
  }

  // -- DRIVE VECTOR (CSV: vx,vy) --
  else if (strcmp(topic, topic_drive_vector) == 0) {
    char payloadChar[length + 1];
    message.toCharArray(payloadChar, length + 1);

    char* token = strtok(payloadChar, ",");
    if (token != NULL) {
      float vx = atof(token);
      token = strtok(NULL, ",");
      if (token != NULL) {
        float vy = atof(token);
        kinematikaOmni(vx, vy);
      }
    }
  }
}

// ==========================================
// 10. SETUP WIFI
// ==========================================
void setup_wifi() {
  logSerial("WIFI", "Menghubungkan...");
  Serial.print("SSID: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    logSerial("WIFI", "Terhubung!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    // Publish WiFi status
    client.publish(topic_status_wifi, "CONNECTED");
  } else {
    logSerial("WIFI", "GAGAL terhubung!");
    client.publish(topic_status_wifi, "DISCONNECTED");
  }
}

// ==========================================
// 11. MQTT RECONNECT (WITHOUT SSL)
// ==========================================
void reconnect() {
  if (client.connected()) return;

  if (millis() - lastMqttReconnect < 3000) return;
  lastMqttReconnect = millis();

  Serial.print("[MQTT] Mencoba terhubung ke ");
  Serial.print(mqtt_server);
  Serial.print(":");
  Serial.println(mqtt_port);

  String clientId = "RobotKRSBI-ESP32-";
  clientId += String(random(0xffff), HEX);

  // Untuk public broker: connect tanpa username/password
  if (client.connect(clientId.c_str())) {
    logSerial("MQTT", "BERHASIL terhubung!");
    reconnectAttempts = 0;

    // Subscribe ke topics
    client.subscribe(topic_drive_vector);
    client.subscribe(topic_action_kick);
    client.subscribe(topic_action_dribble);

    // Publish status
    client.publish(topic_status_mqtt, "CONNECTED");

    Serial.println("[MQTT] Subscribe berhasil ke:");
    Serial.println("  - robot/drive/vector");
    Serial.println("  - robot/action/kick");
    Serial.println("  - robot/action/dribble");
  } else {
    reconnectAttempts++;
    int state = client.state();
    Serial.print("[MQTT] GAGAL, kode error: ");
    Serial.println(state);
    // -1 = timeout, -2 = invalid server, -3 = truncated, -4 = bad protocol, -5 = bad client id, etc.

    if (state == -1) {
      logSerial("MQTT", "ERROR: Connection timeout (WiFi ok?)");
    } else if (state == -2) {
      logSerial("MQTT", "ERROR: Invalid server address");
    } else if (state == -4) {
      logSerial("MQTT", "ERROR: Bad MQTT protocol (check port)");
    } else if (state == -5) {
      logSerial("MQTT", "ERROR: Client ID rejected (check credentials)");
    }

    if (reconnectAttempts > MAX_RECONNECT_ATTEMPTS) {
      logSerial("MQTT", "Max reconnect attempts reached, waiting...");
      reconnectAttempts = 0;
    }
  }
}

// ==========================================
// 12. BACA ULTRASONIK
// ==========================================
float readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  float distance = (duration == 0) ? 999.0 : duration * 0.034 / 2;

  return distance;
}

// ==========================================
// 13. SETUP AWAL
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000); // Tunggu serial ready

  Serial.println("\n\n");
  Serial.println("========================================");
  Serial.println("ROBOT KRSBI - STARTUP");
  Serial.println("========================================");

  // Setup Pin Ultrasonik
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Setup Pin Motor
  pinMode(IN1_M1, OUTPUT); pinMode(IN2_M1, OUTPUT);
  pinMode(IN1_M2, OUTPUT); pinMode(IN2_M2, OUTPUT);
  pinMode(IN1_M3, OUTPUT); pinMode(IN2_M3, OUTPUT);

  // Setup PWM Motor
  ledcSetup(pwmM1, freq, resolution); ledcAttachPin(ENA_M1, pwmM1);
  ledcSetup(pwmM2, freq, resolution); ledcAttachPin(ENA_M2, pwmM2);
  ledcSetup(pwmM3, freq, resolution); ledcAttachPin(ENA_M3, pwmM3);

  // Setup Solenoid
  pinMode(KICK_PIN, OUTPUT);
  digitalWrite(KICK_PIN, LOW);

  // Setup Servo
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  servoKiri.setPeriodHertz(50);
  servoKanan.setPeriodHertz(50);
  servoKiri.attach(SERVO_KIRI_PIN, 500, 2400);
  servoKanan.attach(SERVO_KANAN_PIN, 500, 2400);

  logSerial("SETUP", "Pin & PWM initialized");

  // Setup WiFi
  setup_wifi();

  // Setup MQTT Client
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  logSerial("SETUP", "MQTT client configured");
  Serial.println("========================================");
  Serial.println("STARTUP COMPLETE - Entering loop...");
  Serial.println("========================================\n");

  bukaCapitan();
  capitanTerbuka = true; // Set awal terbuka
  delay(2000);
}

// ==========================================
// 14. MAIN LOOP
// ==========================================
void loop() {
  // Cek WiFi masih terhubung
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiCheck > 5000) {
      lastWifiCheck = millis();
      logSerial("WIFI", "Terputus! Mencoba reconnect...");
      WiFi.reconnect();
    }
    return;
  }

  // Jaga koneksi MQTT
  if (!client.connected()) {
    reconnect();
    return;
  }
  client.loop();

  // Telemetri & Logika Auto-Capitan (10Hz)
  if (millis() - lastPublish > 100) {
    lastPublish = millis();

    // Baca Ultrasonik
    float distance = readUltrasonic();

    // Publish ke Frontend
    char distanceStr[16];
    dtostrf(distance, 6, 2, distanceStr);
    client.publish(topic_status_ultrasonic, distanceStr);

    // Logika Auto-Capitan
    if (!manualOverride) {
      if (distance > 0 && distance < 10.0) {
        tutupCapitan();
      } else {
        bukaCapitan();
      }
    }
  }
}