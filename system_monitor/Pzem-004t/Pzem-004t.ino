#include <SoftwareSerial.h>
#include <PZEM004Tv30.h>

SoftwareSerial pzemSerial(2, 3);
PZEM004Tv30 pzem(pzemSerial);

const float UMBRAL_POTENCIA = 2.0;

// Variables para el control de tiempo y estado
bool isMeasuring = false;
unsigned long lastMeasureTime = 0;
const unsigned long measureInterval = 1000; // Medir cada 1000 ms (1 segundo)

void setup() {
  Serial.begin(115200);
  pzemSerial.begin(9600);
}

void loop() {
  // 1. Escuchar comandos desde Python
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim(); // Eliminar espacios o saltos de línea invisibles

    if (command == "START") {
      isMeasuring = true;
      Serial.println("MSG,Iniciando medicion...");
    } 
    else if (command == "STOP") {
      isMeasuring = false;
      Serial.println("MSG,Medicion detenida.");
    }
  }

  // 2. Ejecutar mediciones si está activo (sin usar delay para no bloquear)
  if (isMeasuring && (millis() - lastMeasureTime >= measureInterval)) {
    lastMeasureTime = millis();
    
    float voltage = pzem.voltage();
    float current = pzem.current();
    float power = pzem.power();
    float energy = pzem.energy();
    float frequency = pzem.frequency();
    float pf = pzem.pf();

    if (!isnan(voltage)) {
      // Ajuste de calibración por software
      if (power < UMBRAL_POTENCIA) {
        power = 0.0;
        current = 0.0;
      }

      // Enviar datos en formato: Voltaje,Corriente,Potencia,Energia,Frecuencia,FP
      Serial.print("DATA,");
      Serial.print(voltage, 2); Serial.print(",");
      Serial.print(current, 3); Serial.print(",");
      Serial.print(power, 2); Serial.print(",");
      Serial.print(energy, 4); Serial.print(",");
      Serial.print(frequency, 1); Serial.print(",");
      Serial.println(pf, 2);
    } else {
      Serial.println("ERROR,Fallo lectura PZEM");
    }
  }
}