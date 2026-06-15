#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

const char* ssid = "neo8";        
const char* password = "asd123123"; 
const char* mqtt_server = "broker.hivemq.com"; 

const char* topic_gerak = "robot/gerak"; 
const char* topic_gerak_vector = "robot/gerak/vector";
const char* topic_gerak_rotate = "robot/gerak/rotate";
const char* topic_jarak = "robot/jarak";                  
const char* topic_tendang = "robot/tendang";              
const char* topic_kecepatan = "robot/kecepatan";          
const char* topic_speed_L = "robot/kecepatan/kiri";       
const char* topic_speed_R = "robot/kecepatan/kanan";      
const char* topic_speed_B = "robot/kecepatan/belakang";   

WiFiClient espClient;
PubSubClient client(espClient);

// Function prototypes (biar callback bisa panggil)
void maju();
void mundur();
void serongKiriDepan();
void serongKananBelakang();
void serongKananDepan();
void serongKiriBelakang();
void putarKanan();
void putarKiri();
void geserKanan();
void geserKiri();
void berhenti();
void setup_wifi();
long getJarak();
void reconnect();
void gerakDariVector(float vx, float vy);

const int ENA_B = 13; const int IN1_B = 14; const int IN2_B = 12; 
const int ENA_L = 25; const int IN1_L = 27; const int IN2_L = 26; 
const int ENA_R = 4;  const int IN1_R = 2;  const int IN2_R = 15; 

const int trigPin = 18;
const int echoPin = 19;

const int pinSolenoid = 23; 

int speedL = 255; 
int speedR = 255; 
int speedB = 255; 
const int freq = 30000;
const int resolution = 8;
const int chanL = 0;
const int chanR = 1;
const int chanB = 2;

unsigned long lastJarakTime = 0;

bool sedangMenendang = false;
unsigned long waktuMulaiTendang = 0;
const int durasiTendang = 500;
unsigned long lastCmdTime = 0;

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Menghubungkan ke Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WIFI] TERHUBUNG!");
  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.localIP());
}

void gerakDariVector(float vx, float vy) {
  if (abs(vx) < 0.1 && abs(vy) < 0.1) { berhenti(); return; }
  if (abs(vy) > abs(vx)) {
    if (vy < 0) {
      if (vx > 0.3) serongKananDepan();
      else if (vx < -0.3) serongKiriDepan();
      else maju();
    } else {
      if (vx > 0.3) serongKananBelakang();
      else if (vx < -0.3) serongKiriBelakang();
      else mundur();
    }
  } else {
    if (vx > 0) {
      if (vy < -0.3) serongKananDepan();
      else if (vy > 0.3) serongKananBelakang();
      else geserKanan();
    } else {
      if (vy < -0.3) serongKiriDepan();
      else if (vy > 0.3) serongKiriBelakang();
      else geserKiri();
    }
  }
  lastCmdTime = millis();
}

