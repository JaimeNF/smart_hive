/**
 * @file system_monitor.hpp
 */

#ifndef SYSTEM_MONITOR_HPP
#define SYSTEM_MONITOR_HPP

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include "lora_serial.hpp"
#include "bme280_linux.hpp"

// Estructura de datos limpia para la telemetría
struct SystemMetrics {
    float hive_temp;
    float hive_humidity;
    float cpu_temp;
    float cpu_load;
};

class SystemMonitor {
public:
    /**
     * @brief Constructor del subsistema de monitorización.
     * @param lora Referencia al módulo LoRa para poder transmitir.
     * @param interval_minutes Cada cuántos minutos se enviará el paquete.
     */
    SystemMonitor(LoRaSerial& lora, int interval_minutes = 15);
    ~SystemMonitor();

    void start();
    void stop();

private:
    void monitor_loop();
    SystemMetrics gather_metrics();

    // Lecturas Nativas de Hardware
    float read_cpu_temp();
    float read_cpu_load();
    bool read_bme280(float& temp, float& hum);

    LoRaSerial& lora_;
    int interval_minutes_;
    
    // Control del hilo y apagado seguro
    std::atomic<bool> running_;
    std::thread monitor_thread_;
    std::mutex cv_m_;
    std::condition_variable cv_;
    BME280 bme_sensor_;
};

#endif // SYSTEM_MONITOR_HPP