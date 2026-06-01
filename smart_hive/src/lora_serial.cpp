/**
 * @file lora_serial.cpp
 */

#include "lora_serial.hpp"

LoRaSerial::LoRaSerial(std::string portName, int baud)
    : port(portName), baudRate(baud), serial_fd(-1) {}

LoRaSerial::~LoRaSerial() {
    if (serial_fd != -1) close(serial_fd);
}

/**
 * @brief Opens the serial port and configures POSIX termios settings.
 */
bool LoRaSerial::begin() {
    serial_fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (serial_fd == -1) {
        std::cerr << "[ERROR] Failed to open serial port: " << port << std::endl;
        return false;
    }

    struct termios tty;
    if (tcgetattr(serial_fd, &tty) != 0) return false;

    // Set communication speed
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);

    // 8N1 standard configuration
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; 
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 5;

    // Disable software flow control and enable raw data mapping
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;

    if (tcsetattr(serial_fd, TCSANOW, &tty) != 0) return false;

    return true;
}

/**
 * @brief Switches hardware to command mode and injects AT configuration parameters.
 */
bool LoRaSerial::configure(int sf, int power, int channel) {
    // Escape sequence to enter AT command mode
    write(serial_fd, "+++", 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Master AT configuration string: SF, BW(125), CR(4/5), PWR, NETID, LBT, MODE(Stream), TX, RX...
    std::string cmd = "AT+ALLP=" + std::to_string(sf) + ",0,1," + std::to_string(power) + ",0,0,1," + std::to_string(channel) + "," + std::to_string(channel) + ",0,0,1,\"8N1\",115200,0\r\n";

    write(serial_fd, cmd.c_str(), cmd.length());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Exit AT mode to resume transparent streaming
    write(serial_fd, "AT+EXIT\r\n", 9);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    return true;
}

/**
 * @brief Dispatches a plaintext payload to the UART buffer for RF transmission.
 */
bool LoRaSerial::sendAlert(std::string message) {
    if (serial_fd == -1) return false;

    // Hardware expects a newline delimiter in transparent stream mode
    std::string payload = message + "\n";
    ssize_t bytesWritten = write(serial_fd, payload.c_str(), payload.length());

    return (bytesWritten != -1);
}