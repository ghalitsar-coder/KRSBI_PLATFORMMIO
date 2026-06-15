# PRESENTASI: ARSITEKTUR KOMUNIKASI ROBOT KRSBI

---

## SLIDE 1 — Judul

**ARSITEKTUR KOMUNIKASI ROBOT OMNIDIRECTIONAL**
Frontend Web + Mobile ↔ ESP32 (IoT)

---

## SLIDE 2 — Gambaran Besar (Topologi)

```
[ Web App ] ─── WebSocket ──┐
                             ├── [ MQTT BROKER ] ── TCP ── [ ESP32 Robot ]
[Mobile App] ── WebSocket ──┘       (HiveMQ Cloud)
```

**Pertanyaan:** Gimana caranya HP/laptop ngomong sama robot yang ada di lapangan?

**Jawaban singkat:** Mereka tidak terhubung langsung. Mereka ngobrol lewat "pos tengah" yang bernama **MQTT Broker**. Si robot dan si HP sama-sama "ngepos" ke broker yang sama.

**Kenapa?**
- Robot pake WiFi biasa ( jaringan lokal)
- HP/laptop bisa di mana aja (4G, WiFi rumah, WiFi kampus)
- Broker jembatani mereka walau network berbeda

---

## SLIDE 3 — Protokol: MQTT (Message Queuing Telemetry Transport)

**Protokol yang dipake: MQTT**

MQTT itu protokol berbasis **publish/subscribe**, bukan request/response kayak HTTP.

**Analogi Koran:**
| Konsep | Analogi |
|--------|---------|
| **Broker** | Percetakan koran |
| **Publish** | Penulis ngirim artikel ke percetakan |
| **Subscribe** | Pembaca langganan korang |
| **Topic** | Nama rubrik (Olahraga, Teknologi, dll) |

**Cara kerja:**
1. ESP32 subscribe ke topic `robot/drive/vector` — artinya robot "langganan" perintah gerak
2. Web app publish ke `robot/drive/vector` dengan payload `"0.50,-0.30"` — artinya web "ngirim artikel" ke rubrik itu
3. Broker kirimkan artikel itu ke semua yang langganan (yaitu ESP32)
4. ESP32 baca, gerakin motor

**Keuntungan MQTT:**
- Ringan (header 2 byte) — cocok buat IoT dengan RAM terbatas
- Real-time (latensi <100ms)
- Support banyak subscriber — banyak HP bisa kontrol 1 robot
- Support banyak publisher — robot bisa kirim telemetry, HP bisa kirim perintah

---

## SLIDE 4 — Kenapa Ada Dua Jenis Koneksi?

| | **ESP32** | **Web/Mobile App** |
|---|---|---|
| **Protokol** | MQTT over **TCP** | MQTT over **WebSocket** |
| **Port** | 1883 | 8884 |
| **Library** | PubSubClient | `mqtt.js` (npm) |

**Kenapa beda?**

- **ESP32:** Bisa TCP langsung. PubSubClient pake koneksi TCP raw ke broker. Simple, ringan, cocok buat microcontroller.
- **Browser/React Native:** Browser TIDAK bisa buka koneksi TCP raw karena alasan security (same-origin policy). Solusinya: **WebSocket**.
- **WebSocket** = protokol yang jalan di atas HTTP. Dia ngasih "pintu" buat TCP lewat browser.
- HiveMQ (dan broker umumnya) nyediain dual port: TCP (1883) buat device IoT, WebSocket (8884) buat browser/mobile.

**Di kode:**
```cpp
// ESP32 — TCP langsung
client.setServer("broker.hivemq.com", 1883);
```

```ts
// Web/Mobile — WebSocket Secure
const client = mqtt.connect("wss://broker.hivemq.com:8884/mqtt");
```

---

## SLIDE 5 — Alur Data Lengkap

### Skenario: Joystick digerakin ke kanan

```
1. User drag joystick ke kanan di HP
       │
2. React Native ngitung vx=1.0, vy=0.0
       │
3. Konversi ke CSV: "1.00,0.00"
       │
4. Publish ke topic "robot/drive/vector" via WebSocket
       │
5. MQTT Broker terima, forward ke subscriber
       │
6. ESP32 (subscribe "robot/drive/vector") terima pesan
       │
7. Callback ESP32 parse "1.00,0.00" → vx=1.0, vy=0.0
       │
8. kinematikaOmni(1.0, 0.0, 0.0) hitung kecepatan tiap roda
       │
9. setMotor(...) → PWM + H-Bridge → Roda berputar
       │
10. Robot geser ke kanan ✓
```

