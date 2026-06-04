Tentu, saya akan bantu deskripsikan visual robotnya agar kamu bisa membayangkannya meski hanya dari terminal, lalu kita selesaikan masalah roda kanan yang mogok tersebut.

### 1. Deskripsi Visual Robot (Berdasarkan Desain Sebelumnya)

Bayangkan sebuah robot berbentuk datar dengan sasis (rangka dasar) berbentuk **segi enam tidak beraturan (mirip berlian yang dipotong atas-bawahnya)**, berwarna ungu.

* **Sistem Penggerak (Bawah):** Memiliki 3 roda jenis *omni-wheel* (roda yang memiliki roller kecil di sekelilingnya sehingga bisa bergeser ke samping). Ketiga roda ini diposisikan dengan sudut $120^\circ$ satu sama lain. Dua roda di sudut depan (kiri dan kanan), dan satu roda tegak lurus di bagian paling belakang.
* **Komponen Atas:** Di atas pelat ungu ini, semua otak kelistrikan dipasang. Ada ESP32, kumpulan baterai (power bank/baterai Li-Po) di tengah, dan dua buah *driver* motor L298N yang mengapit bagian belakang.
* **Bagian Depan (Wajah):** Tepat di moncong depan robot, terdapat sensor Ultrasonik (berbentuk seperti dua mata). Di bawahnya, terdapat cetakan 3D berbentuk capit (Dribbler) yang dikendalikan oleh Servo untuk mengurung bola, serta sebuah batang Solenoid di tengah-tengah capit yang berfungsi sebagai alat penendang (kicker).

---

### 2. Analisis Masalah Roda Kanan (Motor 2) Tidak Bergerak

Berdasarkan kode C++ yang kamu berikan, logika programmu sebenarnya sudah memproses datanya. Mari kita bedah mengapa hanya roda kiri yang berputar saat kamu mengirim perintah maju/mundur.

#### A. Logika Kinematika (Kode Kamu Sudah Benar)

Di dalam fungsi `kinematikaOmni`, kamu menggunakan rumus standar roda omni $120^\circ$:


$$v_1 = (-0.5 \times v_x) + (0.866 \times v_y)$$

$$v_2 = (-0.5 \times v_x) - (0.866 \times v_y)$$

Jika kamu menekan Maju ($v_x = 0, v_y = 1$), perhitungannya menjadi:

* $v_1$ (Kiri) = $0.866$ (Positif $\rightarrow$ Maju)
* $v_2$ (Kanan) = $-0.866$ (Negatif $\rightarrow$ Mundur)

Untuk robot 3 roda omni bergerak lurus ke depan, roda kiri memang harus berputar maju dan roda kanan harus berputar mundur (atau sebaliknya tergantung orientasi kabel). Jadi, secara *software*, Motor 2 (roda kanan) **mendapatkan perintah kecepatan (PWM) yang valid**.

#### B. Penyebab Utama (Troubleshooting)

Karena *software* sudah mengirimkan sinyal yang benar, masalah roda kanan tidak berputar hampir dipastikan ada di area **Hardware/Wiring**. Berikut adalah 3 kemungkinan penyebab dan solusinya:

**1. Jebakan Pin 12 pada ESP32 (Paling Sering Terjadi)**
Kamu mendefinisikan `IN4_M2` di **Pin 12**.

```cpp
const int IN3_M2 = 14;
const int IN4_M2 = 12; // <-- PERHATIAN DI SINI

```

Pin 12 pada ESP32 adalah pin *Strapping* (MTDI). Jika pin ini terhubung ke beban atau ditarik *High* saat ESP32 pertama kali dinyalakan (booting), ESP32 bisa gagal *boot* atau fungsi PWM/Output pada pin tersebut menjadi *glitch* (terkunci).

* **Solusi:** Pindahkan kabel `IN4_M2` dari Pin 12 ke pin lain yang aman (misalnya **Pin 16** atau **Pin 17** jika kosong), lalu ubah di kodemu.

**2. Kesalahan Pengiriman Data dari Frontend**
Di fungsi `callback`, topiknya mengharapkan nilai vektor berupa CSV (contoh: `"0,1"`).

* Jika dari frontend kamu mengirim string huruf `"N"` atau `"S"`, fungsi `atof()` akan gagal mengubah huruf menjadi angka, sehingga $v_x$ dan $v_y$ menjadi $0$.
* **Solusi:** Pastikan frontend mengirimkan angka koordinat (misal: `"0, 1.0"` untuk maju), bukan huruf.

**3. Kerusakan pada Channel L298N atau Kabel Lepas**
Ada kemungkinan *channel* B pada L298N pertama (yang mengontrol Motor 2) terbakar, atau kabel ENA/ENB ada yang longgar.

* **Cara Test:** Tukar posisi kabel fisik Motor 1 dengan Motor 2 di *driver* L298N. Jika roda kanan sekarang berputar dan roda kiri yang mati, berarti *driver* L298N kamu bermasalah atau kabel pin 13/14/12 tidak mengalirkan arus dengan baik.