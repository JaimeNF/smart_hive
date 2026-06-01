/**
 * @file lora_serial.hpp
 * @brief Hardware Abstraction Layer for LoRa USB serial communication.
 * @details Provides a POSIX-compliant C++ interface to communicate with 
 * AT-command based LoRa USB modules (e.g., Waveshare Core1262)
 * over standard Linux UART interfaces.
 * @version 1.0.0
 */

#ifndef LORA_SERIAL_HPP
#define LORA_SERIAL_HPP

#include <string>
#include <iostream>
#include <fcntl.h>    
#include <termios.h>  
#include <unistd.h>   
#include <cstring>
#include <thread>
#include <chrono>

/**
 * @class LoRaSerial
 * @brief Manages the lifecycle and data transmission of a LoRa serial connection.
 * @details Encapsulates file descriptors, termios configuration, and payload 
 * dispatching for headless telemetry nodes.
 */
class LoRaSerial {
private:
    int serial_fd;      /**< POSIX file descriptor for the active serial connection */
    std::string port;   /**< System path to the UART interface */
    int baudRate;       /**< Operating baud rate for the UART connection */

public:
    /**
     * @brief Constructs a new LoRa Serial controller.
     * @param portName Absolute system path to the target UART interface (e.g., "/dev/ttyACM0").
     * @param baud Communication speed in bps. Defaults to 115200.
     */
    LoRaSerial(std::string portName, int baud = 115200);

    /**
     * @brief Destroys the LoRa Serial object and releases system resources.
     * @details Ensures the file descriptor is safely closed upon object destruction.
     */
    ~LoRaSerial();

    /**
     * @brief Initializes the physical serial connection.
     * @details Opens the file descriptor and applies the required termios flags 
     * (8N1, non-blocking, raw mode) for AT command transmission.
     * @return true if the port is successfully opened and configured; false otherwise.
     */
    bool begin();

    /**
     * @brief Configures the hardware transceiver parameters via AT commands.
     * @param sf Spreading Factor (typically 7-12).
     * @param power Transmission power in dBm (e.g., 20 for maximum range).
     * @param channel Frequency channel index (e.g., 18 for 868.0 MHz).
     * @return true if the configuration payload was successfully dispatched; false on I/O failure.
     */
    bool configure(int sf, int power, int channel);

    /**
     * @brief Transmits a plaintext payload over the LoRa stream.
     * @details Appends the necessary carriage return/line feed characters expected 
     * by the hardware in transparent/stream mode.
     * @param message The raw text payload to be broadcasted.
     * @return true if the payload was successfully written to the UART buffer; false otherwise.
     */
    bool sendAlert(std::string message);

    /**
     * @brief Flushes pending data in the UART input/output buffers.
     * @details Discards any unread data received by the port and ensures all 
     * queued outgoing data is physically transmitted.
     */
    void flush();
};

#endif // LORA_SERIAL_HPP