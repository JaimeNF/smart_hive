/**
 * @file bme280_linux.hpp
 */

#ifndef BME280_LINUX_HPP
#define BME280_LINUX_HPP

#include <cstdint>
#include <string>

/* ========================================================================
 * DATA STRUCTURES
 * ======================================================================== */

/**
 * @brief Hardware-burnt factory calibration coefficients.
 * @details Each individual BME280 chip contains a unique compensation matrix 
 * permanently stored in its internal Non-Volatile Memory (NVM) during manufacturing. 
 * These coefficients are mathematically required to convert raw ADC readings 
 * into accurate, real-world metric units.
 */
struct BME280_CalibrationData {
    uint16_t dig_T1; int16_t dig_T2; int16_t dig_T3;
    uint8_t  dig_H1; int16_t dig_H2; uint8_t  dig_H3;
    int16_t  dig_H4; int16_t dig_H5; int8_t   dig_H6;
};

/* ========================================================================
 * CORE SENSOR DRIVER CLASS
 * ======================================================================== */

/**
 * @brief Object-oriented HAL (Hardware Abstraction Layer) for the BME280 sensor.
 * @details Manages the low-level I2C file descriptors natively on Linux (sysfs),
 * bypassing the need for heavy third-party dependencies (like Python's smbus or wiringPi)
 * to ensure a lightweight and robust edge-computing footprint.
 */
class BME280 {
public:
    /**
     * @brief Instantiates the sensor driver architecture.
     * @param bus The hardware I2C bus number (typically 1 for Raspberry Pi mapped to /dev/i2c-1).
     * @param address The 7-bit I2C slave address of the hardware (default: 0x76, alternate: 0x77).
     */
    BME280(int bus = 1, uint8_t address = 0x76);
    
    /**
     * @brief Destructor. Safely releases OS-level POSIX file descriptors to prevent resource leaks.
     */
    ~BME280();

    /**
     * @brief Initializes the sensor hardware and establishes the I2C tunnel.
     * @details Opens the /dev/i2c-X file descriptor, establishes communication with 
     * the slave address, extracts the factory calibration matrix, and configures 
     * the internal oversampling and power modes.
     * @return true if initialization and calibration extraction succeed; false on hardware failure.
     */
    bool begin();

    /**
     * @brief Triggers a burst read of the sensor's memory registers.
     * @details Reads the raw Analog-to-Digital (ADC) outputs and applies the Bosch 
     * proprietary compensation algorithms to yield precise environmental metrics.
     * @param temp Reference output variable for Temperature in Celsius (°C).
     * @param hum Reference output variable for Relative Humidity in percentage (%).
     * @return true if data was successfully fetched and computed; false if the bus disconnected.
     */
    bool read_sensor_data(float& temp, float& hum);

private:
    int file_descriptor_;     ///< POSIX file descriptor for the I2C bus interface
    int bus_;                 ///< Configured hardware I2C bus number
    uint8_t address_;         ///< Configured I2C slave address
    bool initialized_;        ///< Internal state lock preventing memory reads prior to initialization
    
    /**
     * @brief Fine-resolution temperature accumulator.
     * @details A global intermediate variable generated during temperature compensation. 
     * The Bosch mathematical algorithm strictly requires this exact computed value to 
     * subsequently process the humidity compensation accurately.
     */
    int32_t t_fine_; 
    
    BME280_CalibrationData calib_; ///< Struct holding the parsed NVM calibration matrix

    /**
     * @brief Extracts the 26-byte calibration block from the sensor's ROM.
     * @return true if the full memory block was parsed successfully.
     */
    bool read_calibration_data();
    
    /**
     * @brief Applies integer-based compensation math to the raw temperature ADC.
     * @param adc_T Raw 20-bit temperature reading from the hardware buffer.
     * @return int32_t Compensated temperature scaled by 100 (e.g., 2450 = 24.50 °C).
     */
    int32_t compensate_temperature(int32_t adc_T);
    
    /**
     * @brief Applies integer-based compensation math to the raw humidity ADC.
     * @param adc_H Raw 16-bit humidity reading from the hardware buffer.
     * @return uint32_t Compensated humidity scaled by 1024 (e.g., 47185 = 46.07 %).
     */
    uint32_t compensate_humidity(int32_t adc_H);
};

#endif // BME280_LINUX_HPP