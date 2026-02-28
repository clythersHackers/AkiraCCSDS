/*
 * Copyright (c) 2025 AkiraOS Contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define LOG_MODULE_NAME akira_sensor
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(akira_sensor, CONFIG_AKIRA_LOG_LEVEL);

/**
 * @file akira_sensor_api.c
 * @brief Unified sensor read API for WASM applications
 *
 * Dispatches sensor_read(type) to the appropriate hardware driver:
 *   TEMP / HUMIDITY / PRESSURE  →  BME280    (CONFIG_AKIRA_BME280)
 *   ACCEL_X/Y/Z / GYRO_X/Y/Z   →  LSM6DS3   (CONFIG_LSM6DSL — Zephyr driver)
 *
 * The LSM6DS3TR-C shares WHO_AM_I 0x6A with LSM6DSL so Zephyr's built-in
 * lsm6dsl driver handles it without any custom C driver files.
 *
 * Return values are scaled ×1000 as int32 because WAMR passes i32 only.
 */

#include "akira_api.h"
#include "akira_sensor_api.h"
#include <runtime/security.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdbool.h>
#include <errno.h>

#ifdef CONFIG_AKIRA_BME280
#include <drivers/sensors/bme280.h>
#endif

/* -------------------------------------------------------------------------- */
/* IMU — Zephyr lsm6dsl driver                                                */
/* -------------------------------------------------------------------------- */

#ifdef CONFIG_LSM6DSL

static const struct device *s_imu_dev;
static bool s_imu_ready;

static int imu_init(void)
{
    if (s_imu_ready) {
        return 0;
    }

    s_imu_dev = DEVICE_DT_GET(DT_NODELABEL(lsm6ds3));
    if (!device_is_ready(s_imu_dev)) {
        LOG_ERR("imu_init: lsm6dsl device not ready");
        s_imu_dev = NULL;
        return -ENODEV;
    }

    s_imu_ready = true;
    LOG_INF("imu_init: lsm6dsl ready");
    return 0;
}

/** Read one accel or gyro channel via Zephyr sensor API. */
static float imu_read_chan(enum sensor_channel chan)
{
    struct sensor_value val;

    if (sensor_sample_fetch_chan(s_imu_dev, chan) < 0) {
        return 0.0f;
    }
    sensor_channel_get(s_imu_dev, chan, &val);
    return (float)sensor_value_to_double(&val);
}

#endif /* CONFIG_LSM6DSL */

/* -------------------------------------------------------------------------- */
/* BME280 singleton                                                            */
/* -------------------------------------------------------------------------- */

#ifdef CONFIG_AKIRA_BME280

#define AKIRA_BME280_ADDR 0x76

static struct bme280_config s_env_cfg;
static bool s_env_ready = false;

static int env_init(void)
{
    if (s_env_ready) {
        return 0;
    }

    const struct device *i2c = device_get_binding("i2c0");
    if (!i2c || !device_is_ready(i2c)) {
        LOG_ERR("env_init: i2c0 not ready");
        return -ENODEV;
    }

    s_env_cfg = (struct bme280_config){
        .i2c_dev  = i2c,
        .i2c_addr = AKIRA_BME280_ADDR,
    };

    int ret = bme280_init(&s_env_cfg);
    if (ret < 0) {
        LOG_ERR("env_init: bme280_init failed: %d", ret);
        return ret;
    }

    s_env_ready = true;
    LOG_INF("env_init: BME280 @ 0x%02x ready", AKIRA_BME280_ADDR);
    return 0;
}

#endif /* CONFIG_AKIRA_BME280 */

/* -------------------------------------------------------------------------- */
/* Public C-level sensor API                                                   */
/* -------------------------------------------------------------------------- */