**Latensi total:** ~20-50ms (dari drag sampai roda gerak)

### Skenario: ESP32 kirim jarak ultrasonik

```
ESP32:
  1. trigger ultrasonik
  2. hitung jarak
  3. publish "12.5" ke topic "robot/status/ultrasonic"
  4. Broker forward ke semua subscriber

Mobile App:
  5. Terima data
  6. Update UI jarak di layar

Web App:
  7. (bisa juga subscribe, tapi belum diimplement)
```

---

## SLIDE 6 — Topic Tree

### DEVELOPMENT (saat ini di main.cpp)

| Topic | Arah | Fungsi |
|-------|------|--------|
| `robot/drive/vector` | Web → ESP32 | Perintah gerak (vx,vy) |
| `robot/drive/rotate` | Web → ESP32 | Perintah rotasi (omega) |
| `robot/action/kick` | Web → ESP32 | Tendang bola |
| `robot/action/dribble` | Web → ESP32 | Buka/tutup capit |
| `robot/status/ultrasonic` | ESP32 → Web | Data jarak |

### PRODUCTION (finalCode.ino.bak)

| Topic | Arah | Fungsi |
|-------|------|--------|
| `robot/gerak` | Web → ESP32 | Perintah teks ("maju") |
| `robot/gerak/vector` | Web → ESP32 | Perintah gerak joystick |
| `robot/gerak/rotate` | Web → ESP32 | Perintah rotasi |
| `robot/tendang` | Web → ESP32 | Tendang bola |
| `robot/kecepatan/*` | Web → ESP32 | Atur kecepatan motor |
| `robot/jarak` | ESP32 → Web | Data jarak |

Topic structure memungkinkan:
- **1-to-many:** Banyak HP bisa kontrol 1 robot (broadcast)
- **Many-to-1:** Robot bisa kirim ke banyak dashboard
- **Decoupling:** Web & robot gak perlu tahu IP satu sama lain

---

## SLIDE 7 — Kenapa Pake MQTT Bukan HTTP?

| Aspek | HTTP | MQTT |
|-------|------|------|
| **Model** | Request-Response | Publish-Subscribe |
| **Header** | ~800 byte | 2 byte |
| **Real-time** | Polling (bikin lambat) | Push (instant) |
| **RAM ESP32** | Butuh banyak (~40KB) | Ringan (~2KB) |
| **Many clients** | Ribet (tiap client harus request) | Built-in (broker urus) |
| **Koneksi putus** | Harus reconnect manual | Last Will + auto-reconnect |

**HTTP polling:** Web nanya tiap 100ms "ada data baru?" → boros bandwidth & baterai.

**MQTT push:** ESP32 langsung kirim pas ada data → efisien.

---

## SLIDE 8 — Keamanan & Robustness

1. **Watchdog Timer** — jika MQTT putus >1 detik, motor auto-stop (safety)
   ```cpp
   if (millis() - lastCmdTime > 1000) setMotor(0, 0, 0);
   ```

2. **Auto-Reconnect** — ESP32 terus coba reconnect ke broker
   ```cpp
   while (!client.connected()) { ... delay(5000); }
   ```

3. **No Single Point of Failure** — broker itu cloud (HiveMQ), bukan di laptop kita. Robot tetap bisa dikontrol dari mana aja.

4. **QoS 0** — pakai QoS 0 (fire and forget) biar latensi minimal. Cocok buat kontrol robot karena data kadaluarsa cepat.

---

## SLIDE 9 — DEV vs PRODUCTION

**Ganti mode = ganti .env + rename file**

| | DEVELOPMENT | PRODUCTION |
|---|---|---|
| **C++ file** | `main.cpp` (kinematika omni) | `finalCode.ino.bak` rename ke `main.cpp` |
| **Web .env** | `VITE_ENV=development` | `VITE_ENV=production` |
| **Mobile .env** | `EXPO_PUBLIC_ENV=development` | `EXPO_PUBLIC_ENV=production` |
| **Topic move** | `robot/drive/vector` | `robot/gerak/vector` |
| **Topic kick** | `robot/action/kick` | `robot/tendang` |
| **Dribble** | Ada | Tidak ada |

