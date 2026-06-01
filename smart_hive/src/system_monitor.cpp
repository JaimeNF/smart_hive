/**
 * @file system_monitor.cpp
 */

#include "system_monitor.hpp"
#include <iostream>
#include <fstream>
#include <stdlib.h> // Para getloadavg()
#include <unistd.h>
#include <cstdio>

// Si vas a usar una librería C++ de BME280, inclúyela aquí.
// #include "bme280_driver.h" 

SystemMonitor::SystemMonitor(LoRaSerial& lora, int interval_minutes)
    : lora_(lora), interval_minutes_(interval_minutes), running_(false) {
    
    // Iniciar I2C Bus 1, dirección 0x76 (si no funciona, prueba 0x77)
    if (bme_sensor_.begin()) {
        std::cout << "[INFO] BME280 Hardware initialized correctly." << std::endl;
    } else {
        std::cerr << "[WARN] BME280 Hardware not detected." << std::endl;
    }
}

SystemMonitor::~SystemMonitor() {
    stop();
}

void SystemMonitor::start() {
    if (!running_) {
        running_ = true;
        monitor_thread_ = std::thread(&SystemMonitor::monitor_loop, this);
        std::cout << "[INFO] System Monitor started. Interval: " << interval_minutes_ << " minutes." << std::endl;
    }
}

void SystemMonitor::stop() {
    if (running_) {
        running_ = false;
        cv_.notify_all(); // Despierta al hilo inmediatamente si estaba durmiendo
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
        std::cout << "[INFO] System Monitor gracefully stopped." << std::endl;
    }
}

void SystemMonitor::monitor_loop() {
    // Dormimos un poco al arrancar para no pisar los mensajes iniciales del LoRa
    std::this_thread::sleep_for(std::chrono::seconds(10));

    while (running_) {
        // 1. Recolectar datos de los sensores y el sistema
        SystemMetrics metrics = gather_metrics();

        // 2. Comprimir datos (Serialización Estricta)
        // Formato: "E:<TempColmena>,<Humedad>,<TempCPU>,<CargaCPU>"
        // Usamos un array estático pequeño para no saturar la memoria
        char payload[64];
        std::snprintf(payload, sizeof(payload), "E:%.1f,%.0f,%.1f,%.1f", 
                      metrics.hive_temp, 
                      metrics.hive_humidity, 
                      metrics.cpu_temp, 
                      metrics.cpu_load);

        std::cout << "\n[TELEMETRY] Health Check | Sending: [" << payload << "]" << std::endl;

        // 3. Transmitir por LoRa
        lora_.sendAlert(std::string(payload));

        // 4. Dormir de forma inteligente (Interruptible Sleep)
        // El hilo dormirá 15 minutos, pero despertará al instante si stop() cambia running_ a false.
        std::unique_lock<std::mutex> lock(cv_m_);
        cv_.wait_for(lock, std::chrono::minutes(interval_minutes_), [this]{ return !running_; });
    }
}

SystemMetrics SystemMonitor::gather_metrics() {
    SystemMetrics m;
    
    // Leemos la CPU
    m.cpu_temp = read_cpu_temp();
    m.cpu_load = read_cpu_load();

    // Leemos la Colmena (I2C)
    if (!read_bme280(m.hive_temp, m.hive_humidity)) {
        // Valores de error por si el sensor se desconecta
        m.hive_temp = -99.0f;
        m.hive_humidity = -99.0f;
    }

    return m;
}

/* ------------------------------------------------------------------------
 * LECTURAS DE HARDWARE
 * ------------------------------------------------------------------------ */

float SystemMonitor::read_cpu_temp() {
    // Linux guarda la temperatura real de la Raspberry en este archivo en miligrados
    std::ifstream temp_file("/sys/class/thermal/thermal_zone0/temp");
    if (temp_file.is_open()) {
        long temp_milli;
        temp_file >> temp_milli;
        return temp_milli / 1000.0f; // Convertir a Celsius
    }
    return -1.0f;
}

float SystemMonitor::read_cpu_load() {
    // Obtiene la carga media del sistema en el último minuto (1.0 = 100% de 1 núcleo)
    double load[3];
    if (getloadavg(load, 3) != -1) {
        return static_cast<float>(load[0]);
    }
    return -1.0f;
}

bool SystemMonitor::read_bme280(float& temp, float& hum) {
    return bme_sensor_.read_sensor_data(temp, hum);
}