/**
 * @file bme280_linux.hpp
 * @brief Native Linux I2C Driver for the Bosch BME280 Environmental Sensor.
 * @details Implements zero-dependency, bare-metal I2C communication using 
 * POSIX standard <linux/i2c-dev.h>. Includes factory calibration matrix parsing 
 * and integer-based compensation formulas for high accuracy.
 * @version 1.0.0
 */

#ifndef BME280_LINUX_HPP
#define BME280_LINUX_HPP

#include <cstdint>
#include <string>

// Estructura que almacena los coeficientes únicos de fábrica de tu sensor
struct BME280_CalibrationData {
    uint16_t dig_T1; int16_t dig_T2; int16_t dig_T3;
    uint8_t  dig_H1; int16_t dig_H2; uint8_t  dig_H3;
    int16_t  dig_H4; int16_t dig_H5; int8_t   dig_H6;
};

class BME280 {
public:
    /**
     * @brief Constructor.
     * @param bus The I2C bus number (typically 1 for Raspberry Pi /dev/i2c-1).
     * @param address The I2C hardware address (0x76 or 0x77).
     */
    BME280(int bus = 1, uint8_t address = 0x76);
    ~BME280();

    /**
     * @brief Initializes the sensor, opens the I2C tunnel, and reads the calibration matrix.
     * @return true if successful, false if hardware is unreachable.
     */
    bool begin();

    /**
     * @brief Reads raw data and applies compensation math to output real-world values.
     * @param temp Output variable for Temperature in Celsius.
     * @param hum Output variable for Relative Humidity in %.
     * @return true if successful.
     */
    bool read_sensor_data(float& temp, float& hum);

private:
    int file_descriptor_;
    int bus_;
    uint8_t address_;
    bool initialized_;
    
    int32_t t_fine_; // Global variable required for humidity compensation
    BME280_CalibrationData calib_;

    bool read_calibration_data();
    int32_t compensate_temperature(int32_t adc_T);
    uint32_t compensate_humidity(int32_t adc_H);
};

#endif // BME280_LINUX_HPP