**Mekanisme di frontend:**
```ts
// topicConfig.ts
const IS_PRODUCTION = import.meta.env.VITE_ENV === "production";
export const TOPICS = IS_PRODUCTION ? PROD_TOPICS : DEV_TOPICS;
```
Semua komponen pake `TOPICS.move`, `TOPICS.kick`, dll — ganti env, ganti topic otomatis.

---

## SLIDE 10 — Tech Stack Summary

| Layer | Teknologi |
|-------|-----------|
| **Mikrokontroler** | ESP32, 240MHz, 320KB RAM |
| **Firmware** | C++ (Arduino Framework + PlatformIO) |
| **MQTT Library (ESP32)** | PubSubClient |
| **PWM Motor** | LEDC (16-bit, 20kHz) |
| **Wireless** | WiFi 2.4GHz |
| **MQTT Broker** | HiveMQ Cloud (broker.hivemq.com) |
| **Frontend Web** | React 19 + Vite + TypeScript + Tailwind |
| **MQTT Web** | `mqtt.js` via WebSocket Secure |
| **Mobile** | React Native + Expo SDK 54 |
| **MQTT Mobile** | `mqtt.js` via WebSocket Secure |

**Alur koneksi:**
```
Web/Mobile ──wss://──┐
                      ├── HiveMQ ──tcp://── ESP32
Robot ───tcp://──────┘
```

---

## SLIDE 11 — Demo / Live

1. Buka web dashboard (robocommand-neon)
2. Drag joystick — robot bergerak
3. Lihat serial monitor ESP32 — terima MQTT message
4. Tekan tombol kick — solenoid aktif
5. Tutup web, buka mobile app — kontrol dari HP
6. Ganti .env ke production, rename firmware — mode produksi

---

---

## BAGIAN 2: PENJELASAN BLOCK KODE KRUSIAL

---

### 1. MQTT Setup ESP32 — PubSubClient

```cpp
#include <WiFi.h>
#include <PubSubClient.h>

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
    WiFi.begin(ssid, password);
    // ...tunggu konek WiFi...
    
    client.setServer(mqtt_server, 1883);     // address broker MQTT
    client.setCallback(callback);            // fungsi yg dipanggil pas ada pesan masuk
}
```

**Penjelasan:**
- `WiFiClient` — objek koneksi TCP ke internet (bawaan ESP32)
- `PubSubClient` — "bungkus" WiFiClient biar ngerti protokol MQTT
- `setServer` — kasih tau alamat broker tujuan (HiveMQ port 1883)
- `setCallback` — daftarin fungsi `callback()` yang bakal otomatis dipanggil setiap kali ada pesan MQTT masuk. Ini inti dari model **publish/subscribe**
- **Kenapa 1883?** Itu port standar MQTT over TCP. Device IoT pake ini. Browser gak bisa pake port ini (browser cuma bisa WebSocket).

---

### 2. MQTT Setup Web/Mobile — mqtt.js

```ts
import mqtt from "mqtt";

const client = mqtt.connect("wss://broker.hivemq.com:8884/mqtt", {
  clientId: "robocommand-web-xxx",
  clean: true,
});
```

**Penjelasan:**
- **`wss://`** — WebSocket Secure. Bedanya dengan `ws://` adalah ini pake enkripsi TLS.
- **`8884`** — port khusus WebSocket MQTT. Broker kayak HiveMQ sediain 2 pintu: `1883` (TCP untuk IoT) dan `8884` (WebSocket untuk browser/mobile).
- Kenapa gak pake `1883`? **Browser tidak bisa buka TCP raw.** HTML5 kasih WebSocket sebagai gantinya — ini protokol yang jalan di atas HTTP, disediain khusus buat komunikasi real-time di web.
- **`clientId`** — setiap client MQTT harus punya ID unik. Biar broker tahu siapa siapa.
- **Kesimpulan:** ESP32 pake TCP (port 1883), browser pake WebSocket (port 8884). Keduanya ketemu di broker yang sama.

---

### 3. LEDC (PWM) Setup — buat apa sih?

