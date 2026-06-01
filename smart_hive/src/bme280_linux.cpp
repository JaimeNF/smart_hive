/**
 * @file bme280_linux.cpp
 * @brief Implementation of the BME280 I2C Driver.
 * @details Handles low-level POSIX file descriptors, direct memory register manipulation, 
 * I2C burst reads, and the proprietary Bosch integer-based compensation algorithms.
 */

#include "bme280_linux.hpp"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

/* ========================================================================
 * BME280 MEMORY MAP / HARDWARE REGISTERS
 * ======================================================================== */
#define BME280_REG_CTRL_HUM  0xF2 ///< Humidity control register (Oversampling)
#define BME280_REG_CTRL_MEAS 0xF4 ///< Measure control register (Temp/Press Oversampling & Power Mode)
#define BME280_REG_CONFIG    0xF5 ///< Configuration register (Standby time, Filter)
#define BME280_REG_DATA      0xF7 ///< Start of the 8-byte data burst read block (Press, Temp, Hum)

/* ========================================================================
 * LIFECYCLE MANAGEMENT
 * ======================================================================== */

BME280::BME280(int bus, uint8_t address) 
    : bus_(bus), address_(address), file_descriptor_(-1), initialized_(false), t_fine_(0) {}

BME280::~BME280() {
    // Safely release the OS-level file descriptor to prevent resource leaks
    if (file_descriptor_ >= 0) {
        close(file_descriptor_);
    }
}

/* ========================================================================
 * HARDWARE INITIALIZATION & CONFIGURATION
 * ======================================================================== */

bool BME280::begin() {
    std::string filename = "/dev/i2c-" + std::to_string(bus_);
    
    // 1. Open the native Linux I2C device node
    if ((file_descriptor_ = open(filename.c_str(), O_RDWR)) < 0) {
        std::cerr << "[ERROR BME280] Failed to open the I2C bus: " << filename << std::endl;
        return false;
    }

    // 2. Bind the file descriptor to the specific I2C slave address (0x76 or 0x77)
    if (ioctl(file_descriptor_, I2C_SLAVE, address_) < 0) {
        std::cerr << "[ERROR BME280] Failed to acquire bus access or talk to slave." << std::endl;
        return false;
    }

    // 3. Extract the factory calibration matrix from the sensor's NVM (Non-Volatile Memory)
    if (!read_calibration_data()) {
        std::cerr << "[ERROR BME280] Failed to read calibration matrix." << std::endl;
        return false;
    }

    // 4. Configure Oversampling and Power Mode
    // Humidity Oversampling x1
    uint8_t ctrl_hum[2] = {BME280_REG_CTRL_HUM, 0x01};  
    if (write(file_descriptor_, ctrl_hum, 2) != 2) return false;

    // Measurement Control: Temp Oversampling x1, Pressure Oversampling x1, Normal Mode (11)
    // Binary: 001 (Temp x1) | 001 (Press x1) | 11 (Normal mode) -> Hex: 0x27
    uint8_t ctrl_meas[2] = {BME280_REG_CTRL_MEAS, 0x27}; 
    if (write(file_descriptor_, ctrl_meas, 2) != 2) return false;

    initialized_ = true;
    return true;
}

/* ========================================================================
 * DATA ACQUISITION & PROCESSING
 * ======================================================================== */

bool BME280::read_sensor_data(float& temp, float& hum) {
    if (!initialized_) return false;

    // 1. Instruct the I2C pointer to move to the start of the data registers (0xF7)
    uint8_t reg = BME280_REG_DATA;
    if (write(file_descriptor_, &reg, 1) != 1) return false;

    // 2. Perform a Burst Read: Read 8 contiguous bytes (Pressure, Temperature, Humidity)
    uint8_t data[8];
    if (read(file_descriptor_, data, 8) != 8) return false;

    // 3. Extract raw ADC values via bitwise concatenation
    // Temperature is a 20-bit value spread across 3 bytes
    int32_t adc_T = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);
    // Humidity is a 16-bit value spread across 2 bytes
    int32_t adc_H = (data[6] << 8) | data[7];

    // 4. Apply proprietary Bosch compensation math to yield human-readable formats
    temp = compensate_temperature(adc_T) / 100.0f;
    hum = compensate_humidity(adc_H) / 1024.0f;

    return true;
}

