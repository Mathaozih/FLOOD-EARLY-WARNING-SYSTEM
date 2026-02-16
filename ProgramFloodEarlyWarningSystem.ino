#include <Wire.h> // Diperlukan untuk komunikasi I2C
#include <LiquidCrystal_I2C.h> // Library untuk LCD I2C
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// --- Pin untuk Komunikasi I2C LCD ---
// Pin I2C default pada ESP32 biasanya:
#define I2C_SDA_PIN 21 // Pin SDA untuk LCD I2C
#define I2C_SCL_PIN 22 // Pin SCL untuk LCD I2C

// Konfigurasi LCD I2C
// Ganti alamat 0x27 jika LCD Anda menggunakan alamat lain (misalnya 0x3F)
// Ganti 16, 2 jika ukuran LCD Anda berbeda (misalnya 20, 4 untuk 20x4)
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// Konfigurasi WiFi dan Telegram Bot
const char* ssid = "Voltase_Homestay"; // Ganti dengan nama WiFi Anda
const char* password = "asramalistrik"; // Ganti dengan password WiFi Anda
const char* botToken = "8062922768:AAHWFatUwufqVur5pEYmTWmODK03-g28KF0"; // Ganti dengan token Bot Telegram Anda
const char* chat_id = "6200742914"; // Ganti dengan Chat ID Telegram Anda

WiFiClientSecure client;
UniversalTelegramBot bot(botToken, client);

// Pin Sensor dan Aktuator LED diskrit
#define SENSOR_PIN 34    // Pin analog untuk sensor water level
#define LED_HIJAU 14     // Pin LED Hijau (Kondisi Aman/Normal - Berkelip)
#define LED_KUNING 12    // Pin LED Kuning (Kondisi Waspada & Awas)
#define LED_MERAH 13     // Pin LED Merah (Kondisi Bahaya)
#define BUZZER 27        // Pin Buzzer

// Ambang Batas Ketinggian Air (Sesuaikan dengan kalibrasi sensor Anda)
const int THRESHOLD_NORMAL_MAX = 1600;
const int THRESHOLD_SIAGA_MAX = 1850;
const int THRESHOLD_AWAS_MAX = 2200;

// Variabel untuk status dan notifikasi
int lastLevelSent = -1; // Untuk logika Telegram
unsigned long lastSendTime = 0;
const unsigned long telegramInterval = 30000;

// Variabel untuk LED Hijau Berkelip
unsigned long greenLedPreviousMillis = 0;
const long greenLedBlinkInterval = 500;
bool greenLedState = LOW;

// Variabel untuk melacak status terakhir yang ditampilkan di LCD agar tidak sering update
int lastDisplayedLevelOnLCD = -1; 

void setup() {
  Serial.begin(115200);

  // Inisialisasi I2C (untuk LCD)
  // Jika Anda ingin menggunakan pin I2C non-default, Anda bisa memanggil Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  // Namun, untuk pin default ESP32 (GPIO 21 untuk SDA, GPIO 22 untuk SCL), Wire.begin() saja sudah cukup.
  Wire.begin(); 
  
  // Inisialisasi LCD
  lcd.init();      // Inisialisasi LCD
  lcd.backlight(); // Nyalakan backlight LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Flood Early");
  lcd.setCursor(0, 1);
  lcd.print("Warning Sistem"); // Ganti dengan versi atau nama sistem Anda
  delay(3000); 

  //Slide ke 2
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Kelas V2");
  lcd.setCursor(0, 1);
  lcd.print("Kelompok 3"); // Ganti dengan versi atau nama sistem Anda
  delay(3000); 

  pinMode(LED_HIJAU, OUTPUT);
  pinMode(LED_KUNING, OUTPUT);
  pinMode(LED_MERAH, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(LED_HIJAU, LOW); // Mulai dengan LED hijau mati
  digitalWrite(LED_KUNING, LOW);
  digitalWrite(LED_MERAH, LOW);
  digitalWrite(BUZZER, LOW);

  // Koneksi ke WiFi
  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(ssid);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("WiFi Connect...");
  
  WiFi.begin(ssid, password);
  int wifi_dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(wifi_dots, 1); // Menampilkan titik-titik di baris kedua LCD
    lcd.print(".");
    wifi_dots++;
    if (wifi_dots > 15) { // Jika sudah 16 titik, reset titik di LCD
        lcd.setCursor(0,1);
        lcd.print("                "); // Bersihkan baris kedua
        wifi_dots = 0;
    }
    // Anda bisa menambahkan timeout di sini jika diinginkan,
    // agar tidak terjebak selamanya jika WiFi tidak tersedia
  }
  Serial.println("\nWiFi tersambung");
  Serial.print("Alamat IP: ");
  Serial.println(WiFi.localIP());

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("WiFi Terhubung!");
  lcd.setCursor(0,1);
  lcd.print(WiFi.localIP());
  delay(2500);


  client.setInsecure(); 
  bot.sendMessage(chat_id, "✅ Sistem Peringatan Dini Banjir (FEWS) berbasis ESP32 aktif!", "");
  
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Sistem Siap!");
  lcd.setCursor(0,1);
  lcd.print("Monitoring Air..");
}