void callback(char* topic, byte* message, unsigned int length) {
  String payload;
  for (int i = 0; i < length; i++) {
    payload += (char)message[i];
  }
  
  Serial.print("[MQTT] Topik: "); Serial.print(topic);
  Serial.print(" | Pesan: "); Serial.println(payload);

  if (String(topic) == topic_gerak) {
    Serial.print("[GERAK] Perintah teks: "); Serial.println(payload);
    if (payload == "maju") { maju(); }
    else if (payload == "mundur") { mundur(); }
    else if (payload == "geserKanan") { geserKanan(); }
    else if (payload == "geserKiri") { geserKiri(); }
    else if (payload == "serongKiriDepan") { serongKiriDepan(); }
    else if (payload == "serongKananDepan") { serongKananDepan(); }
    else if (payload == "serongKiriBelakang") { serongKiriBelakang(); }
    else if (payload == "serongKananBelakang") { serongKananBelakang(); }
    else if (payload == "putarKanan") { putarKanan(); }
    else if (payload == "putarKiri") { putarKiri(); }
    else if (payload == "berhenti") { berhenti(); }
    else { Serial.print("[GERAK] Perintah tidak dikenal: "); Serial.println(payload); }
  }
  else if (String(topic) == topic_gerak_vector) {
    int comma1 = payload.indexOf(',');
    if (comma1 > 0) {
      float vx = payload.substring(0, comma1).toFloat();
      float vy = payload.substring(comma1 + 1).toFloat();
      Serial.print("[VECTOR] vx="); Serial.print(vx); Serial.print(" vy="); Serial.println(vy);
      gerakDariVector(vx, vy);
    } else {
      Serial.println("[VECTOR] Format salah — tidak ada koma");
    }
  }
  else if (String(topic) == topic_gerak_rotate) {
    float omega = payload.toFloat();
    Serial.print("[ROTATE] omega="); Serial.println(omega);
    if (omega > 0.1) putarKanan();
    else if (omega < -0.1) putarKiri();
    else berhenti();
    lastCmdTime = millis();
  }
  else if (String(topic) == topic_tendang) {
    Serial.print("[TENDANG] Payload: "); Serial.println(payload);
    if (payload == "tendang" && !sedangMenendang) {
      sedangMenendang = true;
      waktuMulaiTendang = millis();
      digitalWrite(pinSolenoid, LOW); 
      Serial.println("[TENDANG] EKSEKUSI!");
    } else if (sedangMenendang) {
      Serial.println("[TENDANG] DITOLAK — sudah menendang");
    } else {
      Serial.println("[TENDANG] DITOLAK — payload bukan 'tendang'");
    }
  }
  else {
    int newSpeed = payload.toInt();
    if (newSpeed >= 0 && newSpeed <= 255) {
      if (String(topic) == topic_speed_L) { speedL = newSpeed; Serial.print("[SPEED] L="); Serial.println(speedL); } 
      else if (String(topic) == topic_speed_R) { speedR = newSpeed; Serial.print("[SPEED] R="); Serial.println(speedR); } 
      else if (String(topic) == topic_speed_B) { speedB = newSpeed; Serial.print("[SPEED] B="); Serial.println(speedB); } 
      else if (String(topic) == topic_kecepatan) { speedL = speedR = speedB = newSpeed; Serial.print("[SPEED] ALL="); Serial.println(newSpeed); }
    } else {
      Serial.print("[SPEED] Nilai tidak valid: "); Serial.println(newSpeed);
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Mencoba terhubung ke Broker MQTT...");
    String clientId = "NagaHitam-KRSBI-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("[MQTT] TERHUBUNG ke broker!");
      client.subscribe(topic_gerak);
      client.subscribe(topic_gerak_vector);
      client.subscribe(topic_gerak_rotate);
      client.subscribe(topic_tendang); 
      client.subscribe(topic_kecepatan);
      client.subscribe(topic_speed_L);
      client.subscribe(topic_speed_R);
      client.subscribe(topic_speed_B);
    } else {
      Serial.print("[MQTT] Gagal konek, rc=");
      Serial.print(client.state());
      Serial.println(" — retry 5 detik");
      delay(5000);
    }
  }
}

long getJarak() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH);
  long distance = duration * 0.034 / 2;
  return distance;
}

void setup() {
  Serial.begin(115200);

  digitalWrite(pinSolenoid, HIGH); 
  pinMode(pinSolenoid, OUTPUT);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  pinMode(IN1_B, OUTPUT); pinMode(IN2_B, OUTPUT);
  pinMode(IN1_L, OUTPUT); pinMode(IN2_L, OUTPUT);
  pinMode(IN1_R, OUTPUT); pinMode(IN2_R, OUTPUT);

  ledcSetup(chanB, freq, resolution); ledcAttachPin(ENA_B, chanB);
  ledcSetup(chanL, freq, resolution); ledcAttachPin(ENA_L, chanL);
  ledcSetup(chanR, freq, resolution); ledcAttachPin(ENA_R, chanR);

  Serial.println("[BOOT] Firmware PRODUCTION v1.0");
  berhenti(); 
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); 

  unsigned long now = millis();

  if (now - lastJarakTime > 500) {
    lastJarakTime = now;
    long jarak = getJarak();
    client.publish(topic_jarak, String(jarak).c_str());
  }

  if (sedangMenendang && (now - waktuMulaiTendang >= durasiTendang)) {
    digitalWrite(pinSolenoid, HIGH); 
    sedangMenendang = false;
    Serial.println("=> Tuas ditarik");
  }

  // Safety watchdog: auto-stop if no command for 1s
  if (lastCmdTime > 0 && millis() - lastCmdTime > 1000) {
    Serial.println("[WATCHDOG] Stop — tidak ada perintah >1 detik");
    berhenti();
    lastCmdTime = millis();
  }
}

void logMotor() {
  Serial.print("[MOTOR] L="); Serial.print(speedL);
  Serial.print(" R="); Serial.print(speedR);
  Serial.print(" B="); Serial.println(speedB);
}

