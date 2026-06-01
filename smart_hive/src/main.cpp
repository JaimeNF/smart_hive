/**
 * @file main.cpp
 * @brief Edge IoT Node with Multithreading, Kalman Tracking, and Continuous DVR.
 * @details Implements a highly optimized Producer-Consumer architecture using POSIX threads.
 * Includes Selective Routing to apply Kalman filtering exclusively to high-priority targets (hornets),
 * significantly reducing CPU overhead. Features a continuous VideoWriter (DVR) with motion 
 * trails for academic/presentation demonstrations.
 * @version 1.6.2 (Bees Visibility + Diagnostic Logger)
 */

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <csignal>
#include <map>    
#include <vector>

// Third-party Libraries
#include <opencv2/opencv.hpp>
#include <hailo/hailort.hpp>

// Local Subsystems
#include "hailo_inference.hpp"
#include "object_tracker.hpp"
#include "rpicam_pipe.hpp"
#include "lora_serial.hpp" 
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
 * @brief Intercepts OS signals (e.g., Ctrl+C) to trigger a graceful shutdown.
 * @param signum The signal code received from the OS.
 */
void signal_handler(int signum) {
    std::cout << "\n[WARN] Shutdown signal received. Initiating graceful teardown..." << std::endl;
    system_running = false;
    frame_cv.notify_all(); // Wake up any sleeping threads
}

/**
 * @brief Producer Thread: Captures frames asynchronously from the camera pipe.
 * @details Prevents the slow I/O operations of the camera from blocking the AI inference loop.
 * @param cam Pointer to the initialized camera pipe object.
 */