```cpp
const int freq = 20000;       // 20 kHz — di atas audiable range
const int res = 8;            // 8-bit resolution (0-255)
const int chanM1 = 8;         // channel LEDC (ESP32 punya 16 channel)

ledcSetup(chanM1, freq, res);    // konfigurasi channel: frekuensi + resolusi
ledcAttachPin(ENA_M1, chanM1);   // hubungin channel ke pin fisik

// ngatur kecepatan:
ledcWrite(chanM1, 128);          // duty cycle 50% → motor setengah kecepatan
ledcWrite(chanM1, 255);          // duty cycle 100% → motor full speed
```

**Apa itu PWM (Pulse Width Modulation)?**

Bayangin kamu mau ngatur kecerahan lampu. Hidup/mati aja gampang (saklar). Tapi gimana mau bikin 50% terang? Solusinya: **saklar ON/OFF 50.000 kali per detik**, dengan perbandingan 50% nyala 50% mati. Mata kita lihatnya jadi "50% terang".

Itu PWM. Motor DC juga sama — kita kasih sinyal kotak (ON/OFF) cepet banget, motor baca rata-ratanya sebagai tegangan analog.

| Duty Cycle | Tegangan rata-rata | Kecepatan motor |
|------------|-------------------|-----------------|
| 0% (0) | 0V | Stop |
| 50% (128) | ~6V | Setengah |
| 100% (255) | ~12V | Full |

**Kenapa LEDC dan bukan `analogWrite()`?**

Arduino biasa pake `analogWrite()` yang pake timer internal. Di ESP32, sistem PWM-nya beda — namanya **LEDC** (LED Controller). Lebih fleksibel:
- Bisa atur frekuensi sendiri (di kode: 20kHz biar gak kedengeran dengung)
- 16 channel independen
- Resolusi bisa 8-16 bit

**3 baris kode itu artinya:**
```
ledcSetup(chanM1, freq, res)   → "Channel 8, tolong siapkan sinyal PWM 20kHz dengan 256 tingkat"
ledcAttachPin(ENA_M1, chanM1)  → "Channel 8, hubungkan outputnya ke pin fisik ENA_M1 (GPIO 25)"
ledcWrite(chanM1, value)       → "Channel 8, set duty cycle ke value (0-255)"
```

3 motor × 3 channel = 3 PWM output buat ngatur kecepatan tiap roda.

---

### 4. Callback — Otak Komunikasi ESP32

```cpp
void callback(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (int i = 0; i < length; i++) msg += (char)payload[i];

    if (strcmp(topic, topic_drive_vector) == 0) {
        // Parse "0.50,-0.30" → vx=0.5, vy=-0.3
        int comma = msg.indexOf(',');
        float vx = msg.substring(0, comma).toFloat();
        float vy = msg.substring(comma + 1).toFloat();
        
        kinematikaOmni(vx, vy, omega);  // Hitung + gerakin motor
    }
    else if (strcmp(topic, topic_action_kick) == 0) {
        digitalWrite(KICK_PIN, HIGH);   // Nyalakan solenoid
        delay(100);
        digitalWrite(KICK_PIN, LOW);    // Matikan solenoid
    }
}
```

**Cara kerja:**
1. **Callback = event handler.** Setiap ada pesan masuk di TOPIC mana pun yang di-subscribe, fungsi ini dipanggil. Parameter `topic` kasih tahu topiknya, `payload` isi datanya.
2. **Pencocokan topik** — dicek pake `strcmp()`. Kalau topiknya `robot/drive/vector`, parse CSVnya. Kalau `robot/action/kick`, tendang.
3. **Parse payload** — data dari frontend dikirim sebagai string CSV "vx,vy". Dipisah koma, dikonversi ke float.
4. **Efek langsung** — fungsi ini bisa langsung gerakin motor, nyalain solenoid, dll. Gak perlu antri.
5. **Non-blocking** — fungsi harus cepet selesai biar gak ngehalang pesan lain. Jangan pake `delay()` di sini (kecuali untuk kick yang cuma 100ms).

Ini inti dari **decoupling**: frontend cukup kirim string ke topic, ESP32 urus sisanya. Mereka gak perlu tahu keberadaan satu sama lain.

---

### 5. setMotor — Jembatan antara logika dan hardware

