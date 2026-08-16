#include "bme280.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

static const char *TAG = "bme280";

static uint8_t read_reg(i2c_master_dev_handle_t dev, uint8_t reg)
{
    uint8_t value;

    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev, &reg, 1, &value, 1, 1000));
    return value;
}

static void wait_for_status(Bme280Device *dev, uint8_t waitingStatus)
{
    const uint8_t reg = 0xF3;
    uint8_t status = 0;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev->dev, &reg, 1, &status, 1, 1000));
    while (status & waitingStatus) {
        ESP_ERROR_CHECK(i2c_master_transmit_receive(dev->dev, &reg, 1, &status, 1, 1000));
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void bme280_add_to_bus(i2c_master_bus_handle_t bus, Bme280Device *dev, uint8_t addr)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_probe(bus, addr, 1000));
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_config, &dev->dev));

    wait_for_status(dev, 0x01);

    ESP_LOGI(TAG, "ID: 0x%02x", read_reg(dev->dev, 0xD0));

    const uint8_t digT = 0x88;
    uint8_t tmpT[6];
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev->dev, &digT, 1, tmpT, sizeof(tmpT) / sizeof(uint8_t), 1000));
    dev->dig_T1 = tmpT[0] | ((int16_t)tmpT[1] << 8);
    dev->dig_T2 = tmpT[2] | ((uint16_t)tmpT[3] << 8);
    dev->dig_T3 = tmpT[4] | ((uint16_t)tmpT[4] << 8);
    ESP_LOGD(TAG, "T compensation parameters: %d %d %d", dev->dig_T1, dev->dig_T2, dev->dig_T3);

    const uint8_t digP = 0x8E;
    uint8_t tmpP[18];
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev->dev, &digP, 1, tmpP, sizeof(tmpP) / sizeof(uint8_t), 1000));
    dev->dig_P1 = tmpP[0] | ((uint16_t)tmpP[1] << 8);
    dev->dig_P2 = tmpP[2] | ((int16_t)tmpP[3] << 8);
    dev->dig_P3 = tmpP[4] | ((int16_t)tmpP[5] << 8);
    dev->dig_P4 = tmpP[6] | ((int16_t)tmpP[7] << 8);
    dev->dig_P5 = tmpP[8] | ((int16_t)tmpP[9] << 8);
    dev->dig_P6 = tmpP[10] | ((int16_t)tmpP[11] << 8);
    dev->dig_P7 = tmpP[12] | ((int16_t)tmpP[13] << 8);
    dev->dig_P8 = tmpP[14] | ((int16_t)tmpP[15] << 8);
    dev->dig_P9 = tmpP[16] | ((int16_t)tmpP[17] << 8);
    ESP_LOGD(TAG, "P compensation parameters: %d %d %d %d %d %d %d %d %d",
             dev->dig_P1, dev->dig_P2, dev->dig_P3, dev->dig_P4, dev->dig_P5, dev->dig_P6,
             dev->dig_P7, dev->dig_P8, dev->dig_P9);

    const uint8_t digH1 = 0xA1;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev->dev, &digH1, 1, &dev->dig_H1, 1, 1000));
    const uint8_t digH = 0xE1;
    uint8_t tmpH[7];
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev->dev, &digH, 1, tmpH, sizeof(tmpH) / sizeof(uint8_t), 1000));
    dev->dig_H2 = tmpH[0] | ((int16_t)tmpH[1] << 8);
    dev->dig_H3 = tmpH[2];
    dev->dig_H4 = (((uint16_t)tmpH[3] << 4) | ((uint16_t)(tmpH[4] & 0x0F))) & 0x0FFF;
    dev->dig_H5 = (((uint16_t)tmpH[5] << 4) | ((uint16_t)((tmpH[4] >> 4) & 0x0F))) & 0x0FFF;
    dev->dig_H6 = (int8_t)tmpH[6];
    ESP_LOGD(TAG, "H compensation parameters: %d %d %d %d %d %d",
             dev->dig_H1, dev->dig_H2, dev->dig_H3, dev->dig_H4, dev->dig_H5, dev->dig_H6);
}

void ina226_remove_from_bus(Bme280Device *dev)
{
    ESP_ERROR_CHECK(i2c_master_bus_rm_device(dev->dev));
}

