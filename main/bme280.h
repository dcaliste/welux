#ifndef BME280_H
#define BME280_H

#include <driver/i2c_master.h>

typedef struct {
    i2c_master_dev_handle_t dev;

    uint16_t dig_T1;
    int16_t dig_T2, dig_T3;

    uint16_t dig_P1;
    int16_t dig_P2, dig_P3, dig_P4, dig_P5;
    int16_t dig_P6, dig_P7, dig_P8, dig_P9;

    uint8_t dig_H1, dig_H3;
    int16_t dig_H2, dig_H4, dig_H5;
    int8_t dig_H6;

    int32_t adcT, adcH, adcP, t_fine;
} Bme280Device;

typedef enum {
    BME280_OVERSAMPLING_NONE,
    BME280_OVERSAMPLING_1x,
    BME280_OVERSAMPLING_2x,
    BME280_OVERSAMPLING_4x,
    BME280_OVERSAMPLING_8x,
    BME280_OVERSAMPLING_16x
} Bme280OversamplingFactors;

void bme280_add_to_bus(i2c_master_bus_handle_t bus, Bme280Device *dev, uint8_t addr);
void bme280_remove_from_bus(Bme280Device *dev);

void bme280_force_read(Bme280Device *dev, Bme280OversamplingFactors oversampling);
float bme280_get_temperature(Bme280Device *dev);
float bme280_get_pressure(Bme280Device *dev);
float bme280_get_humidity(Bme280Device *dev);

#endif