void maju() { 
  digitalWrite(IN1_L, HIGH); digitalWrite(IN2_L, LOW);  ledcWrite(chanL, speedL);
  digitalWrite(IN1_R, HIGH); digitalWrite(IN2_R, LOW);  ledcWrite(chanR, speedR); 
  digitalWrite(IN1_B, LOW);  digitalWrite(IN2_B, LOW);  ledcWrite(chanB, 0);
  Serial.print("[AKSI] maju → "); logMotor();
}
void mundur() { 
  digitalWrite(IN1_L, LOW);  digitalWrite(IN2_L, HIGH); ledcWrite(chanL, speedL);
  digitalWrite(IN1_R, LOW);  digitalWrite(IN2_R, HIGH); ledcWrite(chanR, speedR); 
  digitalWrite(IN1_B, LOW);  digitalWrite(IN2_B, LOW);  ledcWrite(chanB, 0);
  Serial.print("[AKSI] mundur → "); logMotor();
}
void serongKiriDepan() { 
  digitalWrite(IN1_L, LOW);  digitalWrite(IN2_L, LOW);  ledcWrite(chanL, 0); 
  digitalWrite(IN1_R, HIGH); digitalWrite(IN2_R, LOW);  ledcWrite(chanR, speedR); 
  digitalWrite(IN1_B, HIGH); digitalWrite(IN2_B, LOW);  ledcWrite(chanB, speedB);
  Serial.print("[AKSI] serongKiriDepan → "); logMotor();
}
void serongKananBelakang() { 
  digitalWrite(IN1_L, HIGH); digitalWrite(IN2_L, LOW);  ledcWrite(chanL, speedL); 
  digitalWrite(IN1_R, LOW);  digitalWrite(IN2_R, LOW);  ledcWrite(chanR, 0); 
  digitalWrite(IN1_B, LOW);  digitalWrite(IN2_B, HIGH); ledcWrite(chanB, speedB);
  Serial.print("[AKSI] serongKananBelakang → "); logMotor();
}
void serongKananDepan() { 
  digitalWrite(IN1_L, HIGH); digitalWrite(IN2_L, LOW);  ledcWrite(chanL, speedL); 
  digitalWrite(IN1_R, LOW);  digitalWrite(IN2_R, LOW);  ledcWrite(chanR, 0); 
  digitalWrite(IN1_B, HIGH); digitalWrite(IN2_B, LOW);  ledcWrite(chanB, speedB);
  Serial.print("[AKSI] serongKananDepan → "); logMotor();
}
void serongKiriBelakang() { 
  digitalWrite(IN1_L, LOW);  digitalWrite(IN2_L, LOW);  ledcWrite(chanL, 0); 
  digitalWrite(IN1_R, HIGH); digitalWrite(IN2_R, LOW);  ledcWrite(chanR, speedR); 
  digitalWrite(IN1_B, LOW);  digitalWrite(IN2_B, HIGH); ledcWrite(chanB, speedB);
  Serial.print("[AKSI] serongKiriBelakang → "); logMotor();
}
void putarKanan() { 
  digitalWrite(IN1_L, HIGH); digitalWrite(IN2_L, LOW);  ledcWrite(chanL, speedL); 
  digitalWrite(IN1_R, LOW);  digitalWrite(IN2_R, HIGH); ledcWrite(chanR, speedR); 
  digitalWrite(IN1_B, HIGH); digitalWrite(IN2_B, LOW);  ledcWrite(chanB, speedB);
  Serial.print("[AKSI] putarKanan → "); logMotor();
}
void putarKiri() { 
  digitalWrite(IN1_L, LOW);  digitalWrite(IN2_L, HIGH); ledcWrite(chanL, speedL); 
  digitalWrite(IN1_R, HIGH); digitalWrite(IN2_R, LOW);  ledcWrite(chanR, speedR); 
  digitalWrite(IN1_B, LOW);  digitalWrite(IN2_B, HIGH); ledcWrite(chanB, speedB);
  Serial.print("[AKSI] putarKiri → "); logMotor();
}
void geserKanan() { 
  digitalWrite(IN1_L, LOW);  digitalWrite(IN2_L, LOW);  ledcWrite(chanL, 0); 
  digitalWrite(IN1_R, LOW);  digitalWrite(IN2_R, HIGH); ledcWrite(chanR, speedR);   
  digitalWrite(IN1_B, HIGH); digitalWrite(IN2_B, LOW);  ledcWrite(chanB, speedB);
  Serial.print("[AKSI] geserKanan → "); logMotor();
}
void geserKiri() { 
  digitalWrite(IN1_L, LOW);  digitalWrite(IN2_L, HIGH); ledcWrite(chanL, speedL);   
  digitalWrite(IN1_R, LOW);  digitalWrite(IN2_R, LOW);  ledcWrite(chanR, 0);
  digitalWrite(IN1_B, LOW);  digitalWrite(IN2_B, HIGH); ledcWrite(chanB, speedB);
  Serial.print("[AKSI] geserKiri → "); logMotor();
}
void berhenti() {
  digitalWrite(IN1_L, LOW); digitalWrite(IN2_L, LOW); ledcWrite(chanL, 0);
  digitalWrite(IN1_R, LOW); digitalWrite(IN2_R, LOW); ledcWrite(chanR, 0);
  digitalWrite(IN1_B, LOW); digitalWrite(IN2_B, LOW); ledcWrite(chanB, 0);
  Serial.println("[AKSI] berhenti");
}