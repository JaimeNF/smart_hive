/**
 * @file config.hpp
 * @brief Global configuration parameters for the IoT Node.
 * @details Defines compile-time constants for hardware, AI models,
 * camera pipes, and LoRa telemetry to avoid hardcoded values.
 * @version 1.0.0
 */

#ifndef CONFIG_HPP
#define CONFIG_HPP

namespace Config {

    /** @brief AI model and inference parameters */
    namespace AI {
        constexpr const char* MODEL_PATH = "../models/yolov11n_accuracy.hef";
        constexpr float CONFIDENCE_THRESHOLD = 0.45f; // Minimum confidence to trigger alert
    }

    /** @brief LoRa module serial and transmission parameters */
    namespace LoRa {
        constexpr const char* SERIAL_PORT = "/dev/ttyACM0";
        constexpr int BAUD_RATE = 115200;
        constexpr int SPREADING_FACTOR = 7;
        constexpr int TX_POWER_DBM = 20;
        constexpr int CHANNEL = 18; 
    }

    /** @brief Raspberry Pi camera subsystem parameters */
    namespace Camera {
        constexpr int WIDTH = 640;
        constexpr int HEIGHT = 640;
        constexpr int FPS =60;
    }

    /** @brief Local storage parameters for evidence persistence */
    namespace Storage {
        constexpr const char* OUTPUT_DIR = "../images/";
        constexpr const char* FILE_PREFIX = "alert_";
        constexpr const char* FILE_EXTENSION = ".jpg";
    }

} // namespace Config

#endif // CONFIG_HPP