int akira_sensor_read(akira_sensor_type_t type, float *value)
{
    if (!value) {
        return -EINVAL;
    }

    switch (type) {

#ifdef CONFIG_AKIRA_BME280
    case SENSOR_TYPE_TEMP: {
        if (env_init() < 0) { return -ENODEV; }
        struct bme280_data d;
        int ret = bme280_read(&d);
        if (ret < 0) { return ret; }
        *value = d.temperature;
        return 0;
    }
    case SENSOR_TYPE_HUMIDITY: {
        if (env_init() < 0) { return -ENODEV; }
        struct bme280_data d;
        int ret = bme280_read(&d);
        if (ret < 0) { return ret; }
        *value = d.humidity;
        return 0;
    }
    case SENSOR_TYPE_PRESSURE: {
        if (env_init() < 0) { return -ENODEV; }
        struct bme280_data d;
        int ret = bme280_read(&d);
        if (ret < 0) { return ret; }
        *value = d.pressure;
        return 0;
    }
#endif /* CONFIG_AKIRA_BME280 */

#ifdef CONFIG_LSM6DSL
    case SENSOR_TYPE_ACCEL_X:
    case SENSOR_TYPE_ACCEL_Y:
    case SENSOR_TYPE_ACCEL_Z: {
        if (imu_init() < 0) { return -ENODEV; }
        enum sensor_channel ch = (type == SENSOR_TYPE_ACCEL_X) ? SENSOR_CHAN_ACCEL_X :
                                 (type == SENSOR_TYPE_ACCEL_Y) ? SENSOR_CHAN_ACCEL_Y :
                                                                  SENSOR_CHAN_ACCEL_Z;
        *value = imu_read_chan(ch);
        return 0;
    }
    case SENSOR_TYPE_GYRO_X:
    case SENSOR_TYPE_GYRO_Y:
    case SENSOR_TYPE_GYRO_Z: {
        if (imu_init() < 0) { return -ENODEV; }
        enum sensor_channel ch = (type == SENSOR_TYPE_GYRO_X) ? SENSOR_CHAN_GYRO_X :
                                 (type == SENSOR_TYPE_GYRO_Y) ? SENSOR_CHAN_GYRO_Y :
                                                                 SENSOR_CHAN_GYRO_Z;
        *value = imu_read_chan(ch);
        return 0;
    }
#endif /* CONFIG_LSM6DSL */

    default:
        LOG_WRN("sensor_read: unsupported type=%d", (int)type);
        return -ENOTSUP;
    }
}

int akira_sensor_read_imu(akira_imu_data_t *data)
{
    if (!data) {
        return -EINVAL;
    }

#ifdef CONFIG_LSM6DSL
    if (imu_init() < 0) {
        return -ENODEV;
    }
    /* Fetch all accel+gyro channels in one hardware transaction */
    sensor_sample_fetch(s_imu_dev);

    struct sensor_value ax, ay, az, gx, gy, gz;
    sensor_channel_get(s_imu_dev, SENSOR_CHAN_ACCEL_X, &ax);
    sensor_channel_get(s_imu_dev, SENSOR_CHAN_ACCEL_Y, &ay);
    sensor_channel_get(s_imu_dev, SENSOR_CHAN_ACCEL_Z, &az);
    sensor_channel_get(s_imu_dev, SENSOR_CHAN_GYRO_X,  &gx);
    sensor_channel_get(s_imu_dev, SENSOR_CHAN_GYRO_Y,  &gy);
    sensor_channel_get(s_imu_dev, SENSOR_CHAN_GYRO_Z,  &gz);

    data->accel_x = (float)sensor_value_to_double(&ax);
    data->accel_y = (float)sensor_value_to_double(&ay);
    data->accel_z = (float)sensor_value_to_double(&az);
    data->gyro_x  = (float)sensor_value_to_double(&gx);
    data->gyro_y  = (float)sensor_value_to_double(&gy);
    data->gyro_z  = (float)sensor_value_to_double(&gz);
    return 0;
#else
    data->accel_x = 0.0f; data->accel_y = 0.0f; data->accel_z = 9.81f;
    data->gyro_x  = 0.0f; data->gyro_y  = 0.0f; data->gyro_z  = 0.0f;
    return 0;
#endif
}

int akira_sensor_read_env(akira_env_data_t *data)
{
    if (!data) {
        return -EINVAL;
    }

#ifdef CONFIG_AKIRA_BME280
    if (env_init() < 0) { return -ENODEV; }
    struct bme280_data d;
    int ret = bme280_read(&d);
    if (ret < 0) { return ret; }
    data->temperature = d.temperature;
    data->humidity    = d.humidity;
    data->pressure    = d.pressure;
    return 0;
#else
    data->temperature = 25.0f;
    data->humidity    = 50.0f;
    data->pressure    = 1013.25f;
    return 0;
#endif
}

int akira_sensor_read_power(akira_power_data_t *data)
{
    if (!data) {
        return -EINVAL;
    }
    /* INA219 support placeholder */
    data->voltage = 3.7f;
    data->current = 0.15f;
    data->power   = 0.555f;
    return 0;
}

/* -------------------------------------------------------------------------- */
/* WASM native export                                                          */
/* -------------------------------------------------------------------------- */

#ifdef CONFIG_AKIRA_WASM_RUNTIME

int akira_native_sensor_read(wasm_exec_env_t exec_env, int32_t type)
{
    AKIRA_CHECK_CAP_OR_RETURN(exec_env, AKIRA_CAP_SENSOR_READ, -EPERM);

    float v = 0.0f;
    int ret = akira_sensor_read((akira_sensor_type_t)type, &v);
    if (ret < 0) {
        LOG_DBG("sensor_read: type=%d err=%d", type, ret);
        return ret;
    }
    /* Scale ×1000 — callers divide by 1000.0 to recover float */
    return (int32_t)(v * 1000.0f);
}

#endif /* CONFIG_AKIRA_WASM_RUNTIME */