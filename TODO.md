Kerja bagus, Ghal! Langkah memigrasikan *broker* lokal (`test.mosquitto.org`) ke infrastruktur *cloud* yang di-hosting (*managed service*) seperti HiveMQ adalah praktik yang sangat tepat, apalagi ini sejalan dengan arsitektur *cloud deployment* modern.

Untuk menghubungkan ekosistem ini (ESP32 ➔ HiveMQ Cloud ➔ Next.js/React Frontend), ada satu perbedaan utama: **HiveMQ Cloud mewajibkan koneksi terenkripsi (TLS/SSL) dan Autentikasi (Username/Password).** Di sisi CachyOS kamu, struktur *folder* PlatformIO-mu sudah benar, **tetapi ada satu jebakan kompilasi:** PlatformIO akan mengompilasi semua file `.cpp` dan `.ino` di dalam folder `src/`. Jika `check.cpp`, `lancah.ino`, dan `main.cpp` semuanya memiliki fungsi `setup()` dan `loop()`, proses *build* akan gagal (error *multiple definition*). **Solusi:** Hapus atau ubah ekstensi file yang tidak dipakai menjadi `.txt` sementara waktu, dan fokus saja pada `main.cpp`.

Berikut adalah kode pembaruan untuk mengintegrasikan semuanya.

---

### 1. Update Frontend (React / Next.js)

Kamu perlu menambahkan kredensial *username* dan *password* ke dalam *hook* MQTT, serta mengubah URL untuk menggunakan protokol WebSocket Secure (`wss`) pada *port* 8884.

**Update `.env`:**

```env
VITE_MQTT_URL=wss://8bbf3f5e18ea48e0ba67d15832b06403.s1.eu.hivemq.cloud:8884/mqtt
VITE_ROBOT_MOVE_TOPIC=robot/gerak

```

**Update `useMqttPublisher.ts`:**
Tambahkan `username` dan `password` ke dalam `clientOptions`.

```typescript
// ... (kode sebelumnya tetap sama)

  useEffect(() => {
    const clientOptions: IClientOptions = {
      clientId: `${clientIdPrefix}-${Math.random().toString(16).slice(2)}`,
      clean: true,
      connectTimeout: 5000,
      reconnectPeriod: 2000,
      // Tambahkan kredensial HiveMQ Cloud di sini
      username: "ghalytsarhivemq", 
      password: "Ember1233", // Idealnya ditarik dari env: import.meta.env.VITE_MQTT_PASSWORD
      ...options,
    };

    const client = mqtt.connect(url, clientOptions);
// ... (kode setelahnya tetap sama)

```

---

### 2. Update Backend / ESP32 (`src/main.cpp`)

ESP32 tidak bisa lagi menggunakan `WiFiClient` biasa. Kita harus menggunakan `WiFiClientSecure` untuk berkomunikasi lewat *port* 8883 (TLS). Untuk mempermudah pengetesan awal tanpa ribet memasukkan sertifikat *Root CA*, kita gunakan fungsi `espClient.setInsecure()`.

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h> // Wajib untuk HiveMQ Cloud
#include <PubSubClient.h>

// ==========================================
// PENGATURAN WI-FI & HIVEMQ CLOUD
// ==========================================
const char* ssid = "SSID_WIFI_KAMU";        
const char* password = "PASSWORD_WIFI_KAMU"; 

// Kredensial HiveMQ
const char* mqtt_server = "8bbf3f5e18ea48e0ba67d15832b06403.s1.eu.hivemq.cloud"; 
const int mqtt_port = 8883; // Port TLS
const char* mqtt_user = "ghalytsarhivemq";
const char* mqtt_pass = "Ember1233";

const char* topic_gerak = "robot/gerak"; 
const char* topic_kecepatan = "robot/kecepatan"; 

WiFiClientSecure espClient; // Gunakan Client Secure
PubSubClient client(espClient);