void bme280_force_read(Bme280Device *dev, Bme280OversamplingFactors oversampling)
{
    uint8_t load[2];
    load[0] = 0xF2;
    load[1] = (uint8_t)oversampling;
    ESP_ERROR_CHECK(i2c_master_transmit(dev->dev, load, sizeof(load) / sizeof(uint8_t), 1000));
    load[0] = 0xF4;
    load[1] = ((uint8_t)oversampling) << 6;
    load[1] |= ((uint8_t)oversampling) << 3;
    load[1] |= 1; // Force measuring
    ESP_ERROR_CHECK(i2c_master_transmit(dev->dev, load, sizeof(load) / sizeof(uint8_t), 1000));
    // Wait for measure to be finished
    wait_for_status(dev, 0x08);

    const uint8_t reg = 0xF7;
    uint8_t tmp[8];
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev->dev, &reg, 1, tmp, 8, 1000));

    dev->adcP = 0;
    dev->adcP |= ((int32_t)tmp[0] << 16);
    dev->adcP |= ((int32_t)tmp[1] << 8);
    dev->adcP |= (int32_t)tmp[2];
    dev->adcP >>= 4;

    dev->adcT = 0;
    dev->adcT |= ((int32_t)tmp[3] << 16);
    dev->adcT |= ((int32_t)tmp[4] << 8);
    dev->adcT |= (int32_t)tmp[5];
    dev->adcT >>= 4;

    dev->adcH = 0;
    dev->adcH |= ((int32_t)tmp[6] << 8);
    dev->adcH |= (int32_t)tmp[7];

    int32_t var1, var2;
    var1 = ((((dev->adcT>>3) - ((int32_t)dev->dig_T1<<1))) * ((int32_t)dev->dig_T2)) >> 11;
    var2 = (((((dev->adcT>>4) - ((int32_t)dev->dig_T1)) * ((dev->adcT>>4) - ((int32_t)dev->dig_T1)))
             >> 12) *
            ((int32_t)dev->dig_T3)) >> 14;
    dev->t_fine = var1 + var2;
}

float bme280_get_temperature(Bme280Device *dev)
{
    return ((dev->t_fine * 5 + 128) >> 8) * 0.01;
}

float bme280_get_pressure(Bme280Device *dev)
{
    int64_t var1, var2, p;
    var1 = ((int64_t)dev->t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)dev->dig_P6;
    var2 = var2 + ((var1*(int64_t)dev->dig_P5)<<17);
    var2 = var2 + (((int64_t)dev->dig_P4)<<35);
    var1 = ((var1 * var1 * (int64_t)dev->dig_P3)>>8) + ((var1 * (int64_t)dev->dig_P2)<<12);
    var1 = (((((int64_t)1)<<47)+var1))*((int64_t)dev->dig_P1)>>33;
    if (var1 == 0)
        return 0; // avoid exception caused by division by zero
    p = 1048576 - dev->adcP;
    p = (((p<<31)-var2)*3125)/var1;
    var1 = (((int64_t)dev->dig_P9) * (p>>13) * (p>>13)) >> 25;
    var2 = (((int64_t)dev->dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)dev->dig_P7)<<4);
    return ((uint32_t)p) / 256.;
}

float bme280_get_humidity(Bme280Device *dev)
{
    int32_t v_x1_u32r;
    v_x1_u32r = (dev->t_fine - ((int32_t)76800));
    v_x1_u32r = (((((dev->adcH << 14) - (((int32_t)dev->dig_H4) << 20)
                    - (((int32_t)dev->dig_H5) *
                       v_x1_u32r)) + ((int32_t)16384)) >> 15)
                 * (((((((v_x1_u32r *
                          ((int32_t)dev->dig_H6)) >> 10) * (((v_x1_u32r * ((int32_t)dev->dig_H3)) >> 11) +
                                                            ((int32_t)32768))) >> 10) + ((int32_t)2097152)) * ((int32_t)dev->dig_H2) +
                     8192) >> 14));
    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                               ((int32_t)dev->dig_H1)) >> 4));
    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
    return ((uint32_t)(v_x1_u32r>>12)) / 1024.;
}