/* ========================================================================
 * LOW-LEVEL CALIBRATION PARSING
 * ======================================================================== */

bool BME280::read_calibration_data() {
    // Read Temperature & Pressure calibration block (starting at 0x88)
    uint8_t reg = 0x88;
    if (write(file_descriptor_, &reg, 1) != 1) return false;
    
    uint8_t raw[26]; 
    if (read(file_descriptor_, raw, 24) != 24) return false;

    // Reconstruct Little-Endian 16-bit integers for Temperature
    calib_.dig_T1 = (raw[1] << 8) | raw[0];
    calib_.dig_T2 = (raw[3] << 8) | raw[2];
    calib_.dig_T3 = (raw[5] << 8) | raw[4];

    // Read H1 coefficient (standalone register at 0xA1)
    reg = 0xA1;
    if (write(file_descriptor_, &reg, 1) != 1) return false;
    uint8_t h1;
    if (read(file_descriptor_, &h1, 1) != 1) return false;
    calib_.dig_H1 = h1;

    // Read remaining Humidity coefficients block (starting at 0xE1)
    reg = 0xE1;
    if (write(file_descriptor_, &reg, 1) != 1) return false;
    uint8_t h_raw[7];
    if (read(file_descriptor_, h_raw, 7) != 7) return false;

    // Reconstruct complex interleaved Humidity bit patterns
    calib_.dig_H2 = (h_raw[1] << 8) | h_raw[0];
    calib_.dig_H3 = h_raw[2];
    calib_.dig_H4 = (h_raw[3] << 4) | (h_raw[4] & 0x0F);
    calib_.dig_H5 = (h_raw[5] << 4) | (h_raw[4] >> 4);
    calib_.dig_H6 = (int8_t)h_raw[6];

    return true;
}

/* ========================================================================
 * BOSCH SENSORTEC PROPRIETARY COMPENSATION ALGORITHMS
 * ======================================================================== */
// Note: These integer-based formulas are strictly dictated by the BME280 Datasheet.
// They prevent the need for slow floating-point operations on embedded microcontrollers.

int32_t BME280::compensate_temperature(int32_t adc_T) {
    int32_t var1, var2, T;
    
    var1 = ((((adc_T >> 3) - ((int32_t)calib_.dig_T1 << 1))) * ((int32_t)calib_.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)calib_.dig_T1)) * ((adc_T >> 4) - ((int32_t)calib_.dig_T1))) >> 12) * ((int32_t)calib_.dig_T3)) >> 14;
    
    // t_fine_ is saved globally as it is a strict dependency for humidity compensation
    t_fine_ = var1 + var2; 
    T = (t_fine_ * 5 + 128) >> 8;
    return T;
}

uint32_t BME280::compensate_humidity(int32_t adc_H) {
    int32_t v_x1_u32r;
    
    v_x1_u32r = (t_fine_ - ((int32_t)76800));
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)calib_.dig_H4) << 20) - (((int32_t)calib_.dig_H5) * v_x1_u32r)) + 
                ((int32_t)16384)) >> 15) * (((((((v_x1_u32r * ((int32_t)calib_.dig_H6)) >> 10) * (((v_x1_u32r * ((int32_t)calib_.dig_H3)) >> 11) + ((int32_t)32768))) >> 10) + ((int32_t)2097152)) * ((int32_t)calib_.dig_H2) + 8192) >> 14));
    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((int32_t)calib_.dig_H1)) >> 4));
    
    // Clamp values to valid operational boundaries
    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
    
    return (uint32_t)(v_x1_u32r >> 12);
}