// ==========================================
// DEKLARASI PIN & VARIABEL
// ==========================================
// ... (Pertahankan deklarasi PIN IN1, IN2, ENA dan fungsi motor yang kamu miliki sebelumnya)
int currentSpeed = 255; 

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Menghubungkan ke Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWi-Fi Terhubung!");
}

void callback(char* topic, byte* message, unsigned int length) {
  // ... (Pertahankan fungsi callback yang kamu miliki sebelumnya)
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Mencoba terhubung ke HiveMQ Cloud...");
    String clientId = "RobotKRSBI-ESP32-";
    clientId += String(random(0xffff), HEX);
    
    // Perubahan krusial: Memasukkan username dan password saat connect
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("Terhubung!");
      client.subscribe(topic_gerak);
      client.subscribe(topic_kecepatan);
    } else {
      Serial.print("Gagal, rc=");
      Serial.print(client.state());
      Serial.println(" Coba lagi dalam 5 detik");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  // ... (Pertahankan setup pinMode dan ledcAttach milikmu)

  setup_wifi();
  
  // Mengabaikan verifikasi sertifikat SSL untuk kemudahan testing
  espClient.setInsecure(); 
  
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); 
}

// ... (Pertahankan blok fungsi pergerakan omni: maju(), mundur(), dll)

```

---

### 3. File `PRD-update.md`

Simpan teks ini sebagai dokumentasi *Product Requirements Document* proyekmu.

```markdown
# Product Requirements Document (PRD) Update: Migrasi Infrastruktur MQTT

## 1. Deskripsi Pembaruan
Sistem telemetri dan kendali robot KRSBI Beroda dimigrasikan dari broker *open-source* lokal (`test.mosquitto.org`) menuju infrastruktur *cloud-managed* **HiveMQ Cloud** untuk menjamin stabilitas, latensi rendah, dan keamanan data melalui protokol enkripsi TLS/SSL.

## 2. Kredensial & Endpoint Infrastruktur Baru
Semua *client* (ESP32 dan Web Frontend) diwajibkan menggunakan kredensial autentikasi terpusat.
* **Cluster URL:** `8bbf3f5e18ea48e0ba67d15832b06403.s1.eu.hivemq.cloud`
* **Username:** `ghalytsarhivemq`
* **Password:** `Ember1233`
* **Permissions:** `PUBLISH_SUBSCRIBE` (Active)

## 3. Requirement Pembaruan: Frontend (React/Web Client)
* **Protokol:** Wajib menggunakan *Secure WebSockets* (`wss://`).
* **Port:** Endpoint komunikasi WebSocket dialihkan ke port `8884` dengan suffix `/mqtt`.
* **Kredensial:** Modul penyambung (hook `useMqttPublisher`) harus di-refactor untuk menyisipkan objek `username` dan `password` pada konfigurasi `IClientOptions` dari library `mqtt.js`.

## 4. Requirement Pembaruan: Backend (Firmware ESP32)
* **Library:** Mengganti *class* `WiFiClient` standar menjadi `WiFiClientSecure` untuk mendukung *handshake* TLS.
* **Port:** Endpoint komunikasi native MQTT dialihkan ke port `8883`.
* **Keamanan:** Mengimplementasikan fungsi `setInsecure()` pada *client* Wi-Fi selama fase prototipe untuk membypass verifikasi *Root CA Certificate*.
* **Environment:** File sumber kompilasi pada folder `src/` harus diisolasi (hanya boleh terdapat satu fungsi `setup()` dan `loop()` aktif) untuk menghindari *build failure* pada ekosistem PlatformIO.

## 5. Arsitektur Topik (Saat Ini)
Sistem masih mendukung transmisi payload sederhana (Plain Text).
* `robot/gerak` (Command: arah navigasi diskrit)
* `robot/kecepatan` (Command: integer 0-255 untuk PWM modulasi)

```