void camera_worker(RpiCamPipe* cam) {
    std::cout << "[INFO] Producer Thread (Camera) started." << std::endl;
    cv::Mat local_frame;

    while (system_running) {
        if (cam->read(local_frame)) {
<<<<<<< Updated upstream
            // Critical Section: Pass the frame to the Consumer
=======
>>>>>>> Stashed changes
            std::lock_guard<std::mutex> lock(frame_mutex);
            cv::swap(shared_frame, local_frame); 
            new_frame_ready = true;
            frame_cv.notify_one(); // Alert the consumer thread
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
    std::cout << "[INFO] Starting Smart Hive IoT Node (Continuous Test Mode)..." << std::endl;
    
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

    RpiCamPipe cam(Config::Camera::WIDTH, Config::Camera::HEIGHT, Config::Camera::FPS);
    if (!cam.start()) return 1;

    // Launch the Producer thread
    std::thread producer(camera_worker, &cam);

    /* ------------------------------------------------------------------------
     * 2. CONTINUOUS DVR & MOTION TRAIL VARIABLES
     * ------------------------------------------------------------------------ */
    cv::VideoWriter continuous_writer;
    bool is_recording = false;
    
    // Maps a unique Tracker ID to its historical centroid positions for motion trails
    std::map<int, std::vector<cv::Point>> object_trails; 

    std::cout << "[INFO] Consumer Thread active. Awaiting targets..." << std::endl;
    uint32_t frame_count = 0;
    cv::Mat current_frame;

    /* ------------------------------------------------------------------------
     * 3. CONSUMER LOOP (AI INFERENCE PIPELINE)
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

        if (ai_engine.run_inference(current_frame)) {
            // Fetch raw, amnesic bounding boxes from the Hailo NPU
            std::vector<Detection> raw_targets = ai_engine.get_detections();
            
            // --- 3.1. SELECTIVE ROUTING (CPU OPTIMIZATION) ---
            std::vector<Detection> raw_hornets;
            std::vector<Detection> raw_bees;

            // Separate targets to prevent overloading the Kalman Filter with low-priority insects
            for (const auto& det : raw_targets) {
                if (det.class_id == 1) {
                    raw_hornets.push_back(det); // Fed to the smart tracker
                } else {
                    raw_bees.push_back(det);    // Bypasses the tracker (Dumb drawing)
                }
            }
            
            // Execute mathematical tracking ONLY on hornets
            std::vector<TrackedObject> tracked_hornets = tracker.update(raw_hornets);
            
            // Clone the frame to use it as a drawing canvas
            cv::Mat alert_frame = current_frame.clone(); 
            
            // --- 3.2. INITIALIZE VIDEO WRITER ---
            if (!is_recording) {
                std::string vid_name = std::string(Config::Storage::OUTPUT_DIR) + "TEST_CONTINUO.mp4";
                int codec = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
                continuous_writer.open(vid_name, codec, Config::Camera::FPS, alert_frame.size());
                
                if (continuous_writer.isOpened()) {
                    std::cout << "[REC] Continuous DVR started: " << vid_name << std::endl;
                    is_recording = true; 
                } else {
                    std::cerr << "[ERROR] Failed to initialize the VideoWriter." << std::endl;
                }
            }

            // --- 3.3. RENDER BEES (Dumb Drawing) ---
            // Rendered with thickness 2 to survive mp4 compression algorithms
            for (const auto& bee : raw_bees) {
                int x1 = static_cast<int>(bee.xmin * alert_frame.cols);
                int y1 = static_cast<int>(bee.ymin * alert_frame.rows);
                int x2 = static_cast<int>(bee.xmax * alert_frame.cols);
                int y2 = static_cast<int>(bee.ymax * alert_frame.rows);

                cv::Scalar color_bee = cv::Scalar(0, 255, 0); // Green
                
                cv::rectangle(alert_frame, cv::Point(x1, y1), cv::Point(x2, y2), color_bee, 2);
            }

            // --- 3.4. RENDER HORNETS (Smart Tracking & LoRa Alerts) ---
            if (!tracked_hornets.empty()) {
                for (const auto& obj : tracked_hornets) {
                    
                    int x1 = static_cast<int>(obj.bbox.x * alert_frame.cols);
                    int y1 = static_cast<int>(obj.bbox.y * alert_frame.rows);
                    int x2 = static_cast<int>((obj.bbox.x + obj.bbox.width) * alert_frame.cols);
                    int y2 = static_cast<int>((obj.bbox.y + obj.bbox.height) * alert_frame.rows);

                    cv::Scalar color_hornet = cv::Scalar(0, 0, 255); // Red

                    // Calculate centroid and update historical motion trail
                    cv::Point center(x1 + (x2 - x1) / 2, y1 + (y2 - y1) / 2);
                    object_trails[obj.id].push_back(center);
                    
                    // Maintain a maximum trail length of 30 frames (~1 second)
                    if (object_trails[obj.id].size() > 30) {
                        object_trails[obj.id].erase(object_trails[obj.id].begin());
                    }

                    // Render the comet-style motion trail
                    const auto& trail = object_trails[obj.id];
                    for (size_t p = 1; p < trail.size(); p++) {
                        int thickness = static_cast<int>(2.0 * p / trail.size()) + 1; // Fades out towards the tail
                        cv::line(alert_frame, trail[p - 1], trail[p], color_hornet, thickness);
                    }

                    // Render Bounding Box and Tracker ID
                    std::string class_name = "Hornet #" + std::to_string(obj.id);
                    cv::rectangle(alert_frame, cv::Point(x1, y1), cv::Point(x2, y2), color_hornet, 2);
                    cv::putText(alert_frame, class_name, cv::Point(x1, y1 - 10), cv::FONT_HERSHEY_SIMPLEX, 0.6, color_hornet, 2);

                    // LoRa Anti-Spam Logic: Trigger only on newly registered tracking IDs
                    if (obj.is_new) {
<<<<<<< Updated upstream
                        std::cout << "\n[ALERT] NEW Hornet identified (ID: " << obj.id << ")!" << std::endl;
                        lora.sendAlert("HORNET");
                        cv::imwrite(std::string(Config::Storage::OUTPUT_DIR) + "FOTO_AVISPA_" + std::to_string(obj.id) + ".jpg", alert_frame);
=======
                        
                        // 1. Construct the Optimized Historical Payload ("H:<ID>")
                        std::string payload = "H:" + std::to_string(obj.id);
                        
                        std::cout << "\n[CRITICAL] Threat Detected! ID: " << obj.id << " | Sending Payload: [" << payload << "]" << std::endl;
                        
                        // 2. Transmit via LoRa Radio
                        lora.sendAlert(payload);
                    
>>>>>>> Stashed changes
                    }
                }
            }

            // --- 3.5. GHOST TRAIL CLEANUP ---
            // Remove motion trails for objects that have been officially dropped by the Tracker
            for (auto it = object_trails.begin(); it != object_trails.end(); ) {
                bool is_alive = false;
                for (const auto& obj : tracked_hornets) {
                    if (obj.id == it->first) { is_alive = true; break; }
                }
                
                if (!is_alive) {
                    it = object_trails.erase(it); // Object permanently lost, erase its trail
                } else {
                    ++it;
                }
            }

            // --- 3.6. SAVE FRAME TO DVR ---
            if (is_recording) {
                continuous_writer.write(alert_frame);
            }

            frame_count++;
            
            // --- DIAGNOSTIC LOGGER ---
            if (frame_count % Config::Camera::FPS == 0) {
                std::cout << "[INFO] Frame: " << frame_count 
                          << " | Bees (Raw): " << raw_bees.size() 
                          << " | Hornets (Tracked): " << tracked_hornets.size() << std::endl;
            }
        }
    }

    /* ------------------------------------------------------------------------
     * 4. TEARDOWN AND CLEANUP
     * ------------------------------------------------------------------------ */
    std::cout << "[INFO] Shutting down subsystems..." << std::endl;
    
    // Safely close the video file to prevent file corruption
    if (is_recording && continuous_writer.isOpened()) {
        continuous_writer.release();
        std::cout << "[REC] Continuous DVR video TEST_CONTINUO.mp4 saved successfully." << std::endl;
    }
    
    system_running = false;
    frame_cv.notify_all(); 
    if (producer.joinable()) producer.join(); 
    cam.release();
    
    return 0;
}