```cpp
void setMotor(int m1, int m2, int m3) {
    // M1 (Kiri)
    if (m1 == 0) {
        digitalWrite(IN1_M1, LOW);
        digitalWrite(IN2_M1, LOW);     // Dua-duanya LOW → motor berhenti
    } else {
        digitalWrite(IN1_M1, m1 >= 0 ? HIGH : LOW);   // Arah maju/mundur
        digitalWrite(IN2_M1, m1 >= 0 ? LOW : HIGH);
    }
    ledcWrite(chanM1, abs(m1));        // Kecepatan = nilai absolut
    
    // ...sama untuk M2 (Kanan) dan M3 (Belakang)
}
```

**Cara kerja H-Bridge (driver motor L298N):**

| IN1 | IN2 | Motor |
|-----|-----|-------|
| LOW | LOW | Stop |
| HIGH | LOW | Maju |
| LOW | HIGH | Mundur |
| HIGH | HIGH | Brake (jangan dipake) |

**Penjelasan kode:**
- Input `m1` bisa -255 sampai +255
- Tanda positif/negatif = arah (maju/mundur) → diatur lewat IN1, IN2
- Nilai absolut = kecepatan → diatur lewat PWM (ledcWrite)
- Kalau `m1` = 0, dua-duanya LOW biar motor stop (bukan brake)

**Kenapa dipisah?** Biar logika gerak (kinematika) pisah dari hardware. Fungsi di atasnya cuma perlu bilang "motor 1 = 128, motor 2 = -255" — urusan pin mana HIGH/LOW diurus `setMotor`.

---

### 6. Kinematika Omni 3 Roda

```cpp
void kinematikaOmni(float vx, float vy, float omega) {
    // M1=150° (kiri), M2=30° (kanan), M3=270° (belakang)
    float v1 = (-1.0 * vx) + (0.866 * vy) + omega;
    float v2 = (-1.0 * vx) - (0.866 * vy) + omega;
    float v3 = vx + omega;

    int s1 = constrain(v1 * 255, -255, 255);
    int s2 = constrain(v2 * 255, -255, 255);
    int s3 = constrain(v3 * 255, -255, 255);

    setMotor(s1, s2, s3);
}
```

**Apa yang dilakuin?** Ini map-in input joystick (vx, vy) jadi kecepatan 3 motor individual.

**Coba contoh geser kanan (vx=1.0, vy=0, omega=0):**
```
v1 = -1.0 + 0 + 0 = -1.0  → Kiri mundur
v2 = -1.0 - 0 + 0 = -1.0  → Kanan mundur
v3 =  1.0 + 0 + 0 =  1.0  → Belakang maju
```
Tiga roda barengan hasilin resultan gaya ke kanan → robot crab ke kanan.

**Contoh maju (vx=0, vy=-1.0, omega=0):**
```
v1 = 0 - 0.866 = -0.866  → Kiri mundur
v2 = 0 + 0.866 =  0.866  → Kanan maju
v3 = 0                   → Belakang stop
```
Roda kiri mundur + kanan maju = robot jalan maju (karena orientasi roda 120°).

**Fungsi `constrain`** — ngejepit nilai biar gak kelebihan (-255 sampai 255) karena motor cuma bisa terima range itu.

---

### 7. Safety Watchdog

```cpp
unsigned long lastCmdTime = 0;

void loop() {
    // ... setiap kali ada perintah masuk:
    lastCmdTime = millis();

    // Safety: auto-stop kalau >1 detik gak ada perintah
    if (millis() - lastCmdTime > 1000) {
        setMotor(0, 0, 0);  // Semua motor berhenti
    }
}
```

**Kenapa penting?**
- WiFi/MQTT bisa putus kapan aja
- Koneksi broker bisa drop
- User aplikasi bisa tiba-tiba keluar
- Tanpa watchdog, robot akan terus jalan dengan指令 terakhir → bisa nabrak

**`millis()`** = timer internal ESP32 yang ngitung milidetik sejak nyala. Dengan ngurangin `lastCmdTime` dari `millis()` sekarang, kita tahu udah berapa lama sejak perintah terakhir. Kalau >1000ms (1 detik), semua motor di-stop.

Ini **fail-safe**: kalau komunikasi putus, robot berhenti sendiri. Safety dulu, baru fitur.

---

### 8. DEV vs PRODUCTION Topic Config (Frontend)

