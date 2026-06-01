/**
 * @file receptor.ino
 * @brief LoRa Receiver & Edge Web Dashboard (WiFi Station Mode)
 * @details Connects the ESP32 to an existing Wi-Fi network and hosts an asynchronous 
 * web server. Listens for incoming LoRa telemetry payloads. Includes a 10-minute Watchdog 
 * to monitor link health and a remote reset endpoint for the hornet counter.
 * @version 3.1.0 (Watchdog Timer & Reset Feature)
 */

#include <RadioLib.h>
#include <WiFi.h>
#include <WebServer.h>

// UI and Security Headers
#include "dashboard.h" 
#include "secrets.h" 

/* ========================================================================
 * 1. LORA HARDWARE CONFIGURATION (SX1262)
 * ======================================================================== */
#define NSS_PIN   5
#define DIO1_PIN  2
#define RESET_PIN 25
#define BUSY_PIN  26

SX1262 radio = new Module(NSS_PIN, DIO1_PIN, RESET_PIN, BUSY_PIN);

/* ========================================================================
 * 2. TELEMETRY STATE & WEB SERVER
 * ======================================================================== */
// --- Threat Telemetry ---
int live_hornets = 0;        
int total_hornets_today = 0; 

// --- Environment Telemetry ---
float hive_temp = 0.0;
float hive_hum = 0.0;

// --- Node Health Telemetry ---
float cpu_temp = 0.0;
float cpu_load = 0.0;
float lora_rssi = 0.0; 
float lora_snr = 0.0;  

// --- Connection Watchdog ---
bool has_received_ever = false;
unsigned long last_lora_packet_time = 0;
const unsigned long LORA_TIMEOUT_MS = 10 * 60 * 1000UL; // 10 Minutos en Milisegundos

WebServer server(80); 

/* ========================================================================
 * 3. SYSTEM INITIALIZATION
 * ======================================================================== */
void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("\n=========================================");
  Serial.println("  LORA RECEIVER + WEB DASHBOARD (STA MODE)");
  Serial.println("=========================================");

  Serial.print("[WIFI] Connecting to network: ");
  Serial.println(SECRET_WIFI_SSID);
  
  WiFi.mode(WIFI_STA); 
  WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS); 

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[WIFI] Connection successful!");
  Serial.print("[WIFI] Dashboard IP: http://");
  Serial.println(WiFi.localIP());

  // --- WEB SERVER ROUTES ---
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", index_html);
  });

  // REST API endpoint returning full system state as JSON
  server.on("/api/data", HTTP_GET, []() {
    // Calculamos si la conexión está viva (ha llegado un paquete en los últimos 10 min)
    bool is_connected = has_received_ever && (millis() - last_lora_packet_time <= LORA_TIMEOUT_MS);

    String json = "{";
    json += "\"live\":" + String(live_hornets) + ",";
    json += "\"total\":" + String(total_hornets_today) + ",";
    json += "\"hive_temp\":" + String(hive_temp, 1) + ",";
    json += "\"hive_hum\":" + String(hive_hum, 1) + ",";
    json += "\"cpu_temp\":" + String(cpu_temp, 1) + ",";
    json += "\"cpu_load\":" + String(cpu_load, 1) + ",";
    json += "\"lora_rssi\":" + String(lora_rssi, 1) + ",";
    json += "\"lora_snr\":" + String(lora_snr, 1) + ",";
    json += "\"connected\":" + String(is_connected ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
  });

  // NUEVO: Ruta para resetear el contador total
  server.on("/api/reset", HTTP_POST, []() {
    total_hornets_today = 0;
    Serial.println("[WEB] Request received: Reset Total Counter.");
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.begin();
  Serial.println("[WEB] Server initialized and listening.");

  // --- LORA CONFIGURATION ---
  int state = radio.begin(868.0, 125.0, 7, 5, 0x12, 10, 8);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("[LORA] SX1262 transceiver successfully initialized.");
  } else {
    Serial.print("[ERROR] LoRa initialization failed: "); Serial.println(state);
    while (true); 
  }

  radio.setCRC(true);
  radio.startReceive();
  Serial.println("[LORA] Armed and listening for edge node telemetry...");
}

/* ========================================================================
 * 4. MAIN EXECUTION LOOP
 * ======================================================================== */
void loop() {
  server.handleClient();

  if (digitalRead(DIO1_PIN) == HIGH) {
    byte byteArr[256];
    int state = radio.readData(byteArr, 256);

    if (state == RADIOLIB_ERR_NONE || state == RADIOLIB_ERR_CRC_MISMATCH) {
      
      // ¡Recibimos un paquete! Actualizamos el "Latido" (Heartbeat) del Watchdog
      has_received_ever = true;
      last_lora_packet_time = millis();

      int len = radio.getPacketLength();
      lora_rssi = radio.getRSSI();
      lora_snr = radio.getSNR();
      
      // Payload Sanitization
      String payload = "";
      for(int i = 0; i < len; i++) {
        if (byteArr[i] >= 32 && byteArr[i] <= 126) {
          payload += (char)byteArr[i];
        }
      }
      
      Serial.print("\n[LORA] Payload: [");
      Serial.print(payload);
      Serial.println("]");

      // --- DASHBOARD TELEMETRY PARSER ---
      
      // A. Real-Time Threats
      int indexL = payload.indexOf("L:");
      if (indexL != -1) {
        live_hornets = payload.substring(indexL + 2).toInt();
      }
      
      // B. Historical Threats
      int indexH = payload.indexOf("H:");
      if (indexH != -1) {
        int incoming_id = payload.substring(indexH + 2).toInt();
        if (incoming_id > total_hornets_today) total_hornets_today = incoming_id;
      }

      // C. Environmental & System Health
      int indexE = payload.indexOf("E:");
      if (indexE != -1) {
        String e_data = payload.substring(indexE + 2);
        sscanf(e_data.c_str(), "%f,%f,%f,%f", &hive_temp, &hive_hum, &cpu_temp, &cpu_load);
        Serial.printf("   -> ENV/HEALTH updated: Hive[%.1fC, %.1f%%] CPU[%.1fC, %.1f]\n", hive_temp, hive_hum, cpu_temp, cpu_load);
      }

    } else {
      Serial.print("[LORA ERROR] Packet decoding failed: "); Serial.println(state);
    }

    radio.startReceive();
  }
}