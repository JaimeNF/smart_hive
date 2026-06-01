/**
 * @file system_monitor.cpp
 */

#include "system_monitor.hpp"
#include <iostream>
#include <fstream>
#include <stdlib.h> // Required for native getloadavg()
#include <unistd.h>
#include <cstdio>

/* ========================================================================
 * LIFECYCLE MANAGEMENT
 * ======================================================================== */

SystemMonitor::SystemMonitor(LoRaSerial& lora, int interval_minutes)
    : lora_(lora), interval_minutes_(interval_minutes), running_(false) {
    
    // Initialize the I2C Bus 1 and probe the BME280 sensor at address 0x76.
    // (Note: If hardware is undetected, consider falling back to 0x77).
    if (bme_sensor_.begin()) {
        std::cout << "[INFO] BME280 Hardware initialized correctly via I2C." << std::endl;
    } else {
        std::cerr << "[WARN] BME280 Hardware not detected. Environmental data will be disabled." << std::endl;
    }
}

SystemMonitor::~SystemMonitor() {
    stop(); // Ensure the thread is safely merged before destroying the object
}

void SystemMonitor::start() {
    if (!running_) {
        running_ = true;
        // Spawn the background worker thread
        monitor_thread_ = std::thread(&SystemMonitor::monitor_loop, this);
        std::cout << "[INFO] System Monitor started. Telemetry interval: " << interval_minutes_ << " minutes." << std::endl;
    }
}

void SystemMonitor::stop() {
    if (running_) {
        running_ = false;
        
        // Broadcast a wake-up signal to the condition variable.
        // This instantly interrupts the thread if it is currently sleeping,
        // allowing for a zero-latency graceful OS shutdown.
        cv_.notify_all(); 
        
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
        std::cout << "[INFO] System Monitor gracefully stopped." << std::endl;
    }
}

/* ========================================================================
 * CORE ASYNCHRONOUS LOOP
 * ======================================================================== */

void SystemMonitor::monitor_loop() {
    // Initial delay to prevent radio collisions with LoRa startup routines
    // and to allow the OS to stabilize its load averages.
    std::this_thread::sleep_for(std::chrono::seconds(10));

    while (running_) {
        // 1. Harvest environmental and internal hardware metrics
        SystemMetrics metrics = gather_metrics();

        // 2. Strict Payload Serialization
        // Format: "E:<HiveTemp>,<HiveHum>,<CpuTemp>,<CpuLoad>"
        // Utilizing a statically allocated char array (Zero-allocation) to prevent 
        // heap fragmentation and memory leaks in long-term embedded deployments.
        char payload[64];
        std::snprintf(payload, sizeof(payload), "E:%.1f,%.0f,%.1f,%.1f", 
                      metrics.hive_temp, 
                      metrics.hive_humidity, 
                      metrics.cpu_temp, 
                      metrics.cpu_load);

        std::cout << "\n[TELEMETRY] Health Check | Sending: [" << payload << "]" << std::endl;

        // 3. Dispatch payload to the LoRa Radio hardware
        lora_.sendAlert(std::string(payload));

        // 4. Smart Interruptible Sleep (Watchdog-friendly)
        // The thread will sleep for the defined interval but will wake up 
        // instantaneously if stop() sets running_ to false.
        std::unique_lock<std::mutex> lock(cv_m_);
        cv_.wait_for(lock, std::chrono::minutes(interval_minutes_), [this]{ return !running_; });
    }
}

SystemMetrics SystemMonitor::gather_metrics() {
    SystemMetrics m;
    
    // Fetch Internal Node Health
    m.cpu_temp = read_cpu_temp();
    m.cpu_load = read_cpu_load();

    // Fetch External Hive Environment via I2C
    if (!read_bme280(m.hive_temp, m.hive_humidity)) {
        // Fault Tolerance: Inject sentinel values (-99.0) if the I2C wires 
        // are disconnected or the sensor gets damaged, alerting the dashboard.
        m.hive_temp = -99.0f;
        m.hive_humidity = -99.0f;
    }

    return m;
}

/* ========================================================================
 * NATIVE HARDWARE ABSTRACTION LAYER (HAL)
 * ======================================================================== */

float SystemMonitor::read_cpu_temp() {
    // Reads the native Linux thermal zone virtual file.
    // The OS provides the raw CPU core temperature in milli-Celsius.
    std::ifstream temp_file("/sys/class/thermal/thermal_zone0/temp");
    if (temp_file.is_open()) {
        long temp_milli;
        temp_file >> temp_milli;
        return temp_milli / 1000.0f; // Scale down to standard Celsius
    }
    return -1.0f; // OS read failure
}

float SystemMonitor::read_cpu_load() {
    // Executes the POSIX standard getloadavg() system call.
    // Retrieves the system load average over the last 1 minute.
    // (1.0 equals 100% utilization of a single CPU core).
    double load[3];
    if (getloadavg(load, 3) != -1) {
        return static_cast<float>(load[0]);
    }
    return -1.0f; // Syscall failure
}

bool SystemMonitor::read_bme280(float& temp, float& hum) {
    // Delegates physical I2C register reading and mathematical 
    // calibration compensation to the dedicated BME280 driver.
    return bme_sensor_.read_sensor_data(temp, hum);
}