// Fungsi untuk menampilkan pesan ke LCD dan padding untuk membersihkan sisa baris
void printToLCD(int col, int row, String text, int lcdWidth = 16) {
    lcd.setCursor(col, row);
    lcd.print(text);
    // Beri spasi untuk menghapus sisa karakter di baris itu jika text lebih pendek dari lcdWidth
    for (int i = text.length(); i < lcdWidth; i++) {
        lcd.print(" ");
    }
}

void loop() {
  int sensorValue = analogRead(SENSOR_PIN);
  // Komentari Serial.print di bawah jika tidak ingin terlalu banyak output di Serial Monitor
  // Serial.print("Nilai Sensor ADC: "); 
  // Serial.println(sensorValue);

  // Reset LED Kuning, Merah, dan Buzzer di awal loop. LED Hijau dikelola terpisah.
  digitalWrite(LED_KUNING, LOW);
  digitalWrite(LED_MERAH, LOW);
  digitalWrite(BUZZER, LOW);

  int currentSystemLevel = 0; 
  String telegramMessage = "";
  String lcdStatusLine1 = "";
  String lcdStatusLine2Prefix = ""; // Teks sebelum nilai sensor di LCD

  // 1. Penentuan level kondisi berdasarkan nilai sensor
  if (sensorValue < THRESHOLD_NORMAL_MAX) {
    currentSystemLevel = 0; // Normal/Aman
    lcdStatusLine1 = "NORMAL";
    lcdStatusLine2Prefix = "Air Aman";
  } else if (sensorValue < THRESHOLD_SIAGA_MAX) {
    currentSystemLevel = 1; // Waspada/Siaga
    lcdStatusLine1 = "WASPADA";
    lcdStatusLine2Prefix = "Air Naik";
  } else if (sensorValue < THRESHOLD_AWAS_MAX) {
    currentSystemLevel = 2; // Awas
    lcdStatusLine1 = "AWAS!";
    lcdStatusLine2Prefix = "Air Tinggi";
  } else {
    currentSystemLevel = 3; // Bahaya
    lcdStatusLine1 = "BAHAYA!!!";
    lcdStatusLine2Prefix = "EVAKUASI!";
  }

  // Hanya update LCD jika status level berubah atau ini adalah loop pertama
  if (currentSystemLevel != lastDisplayedLevelOnLCD || lastDisplayedLevelOnLCD == -1) {
    printToLCD(0, 0, "Status: " + lcdStatusLine1);
    
    // Format baris kedua untuk menyertakan nilai sensor
    char lcdLine2Buffer[17]; // 16 karakter + null terminator
    snprintf(lcdLine2Buffer, sizeof(lcdLine2Buffer), "%s (%d)", lcdStatusLine2Prefix.c_str(), sensorValue);
    printToLCD(0, 1, lcdLine2Buffer);
    
    lastDisplayedLevelOnLCD = currentSystemLevel; // Update status terakhir yang ditampilkan
    // Baris Serial.print di bawah ini bisa diaktifkan untuk debug jika LCD tidak menampilkan sesuai harapan
    // Serial.print("LCD Updated - Status: " + lcdStatusLine1 + ", Detail: " + lcdStatusLine2Prefix + " (" + String(sensorValue) + ")\n");
  }


  // 2. Logika Aktuator berdasarkan currentSystemLevel
  
  // LED Hijau: Berkelip jika Normal (Level 0), Mati jika tidak.
  if (currentSystemLevel == 0) {
    unsigned long currentMillis = millis();
    if (currentMillis - greenLedPreviousMillis >= greenLedBlinkInterval) {
      greenLedPreviousMillis = currentMillis;
      greenLedState = !greenLedState;
      digitalWrite(LED_HIJAU, greenLedState);
    }
    // Komentari Serial.println jika tidak ingin terlalu banyak output status di Serial Monitor
    // Serial.println("Status: NORMAL (Air aman terkendali) - LED Hijau Berkelip"); 
  } else {
    digitalWrite(LED_HIJAU, LOW); // Pastikan LED Hijau mati jika bukan level Normal
  }

  // LED Kuning: Menyala untuk Waspada (Level 1) dan Awas (Level 2).
  // Buzzer: Menyala untuk Awas (Level 2).
  if (currentSystemLevel == 1) { // Waspada
    digitalWrite(LED_KUNING, HIGH);
    // Serial.println("Status: WASPADA / SIAGA (Peringatan Level 1 - Ketinggian air meningkat)");
  } else if (currentSystemLevel == 2) { // Awas
    digitalWrite(LED_KUNING, HIGH);
    digitalWrite(BUZZER, HIGH); 
    // Serial.println("Status: AWAS (Peringatan Level 2 - Ketinggian air tinggi!)");
  }

  // LED Merah dan Buzzer: Menyala untuk Bahaya (Level 3).
  // Notifikasi Telegram juga dikirim untuk Level 3.
  if (currentSystemLevel == 3) { // Bahaya
    digitalWrite(LED_MERAH, HIGH);
    digitalWrite(BUZZER, HIGH); 
    // Menambahkan nilai sensor ke pesan Telegram
    telegramMessage = "🚨 PERINGATAN LEVEL 3: BAHAYA! Ketinggian air sangat tinggi (" + String(sensorValue) + "). Segera lakukan evakuasi! 🚨";
    // Serial.println("Status: BAHAYA (Peringatan Level 3 - Ketinggian air SANGAT TINGGI, EVAKUASI!)");

    // Kirim notifikasi Telegram HANYA untuk kondisi BAHAYA (Level 3)
    // jika terjadi perubahan dari level lain ke Level 3, serta interval terpenuhi
    if (lastLevelSent != 3) { 
      if (millis() - lastSendTime > telegramInterval) {
        bot.sendMessage(chat_id, telegramMessage, "");
        Serial.println("Notifikasi Telegram BAHAYA terkirim: " + telegramMessage);
        lastSendTime = millis(); // Perbarui waktu pengiriman terakhir
      } else {
        Serial.println("Status BAHAYA terdeteksi, namun interval pengiriman Telegram belum terpenuhi.");
      }
    }
  }

  // 3. Perbarui status level terakhir yang terproses untuk logika Telegram
  if (currentSystemLevel != lastLevelSent) {
    // Serial.print("Perubahan Level (untuk Telegram): Dari "); // Bisa diaktifkan untuk debug
    // Serial.print(lastLevelSent);
    // Serial.print(" ke ");
    // Serial.println(currentSystemLevel);
    lastLevelSent = currentSystemLevel;
  }

  delay(100); // Delay loop utama agar kedipan LED lebih responsif dan pembacaan sensor lebih sering.
}