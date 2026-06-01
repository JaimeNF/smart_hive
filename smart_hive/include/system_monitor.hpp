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

// Hardware Abstraction Headers
#include "lora_serial.hpp"
#include "bme280_linux.hpp"

/**
 * @brief Plain Old Data (POD) structure for aggregating telemetry data.
 * @details Provides a unified memory block to store both external environmental 
 * conditions and internal computational health metrics before serialization.
 */
struct SystemMetrics {
    float hive_temp;     ///< Internal hive temperature in Celsius (°C)
    float hive_humidity; ///< Internal hive relative humidity percentage (%)
    float cpu_temp;      ///< Edge Node SoC temperature in Celsius (°C)
    float cpu_load;      ///< 1-minute system load average (1.0 = 100% of 1 CPU core)
};

/**
 * @brief Asynchronous daemon for autonomous hardware monitoring.
 * @details Encapsulates its own threading lifecycle. Injects the LoRa transceiver 
 * by reference to share the radio hardware with the primary threat-detection pipeline.
 */
class SystemMonitor {
public:
    /**
     * @brief Initializes the monitoring subsystem.
     * @param lora Reference to the active SX1262 LoRa module for radio transmission.
     * @param interval_minutes Sleep duration between telemetry broadcasts (default: 15 mins).
     */
    SystemMonitor(LoRaSerial& lora, int interval_minutes = 15);
    
    /**
     * @brief Destructor. Automatically triggers a graceful thread shutdown to prevent memory leaks.
     */
    ~SystemMonitor();

    /**
     * @brief Spawns the background worker thread and begins the monitoring loop.
     */
    void start();
    
    /**
     * @brief Safely terminates the background thread. 
     * @details Signals the condition variable to wake the thread immediately if it is sleeping,
     * ensuring the OS is not blocked during a system halt.
     */
    void stop();

private:
    /**
     * @brief The core execution loop running inside the detached thread.
     */
    void monitor_loop();
    
    /**
     * @brief Orchestrates the retrieval of data from all sensors and OS files.
     * @return SystemMetrics A populated struct with the latest hardware readings.
     */
    SystemMetrics gather_metrics();

    /* --- Native Hardware Interfacing --- */
    
    /**
     * @brief Reads the raw SoC thermal zone via Linux sysfs (/sys/class/thermal).
     * @return float CPU temperature in Celsius, or -1.0 on read error.
     */
    float read_cpu_temp();
    
    /**
     * @brief Retrieves the UNIX load average using the native getloadavg() system call.
     * @return float System load average over the last 1 minute.
     */
    float read_cpu_load();
    
    /**
     * @brief Fetches compensated temperature and humidity from the bare-metal I2C driver.
     * @param temp Reference output variable for temperature.
     * @param hum Reference output variable for humidity.
     * @return true if the BME280 sensor responded successfully, false otherwise.
     */
    bool read_bme280(float& temp, float& hum);

    /* --- Dependencies & State Variables --- */
    
    LoRaSerial& lora_;                 ///< Shared dependency for RF transmission
    int interval_minutes_;             ///< Duty cycle sleep duration
    
    /* --- Thread Control & Synchronization Primitives --- */
    
    std::atomic<bool> running_;        ///< Thread-safe atomic flag for loop execution
    std::thread monitor_thread_;       ///< The OS-level worker thread
    std::mutex cv_m_;                  ///< Mutex for the condition variable
    std::condition_variable cv_;       ///< CV allowing interruptible, zero-CPU sleep
    
    /* --- Subsystem Drivers --- */
    
    BME280 bme_sensor_;                ///< Instance of the I2C Environmental Sensor driver
};

#endif // SYSTEM_MONITOR_HPP