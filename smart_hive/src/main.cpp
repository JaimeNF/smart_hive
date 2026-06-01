/**
 * @file main.cpp
 * @brief Edge IoT Node - ULTRA-LEAN PRODUCTION VERSION
 * @details Optimized for maximum long-term stability and minimum power consumption.
 * Features Zero Disk I/O (no images or videos saved) to prevent SD card degradation.
 * Implements a highly optimized Producer-Consumer threading architecture, Selective 
 * Routing for CPU optimization, and state-change driven LoRa telemetry payloads.
 * @version 1.2.0 (Continuous Run + Zero Disk I/O)
 */

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <csignal>
#include <vector>

// Third-party Libraries
#include <opencv2/opencv.hpp>
#include <hailo/hailort.hpp>

// Local Subsystems
#include "hailo_inference.hpp"
#include "object_tracker.hpp"
#include "rpicam_pipe.hpp"
#include "lora_serial.hpp" 
#include "system_monitor.hpp"
#include "config.hpp"

/* ========================================================================
 * GLOBAL IPC (Inter-Process Communication) VARIABLES
 * ======================================================================== */
std::mutex frame_mutex;
std::condition_variable frame_cv;
cv::Mat shared_frame;
bool new_frame_ready = false;
std::atomic<bool> system_running{true}; // Atomic flag for thread-safe shutdown

/**
 * @brief Intercepts OS signals (e.g., Ctrl+C) to trigger a graceful system teardown.
 * @param signum The signal code received from the OS.
 */
void signal_handler(int signum) {
    std::cout << "\n[WARN] Shutdown signal received. Initiating graceful teardown..." << std::endl;
    system_running = false;
    frame_cv.notify_all(); // Wake up any sleeping threads to exit their loops
}

/**
 * @brief Producer Thread: Captures frames asynchronously from the camera hardware.
 * @details Decouples the slow I/O operations of the camera from the main AI inference loop,
 * ensuring the NPU is never starved for data.
 * @param cam Pointer to the initialized camera pipe object.
 */
void camera_worker(RpiCamPipe* cam) {
    std::cout << "[INFO] Producer Thread (Camera) started." << std::endl;
    cv::Mat local_frame;

    while (system_running) {
        if (cam->read(local_frame)) {
            // Critical Section: Safely transfer the frame to the shared mailbox
            std::lock_guard<std::mutex> lock(frame_mutex);
            
            // Zero-copy swap: Instantly exchanges memory pointers instead of heavy deep copying
            cv::swap(shared_frame, local_frame); 
            new_frame_ready = true;
            frame_cv.notify_one(); // Alert the consumer thread that new data is available
        } else {
            std::cerr << "[WARN] Pipe I/O error or stream ended. Stopping Producer Thread." << std::endl;
            system_running = false;
            frame_cv.notify_all();
            break;
        }
    }
    std::cout << "[INFO] Producer Thread gracefully stopped." << std::endl;
}

/* ========================================================================
 * MAIN ENTRY POINT
 * ======================================================================== */
