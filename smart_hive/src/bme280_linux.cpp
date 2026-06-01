/**
 * @file bme280_linux.cpp
 * @brief Implementation of the BME280 I2C Driver.
 */

#include "bme280_linux.hpp"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

/* Registros de memoria del BME280 */
#define BME280_REG_CTRL_HUM  0xF2
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG    0xF5
#define BME280_REG_DATA      0xF7 // Inicio de la ráfaga de datos

BME280::BME280(int bus, uint8_t address) 
    : bus_(bus), address_(address), file_descriptor_(-1), initialized_(false), t_fine_(0) {}

BME280::~BME280() {
    if (file_descriptor_ >= 0) {
        close(file_descriptor_);
    }
}

bool BME280::begin() {
    std::string filename = "/dev/i2c-" + std::to_string(bus_);
    
    // 1. Abrimos el bus I2C nativo de Linux
    if ((file_descriptor_ = open(filename.c_str(), O_RDWR)) < 0) {
        std::cerr << "[ERROR BME280] Failed to open the I2C bus: " << filename << std::endl;
        return false;
    }

    // 2. Establecemos conexión con la dirección del esclavo (0x76)
    if (ioctl(file_descriptor_, I2C_SLAVE, address_) < 0) {
        std::cerr << "[ERROR BME280] Failed to acquire bus access or talk to slave." << std::endl;
        return false;
    }

    // 3. Leemos los coeficientes de fábrica
    if (!read_calibration_data()) {
        std::cerr << "[ERROR BME280] Failed to read calibration matrix." << std::endl;
        return false;
    }

    // 4. Configurar Oversampling y Modo de Energía
    uint8_t ctrl_hum[2] = {BME280_REG_CTRL_HUM, 0x01};  // Humedad x1
    if (write(file_descriptor_, ctrl_hum, 2) != 2) return false;

    // Control de medidas: Temp x1, Presión x1, Modo Normal (11)
    // 001 (Temp x1) | 001 (Press x1) | 11 (Normal mode) -> 0x27
    uint8_t ctrl_meas[2] = {BME280_REG_CTRL_MEAS, 0x27}; 
    if (write(file_descriptor_, ctrl_meas, 2) != 2) return false;

    initialized_ = true;
    return true;
}

bool BME280::read_sensor_data(float& temp, float& hum) {
    if (!initialized_) return false;

    // Leemos 8 bytes empezando en 0xF7 (Press, Temp, Hum)
    uint8_t reg = BME280_REG_DATA;
    if (write(file_descriptor_, &reg, 1) != 1) return false;

    uint8_t data[8];
    if (read(file_descriptor_, data, 8) != 8) return false;

    // Extraemos los datos crudos (Shift de bits)
    int32_t adc_T = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);
    int32_t adc_H = (data[6] << 8) | data[7];

    // Compensamos los datos con las fórmulas de Bosch
    temp = compensate_temperature(adc_T) / 100.0f;
    hum = compensate_humidity(adc_H) / 1024.0f;

    return true;
}

bool BME280::read_calibration_data() {
    uint8_t reg = 0x88;
    if (write(file_descriptor_, &reg, 1) != 1) return false;
    
    uint8_t raw[26]; // Datos de temp y presión
    if (read(file_descriptor_, raw, 24) != 24) return false;

    calib_.dig_T1 = (raw[1] << 8) | raw[0];
    calib_.dig_T2 = (raw[3] << 8) | raw[2];
    calib_.dig_T3 = (raw[5] << 8) | raw[4];

    // Leer coeficientes de Humedad
    reg = 0xA1;
    if (write(file_descriptor_, &reg, 1) != 1) return false;
    uint8_t h1;
    if (read(file_descriptor_, &h1, 1) != 1) return false;
    calib_.dig_H1 = h1;

    reg = 0xE1;
    if (write(file_descriptor_, &reg, 1) != 1) return false;
    uint8_t h_raw[7];
    if (read(file_descriptor_, h_raw, 7) != 7) return false;

    calib_.dig_H2 = (h_raw[1] << 8) | h_raw[0];
    calib_.dig_H3 = h_raw[2];
    calib_.dig_H4 = (h_raw[3] << 4) | (h_raw[4] & 0x0F);
    calib_.dig_H5 = (h_raw[5] << 4) | (h_raw[4] >> 4);
    calib_.dig_H6 = (int8_t)h_raw[6];

    return true;
}

// Fórmulas matemáticas estándar de la hoja de datos de Bosch Sensortec
int32_t BME280::compensate_temperature(int32_t adc_T) {
    int32_t var1, var2, T;
    var1 = ((((adc_T >> 3) - ((int32_t)calib_.dig_T1 << 1))) * ((int32_t)calib_.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)calib_.dig_T1)) * ((adc_T >> 4) - ((int32_t)calib_.dig_T1))) >> 12) * ((int32_t)calib_.dig_T3)) >> 14;
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
    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
    return (uint32_t)(v_x1_u32r >> 12);
}