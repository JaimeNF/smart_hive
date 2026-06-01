#include <RadioLib.h>

#define NSS_PIN   5
#define DIO1_PIN  2
#define RESET_PIN 25
#define BUSY_PIN  26

SX1262 radio = new Module(NSS_PIN, DIO1_PIN, RESET_PIN, BUSY_PIN);

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("\n=========================================");
  Serial.println("  CAZADOR CONTINUO (0x12 CONFIRMADO)");
  Serial.println("=========================================");

  // Volvemos al 0x12 victorioso
  int state = radio.begin(868.0, 125.0, 7, 5, 0x12, 10, 8);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("[OK] Chip SX1262 configurado a bajo nivel.");
  } else {
    Serial.print("[ERROR] Fallo al iniciar: "); Serial.println(state);
    while (true);
  }

  // ACTIVAMOS EL CRC: Queremos que la estructura sea idéntica a la industrial
  radio.setCRC(true);

  // INICIO CONTINUO: No hay tiempo límite. Escucha infinitamente.
  radio.startReceive();
  Serial.println("Escuchando... Esperando impulso eléctrico en el pin DIO1.");
  Serial.println("-----------------------------------------");
}

void loop() {
  // Leemos el pin físico DIO1. Si se pone en HIGH, el chip ha terminado de descargar todo.
  if (digitalRead(DIO1_PIN) == HIGH) {
    Serial.println("\n[>>] ¡PIN DIO1 ACTIVADO! El chip ha descargado el paquete.");
    
    byte byteArr[256];
    // Extraemos la información de la memoria del chip
    int state = radio.readData(byteArr, 256);

    if (state == RADIOLIB_ERR_NONE || state == RADIOLIB_ERR_CRC_MISMATCH) {
      int len = radio.getPacketLength();
      Serial.print("[BINGO] Paquete leído con éxito. Longitud: "); Serial.print(len); Serial.println(" bytes.");
      
      Serial.print(" -> HEX: ");
      for(int i = 0; i < len; i++) {
        if(byteArr[i] < 16) Serial.print("0");
        Serial.print(byteArr[i], HEX); Serial.print(" ");
      }
      Serial.println();

      Serial.print(" -> TXT: ");
      for(int i = 0; i < len; i++) {
        Serial.print((byteArr[i] >= 32 && byteArr[i] <= 126) ? (char)byteArr[i] : '.');
      }
      Serial.println("\n-----------------------------------------");
      
    } else {
      Serial.print("[ERROR] La antena avisó, pero hubo un fallo al decodificar. Código: ");
      Serial.println(state);
    }

    // Volvemos a armar la recepción continua para el siguiente mensaje
    radio.startReceive();
  }

  // Monitor pasivo: Si detecta una onda fuerte, te avisa en tiempo real
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 100) { // Comprueba rápido
    float rssi = radio.getRSSI();
    // Si la potencia sube de -60 dBm, significa que el Waveshare está disparando
    if (rssi > -60.0) { 
       Serial.print("[Ráfaga detectada en el aire: ");
       Serial.print(rssi);
       Serial.println(" dBm]");
       delay(500); // Pequeña pausa para no saturar tu pantalla
    }
    lastPrint = millis();
  }
  
  delay(10);
}