int main() {
    std::cout << "[INFO] Starting Smart Hive IoT Node (ULTRA-LEAN PRODUCTION MODE)..." << std::endl;
    
    // Register POSIX signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    /* ------------------------------------------------------------------------
     * 1. HARDWARE & SUBSYSTEM INITIALIZATION
     * ------------------------------------------------------------------------ */
    HailoInference ai_engine(Config::AI::MODEL_PATH);
    if (!ai_engine.is_initialized()) return 1;

    // Initialize the Kalman Filter Tracker (Remembers lost objects for up to 15 frames)
    ObjectTracker tracker(15); 
    
    LoRaSerial lora(Config::LoRa::SERIAL_PORT);
    if (lora.begin()) lora.configure(Config::LoRa::SPREADING_FACTOR, Config::LoRa::TX_POWER_DBM, Config::LoRa::CHANNEL);

    SystemMonitor sys_monitor(lora, Config::System::TELEMETRY_INTERVAL);
    sys_monitor.start();

    RpiCamPipe cam(Config::Camera::WIDTH, Config::Camera::HEIGHT, Config::Camera::FPS);
    if (!cam.start()) return 1;


    // Launch the Producer thread
    std::thread producer(camera_worker, &cam);

    /* ------------------------------------------------------------------------
     * 2. STATE VARIABLES
     * ------------------------------------------------------------------------ */
    uint32_t frame_count = 0;
    cv::Mat current_frame;
    
    // Memory state for real-time telemetry (Prevents LoRa bandwidth spamming)
    int last_live_count = 0; 

    std::cout << "[INFO] System armed and monitoring. Awaiting threats (Zero Disk I/O Mode)..." << std::endl;

    /* ------------------------------------------------------------------------
     * 3. CONSUMER LOOP (AI INFERENCE & TELEMETRY PIPELINE)
     * ------------------------------------------------------------------------ */
    while (system_running) {
        
        // Wait for a new frame to be delivered by the Producer
        {
            std::unique_lock<std::mutex> lock(frame_mutex);
            frame_cv.wait(lock, []{ return new_frame_ready || !system_running; });
            if (!system_running) break; 
            
            // Zero-copy fetch
            cv::swap(current_frame, shared_frame); 
            new_frame_ready = false; 
        }

        // Push frame to the Hailo Neural Processing Unit
        if (ai_engine.run_inference(current_frame)) {
            
            // Fetch raw, amnesic bounding boxes from the hardware
            std::vector<Detection> raw_targets = ai_engine.get_detections();
            
            // --- 3.1. SELECTIVE ROUTING (CPU OPTIMIZATION) ---
            std::vector<Detection> raw_hornets;
            
            // Discard low-priority insects (e.g., bees) to prevent overloading the Kalman matrices
            for (const auto& det : raw_targets) {
                if (det.class_id == 1) {
                    raw_hornets.push_back(det); 
                }
            }
            
            // Execute mathematical tracking ONLY on verified hornets
            std::vector<TrackedObject> tracked_hornets = tracker.update(raw_hornets);
            
            // --- 3.2. REAL-TIME TELEMETRY (STATE-CHANGE DRIVEN) ---
            int current_live_count = tracked_hornets.size();
            
            // Broadcast a LoRa update ONLY when the number of hornets actively on-screen changes
            if (current_live_count != last_live_count) {
                std::string live_payload = "L:" + std::to_string(current_live_count);
                lora.sendAlert(live_payload); // Transmits "L:1", "L:2", or "L:0"
                
                std::cout << "[TELEMETRY] Live Hornets on screen changed to: " << current_live_count << std::endl;
                last_live_count = current_live_count;
            }

            // --- 3.3. HISTORICAL TELEMETRY (LOGIC ONLY, NO I/O) ---
            if (!tracked_hornets.empty()) {
                
                for (const auto& obj : tracked_hornets) {
                    
                    // Trigger logic only for newly confirmed unique objects
                    if (obj.is_new) {
                        
                        // 1. Construct the Optimized Historical Payload ("H:<ID>")
                        std::string payload = "H:" + std::to_string(obj.id);
                        
                        std::cout << "\n[CRITICAL] Threat Detected! ID: " << obj.id << " | Sending Payload: [" << payload << "]" << std::endl;
                        
                        // 2. Transmit via LoRa Radio
                        lora.sendAlert(payload);
                        
                        // NOTE: Forensic image rendering and saving (cv::imwrite) has been 
                        // explicitly removed to ensure prolonged SD card lifespan and minimal 
                        // power draw during 24/7 continuous edge deployment.
                    }
                }
            }

            frame_count++;
            
            // System Heartbeat Logger (Occurs roughly every ~33 seconds at 30 FPS)
            if (frame_count % 1000 == 0) {
                std::cout << "[HEARTBEAT] System nominal. Processed frames: " << frame_count << std::endl;
            }
        }
    }

    /* ------------------------------------------------------------------------
     * 4. TEARDOWN AND CLEANUP
     * ------------------------------------------------------------------------ */
    std::cout << "[INFO] Shutting down subsystems..." << std::endl;
    system_running = false;
    frame_cv.notify_all(); 
    
    // Ensure the camera thread merges safely before exit
    if (producer.joinable()) {
        producer.join(); 
    }
    
    cam.release();
    std::cout << "[INFO] Smart Hive IoT Node safely terminated." << std::endl;
    
    return 0;
}