```ts
// topicConfig.ts
const env = (import.meta.env.VITE_ENV ?? "development").toLowerCase();
export const IS_PRODUCTION = env === "production";

export const DEV_TOPICS = {
  move: "robot/drive/vector",
  kick: "robot/action/kick",
  dribble: "robot/action/dribble",
  sensor: "robot/status/ultrasonic",
};

export const PROD_TOPICS = {
  move: "robot/gerak/vector",
  kick: "robot/tendang",
  dribble: null,           // PROD tidak ada fitur dribble
  sensor: "robot/jarak",
};

export const TOPICS = IS_PRODUCTION ? PROD_TOPICS : DEV_TOPICS;
```

**Cara kerja:**
- `VITE_ENV` diambil dari file `.env` (environment variable Vite)
- Kalau `VITE_ENV=production`, pake `PROD_TOPICS` (topic sesuai `finalCode.ino.bak`)
- Kalau tidak ada / `development`, pake `DEV_TOPICS` (topic sesuai `main.cpp` saat ini)
- Semua komponen pake `TOPICS.move`, `TOPICS.kick` — ganti env, otomatis ganti topic

```ts
// ControllerDashboard.tsx — pakenya gini:
publish(TOPICS.move, "0.50,-0.30");    // otomatis robot/drive/vector atau robot/gerak/vector
publish(TOPICS.kick, "KICK");          // otomatis robot/action/kick atau robot/tendang
```

**Dribble null** — di PROD, `TOPICS.dribble = null`. Komponen UI dribble otomatis ilang karena:
```tsx
{TOPICS.dribble && <DribblerCard />}
```

---

### 9. Loop Utama ESP32 — Semua berjalan di sini

```cpp
void loop() {
    if (!client.connected()) reconnect();  // 1. Cek koneksi MQTT
    client.loop();                          // 2. Proses pesan masuk dari broker

    // 3. Safety watchdog
    if (millis() - lastCmdTime > 1000) {
        setMotor(0, 0, 0);
    }

    // 4. Telemetry 10Hz — kirim data ultrasonik
    if (millis() - lastTeleTime > 100) {
        lastTeleTime = millis();
        // ...baca sensor, publish ke broker
    }
}
```

**Loop()** = fungsi yang dipanggil terus menerus oleh Arduino framework. Efeknya seperti `while(true)`.

**4 tugas utama tiap siklus:**
1. **Reconnect** — kalau MQTT putus, coba sambungin lagi (dengan delay 5 detik biar gak spam)
2. **client.loop()** — fungsi Krusial. Ini yang ngecek "ada pesan masuk? ada yang mau dikirim?". **Kalau lupa panggil ini, ESP32 gak akan pernah nerima perintah MQTT.** Ini jembatan antara PubSubClient dan jaringan.
3. **Watchdog** — berhentiin motor kalau >1 detik gak ada perintah
4. **Telemetry** — tiap 100ms (10Hz) baca sensor ultrasonik, kirim jarak ke broker. Frekuensi 10Hz cukup buat real-time tanpa banjir data.

```
 ┌─────────────────────────────────────────────┐
 │                loop() berulang               │
 │  ┌──────────┐  ┌──────────┐  ┌───────────┐  │
 │  │ reconnect │→│client.  │→│ watchdog  │→│ telemetry │  │
 │  │ (jika     │  │loop()   │  │ (1s)     │  │ (10Hz)   │  │
 │  │  putus)   │  │         │  │           │  │          │  │
 │  └──────────┘  └──────────┘  └───────────┘  └──────────┘  │
 └─────────────────────────────────────────────────────────────┘
```

---

## Q&A

**Pertanyaan umum:**
- **Q:** Apakah harus konek internet? **A:** Iya, karena broker di cloud. Tapi bisa pake broker lokal (Mosquitto) di jaringan yang sama.
- **Q:** Berapa jarak maksimal? **A:** Selama robot & pengontrol sama-sama konek internet, bisa dari mana aja.
- **Q:** Gimana kalau broker mati? **A:** Watchdog akan stop motor dalam 1 detik (fail-safe).
- **Q:** Bisa multi-user? **A:** Bisa! Semua yang subscribe topic yang sama akan dapet data yang sama. Tapi perintah gerak dari banyak pengirim akan tabrakan — perlu logika prioritasi.
