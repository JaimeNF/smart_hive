/**
 * @file receptor.ino
 * @brief LoRa Receiver & Edge Web Dashboard
 * @details Utiliza Interrupciones de Hardware (ISR) en el pin DIO1 para garantizar 
 * que la recepción LoRa (misión crítica) jamás se vea afectada por bloqueos o latencias 
 * en la conexión Wi-Fi o MQTT.
 * @version 3.4.0 (Hardware Interrupts & Non-Blocking Fix)
 */

#include <RadioLib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>

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

// --- NUEVO: BANDERA DE INTERRUPCIÓN HARDWARE ---
// 'volatile' obliga a la CPU a leer esto directamente de la RAM, vital para ISR.
volatile bool receivedFlag = false;

// Esta función se ejecuta a nivel de silicio en cuanto el pin DIO1 sube a HIGH
void IRAM_ATTR setFlag(void) {
  receivedFlag = true;
}

/* ========================================================================
 * 2. TELEMETRY STATE & WEB SERVER
 * ======================================================================== */
int live_hornets = 0;        
int total_hornets_today = 0; 
float hive_temp = 0.0, hive_hum = 0.0;
float cpu_temp = 0.0, cpu_load = 0.0;
float lora_rssi = 0.0, lora_snr = 0.0;  

bool has_received_ever = false;
unsigned long last_lora_packet_time = 0;
const unsigned long LORA_TIMEOUT_MS = 10 * 60 * 1000UL; 

WebServer server(80); 

/* ========================================================================
 * 3. SECURE MQTT CONFIGURATION
 * ======================================================================== */
WiFiClient espClient;
PubSubClient mqtt_client(espClient);
unsigned long last_mqtt_reconnect_attempt = 0;

/* ========================================================================
 * 4. SYSTEM INITIALIZATION
 * ======================================================================== */
void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("\n=========================================");
  Serial.println("  LORA RECEIVER + WEB DASHBOARD (ISR MODE)");
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
  server.on("/", HTTP_GET, []() { server.send(200, "text/html", index_html); });
  
  server.on("/api/data", HTTP_GET, []() {
    bool is_connected = has_received_ever && (millis() - last_lora_packet_time <= LORA_TIMEOUT_MS);
    String json = "{";
    json += "\"live\":" + String(live_hornets) + ", \"total\":" + String(total_hornets_today) + ",";
    json += "\"hive_temp\":" + String(hive_temp, 1) + ", \"hive_hum\":" + String(hive_hum, 1) + ",";
    json += "\"cpu_temp\":" + String(cpu_temp, 1) + ", \"cpu_load\":" + String(cpu_load, 1) + ",";
    json += "\"lora_rssi\":" + String(lora_rssi, 1) + ", \"lora_snr\":" + String(lora_snr, 1) + ",";
    json += "\"connected\":" + String(is_connected ? "true" : "false") + "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/reset", HTTP_POST, []() {
    total_hornets_today = 0;
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });
  server.begin();

  // --- LORA CONFIGURATION ---
  int state = radio.begin(868.0, 125.0, 7, 5, 0x12, 10, 8);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[ERROR] LoRa failed: "); Serial.println(state);
    while (true); 
  }

  // NUEVO: Enlazamos la radio a la interrupción de hardware
  radio.setDio1Action(setFlag);
  radio.setCRC(true);
  radio.startReceive();

  // Reducimos el timeout del TCP para evitar que la red cuelgue el ESP32
  espClient.setTimeout(2); 
  mqtt_client.setServer(SECRET_MQTT_SERVER, 1883);
  
  Serial.println("[LORA] ISR Armed. Listening for edge node telemetry...");
}

/* ========================================================================
 * 5. MAIN EXECUTION LOOP
 * ======================================================================== */
void loop() {
  server.handleClient();
  
  // Mantenimiento de MQTT no bloqueante
  if (!mqtt_client.connected()) {
    if (millis() - last_mqtt_reconnect_attempt > 5000) {
      last_mqtt_reconnect_attempt = millis();
      Serial.print("[MQTT] Reconectando al broker...");
      
      if (mqtt_client.connect("ESP32_LoRa_Receiver", SECRET_MQTT_USER, SECRET_MQTT_PASS)) {
        Serial.println(" ¡Conectado!");
      } else {
        Serial.print(" Fallo, código: "); Serial.println(mqtt_client.state());
      }
    }
  } else {
    mqtt_client.loop();
  }

  // --- NUEVO: LECTURA BASADA EN BANDERAS DE INTERRUPCIÓN ---
  // En lugar de leer el pin físico, miramos si la ISR ha cambiado la variable.
  if (receivedFlag) {
    receivedFlag = false; // Reseteamos la bandera al instante

    byte byteArr[256];
    int state = radio.readData(byteArr, 256);

    if (state == RADIOLIB_ERR_NONE || state == RADIOLIB_ERR_CRC_MISMATCH) {
      has_received_ever = true;
      last_lora_packet_time = millis();

      int len = radio.getPacketLength();
      lora_rssi = radio.getRSSI();
      lora_snr = radio.getSNR();
      
      String payload = "";
      for(int i = 0; i < len; i++) {
        if (byteArr[i] >= 32 && byteArr[i] <= 126) payload += (char)byteArr[i];
      }
      
      Serial.print("\n[LORA ISR] Payload: ["); Serial.print(payload); Serial.println("]");

      int indexL = payload.indexOf("L:");
      if (indexL != -1) live_hornets = payload.substring(indexL + 2).toInt();
      
      int indexH = payload.indexOf("H:");
      if (indexH != -1) {
        int incoming_id = payload.substring(indexH + 2).toInt();
        if (incoming_id > total_hornets_today) total_hornets_today = incoming_id;
      }

      int indexE = payload.indexOf("E:");
      if (indexE != -1) {
        String e_data = payload.substring(indexE + 2);
        sscanf(e_data.c_str(), "%f,%f,%f,%f", &hive_temp, &hive_hum, &cpu_temp, &cpu_load);
        
        if (mqtt_client.connected()) {
          String json = "{\"cpu_temp\":" + String(cpu_temp) + ", \"cpu_load\":" + String(cpu_load) + ", \"rssi\":" + String(lora_rssi) + "}";
          mqtt_client.publish("smarthive/telemetry", json.c_str());
          Serial.println("   -> [MQTT] Datos publicados con éxito.");
        }
      }

    } else {
      Serial.print("[LORA ERROR] ISR decoding failed: "); Serial.println(state);
    }

    // Rearmamos el chip para la siguiente interrupción
    radio.startReceive();
  }
}