// Copyright (c) Acconeer AB, 2021-2023
// All rights reserved
// This file is subject to the terms and conditions defined in the file
// 'LICENSES/license_acconeer.txt', (BSD 3-Clause License) which is part
// of this source code package.

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "acc_definitions_common.h"
#include "acc_hal_definitions.h"
#include "acc_hal_integration.h"
#include "acc_integration.h"
#include "acc_integration_log.h"
#include "acc_libgpiod.h"
#include "acc_libspi.h"


#define SENSOR_COUNT (4) /**< @brief The number of sensors available on the board */

#define PIN_PMU_EN (17) /**< @brief PMU_EN BCM:17 J5:11 */

#define PIN_SPI_ENABLE_S1_N (18) /**< @brief SPI S1 enable BCM:18 J5:12 */
#define PIN_SPI_ENABLE_S2_N (27) /**< @brief SPI S2 enable BCM:27 J5:13 */
#define PIN_SPI_ENABLE_S3_N (22) /**< @brief SPI S3 enable BCM:22 J5:15 */
#define PIN_SPI_ENABLE_S4_N (7)  /**< @brief SPI S4 enable BCM:7 J5:26 */

#define PIN_ENABLE_N      (6)  /**< @brief Gpio Enable BCM:4 J5:31 */
#define PIN_ENABLE_S1_3V3 (23) /**< @brief Gpio Enable S1 BCM:23 J5:16 */
#define PIN_ENABLE_S2_3V3 (5)  /**< @brief Gpio Enable S2 BCM:5 J5:29 */
#define PIN_ENABLE_S3_3V3 (12) /**< @brief Gpio Enable S3 BCM:12 J5:32 */
#define PIN_ENABLE_S4_3V3 (26) /**< @brief Gpio Enable S4 BCM:26 J5:37 */

#define PIN_INTERRUPT_S1_3V3 (20) /**< @brief Gpio Interrupt S1 BCM:20 J5:38, connect to sensor 1 GPIO 5 */
#define PIN_INTERRUPT_S2_3V3 (21) /**< @brief Gpio Interrupt S2 BCM:21 J5:40, connect to sensor 2 GPIO 5 */
#define PIN_INTERRUPT_S3_3V3 (24) /**< @brief Gpio Interrupt S3 BCM:24 J5:18, connect to sensor 3 GPIO 5 */
#define PIN_INTERRUPT_S4_3V3 (25) /**< @brief Gpio Interrupt S4 BCM:25 J5:22, connect to sensor 4 GPIO 5 */

#define ACC_BOARD_SPI_SPEED (15000000) /**< @brief The SPI speed of this board */
#define ACC_BOARD_BUS       (0)        /**< @brief The SPI bus of this board */
#define ACC_BOARD_CS        (0)        /**< @brief The SPI device of the board */

#define ACC_BOARD_REF_FREQ (24000000)  /**< @brief The reference frequency assumes 26 MHz on reference board */


static bool init_done;


/**
 * @brief Sensor states
 */
typedef enum
{
	SENSOR_DISABLED,
	SENSOR_ENABLED,
	SENSOR_ENABLED_AND_SELECTED
} acc_board_sensor_state_t;

typedef struct
{
	acc_board_sensor_state_t state;
	const int                enable_pin;
	const int                slave_select_pin;
	const int                interrupt_pin;
} acc_sensor_info_t;

static acc_sensor_info_t sensor_infos[SENSOR_COUNT] = {
	{.state            = SENSOR_DISABLED,
	 .enable_pin       = PIN_ENABLE_S1_3V3,
	 .slave_select_pin = PIN_SPI_ENABLE_S1_N,
	 .interrupt_pin    = PIN_INTERRUPT_S1_3V3},
	{.state            = SENSOR_DISABLED,
	 .enable_pin       = PIN_ENABLE_S2_3V3,
	 .slave_select_pin = PIN_SPI_ENABLE_S2_N,
	 .interrupt_pin    = PIN_INTERRUPT_S2_3V3},
	{.state            = SENSOR_DISABLED,
	 .enable_pin       = PIN_ENABLE_S3_3V3,
	 .slave_select_pin = PIN_SPI_ENABLE_S3_N,
	 .interrupt_pin    = PIN_INTERRUPT_S3_3V3},
	{.state            = SENSOR_DISABLED,
	 .enable_pin       = PIN_ENABLE_S4_3V3,
	 .slave_select_pin = PIN_SPI_ENABLE_S4_N,
	 .interrupt_pin    = PIN_INTERRUPT_S4_3V3}
};

static const gpio_config_t pin_config[] =
{
	{PIN_PMU_EN, GPIO_DIR_OUTPUT_LOW},
	{PIN_SPI_ENABLE_S1_N, GPIO_DIR_OUTPUT_HIGH},
	{PIN_SPI_ENABLE_S2_N, GPIO_DIR_OUTPUT_HIGH},
	{PIN_SPI_ENABLE_S3_N, GPIO_DIR_OUTPUT_HIGH},
	{PIN_SPI_ENABLE_S4_N, GPIO_DIR_OUTPUT_HIGH},
	{PIN_ENABLE_N, GPIO_DIR_OUTPUT_HIGH},
	{PIN_ENABLE_S1_3V3, GPIO_DIR_OUTPUT_LOW},
	{PIN_ENABLE_S2_3V3, GPIO_DIR_OUTPUT_LOW},
	{PIN_ENABLE_S3_3V3, GPIO_DIR_OUTPUT_LOW},
	{PIN_ENABLE_S4_3V3, GPIO_DIR_OUTPUT_LOW},
	{PIN_INTERRUPT_S1_3V3, GPIO_DIR_INPUT_INTERRUPT},
	{PIN_INTERRUPT_S2_3V3, GPIO_DIR_INPUT_INTERRUPT},
	{PIN_INTERRUPT_S3_3V3, GPIO_DIR_INPUT_INTERRUPT},
	{PIN_INTERRUPT_S4_3V3, GPIO_DIR_INPUT_INTERRUPT},
	{0, GPIO_DIR_UNKNOWN}
};

static uint32_t spi_speed = ACC_BOARD_SPI_SPEED;

static pthread_mutex_t spi_mutex;


/**
 * @brief Get the combined status of all sensors
 *
 * @return False if any sensor is busy
 */
static bool all_sensors_inactive(void)
{
	for (uint8_t sensor_index = 0; sensor_index < SENSOR_COUNT; sensor_index++)
	{
		if (sensor_infos[sensor_index].state != SENSOR_DISABLED)
		{
			return false;
		}
	}

	return true;
}


static void board_deinit(void)
{
	acc_libgpiod_deinit();
	acc_libspi_deinit();
	pthread_mutex_destroy(&spi_mutex);
	init_done = false;
}


static bool acc_board_init(void)
{
	bool result = true;

	if (init_done)
	{
		return true;
	}

	if (atexit(board_deinit))
	{
		fprintf(stderr, "Unable to set exit function 'board_deinit()'\n");
		result = false;
	}

	if (result)
	{
		result = acc_libspi_init();
	}

	if (result)
	{
		int res = pthread_mutex_init(&spi_mutex, NULL);
		if (res != 0)
		{
			printf("pthread_mutex_init failed: %s\n", strerror(res));
			result = false;
		}
	}

	if (result)
	{
		result = acc_libgpiod_init(pin_config);
	}

	if (result)
	{
		init_done = true;
	}

	return result;
}


static void acc_board_start_sensor(acc_sensor_id_t sensor_id)
{
	assert(sensor_id <= SENSOR_COUNT);
	acc_sensor_info_t *sensor_info = &sensor_infos[sensor_id - 1];

	if (sensor_info->state != SENSOR_DISABLED)
	{
		return;
	}

	if (all_sensors_inactive())
	{
		// No active sensors yet, set pmu high to start the board

		if (!acc_libgpiod_set(PIN_PMU_EN, PIN_HIGH))
		{
			fprintf(stderr, "%s: Unable to activate PIN_PMU_EN.\n", __func__);
			assert(false);
		}

		// Wait for the board to power up
		acc_integration_sleep_ms(5);

		if (!acc_libgpiod_set(PIN_ENABLE_N, PIN_LOW))
		{
			fprintf(stderr, "%s: Unable to activate PIN_ENABLE_N.\n", __func__);
			assert(false);
		}

		acc_integration_sleep_ms(5);
	}

	if (!acc_libgpiod_set(sensor_info->enable_pin, PIN_HIGH))
	{
		fprintf(stderr, "%s: Unable to activate enable_pin for sensor %" PRIsensor_id ".\n", __func__, sensor_id);
		assert(false);
	}

	acc_integration_sleep_ms(5);

	sensor_info->state = SENSOR_ENABLED;
}


static void acc_board_stop_sensor(acc_sensor_id_t sensor_id)
{
	assert(sensor_id <= SENSOR_COUNT);
	acc_sensor_info_t *sensor_info = &sensor_infos[sensor_id - 1];

	if (sensor_info->state != SENSOR_DISABLED)
	{
		// "unselect" spi slave select
		if (sensor_info->state == SENSOR_ENABLED_AND_SELECTED)
		{
			if (!acc_libgpiod_set(sensor_info->slave_select_pin, PIN_HIGH))
			{
				fprintf(stderr, "%s: Unable to deactivate slave_select_pin for sensor_id %" PRIsensor_id ".\n", __func__, sensor_id);
				assert(false);
			}
		}

		// Disable sensor
		if (!acc_libgpiod_set(sensor_info->enable_pin, PIN_LOW))
		{
			// Set the state to enabled since it is not selected and failed to disable
			sensor_info->state = SENSOR_ENABLED;
			fprintf(stderr, "%s: Unable to deactivate enable_pin for sensor %" PRIsensor_id ".\n", __func__, sensor_id);
			assert(false);
		}

		sensor_info->state = SENSOR_DISABLED;
	}

	if (all_sensors_inactive())
	{
		// No active sensors, shut down the board to save power
		acc_libgpiod_set(PIN_ENABLE_N, PIN_HIGH);
		acc_libgpiod_set(PIN_PMU_EN, PIN_LOW);
	}

	// Wait after power off to leave the sensor in a known state
	// in case the application intends to enable the sensor directly
	acc_integration_sleep_ms(5);
}


static bool acc_board_chip_select(acc_sensor_id_t sensor_id, uint8_t cs_assert)
{
	assert(sensor_id <= SENSOR_COUNT);
	acc_sensor_info_t *sensor_info = &sensor_infos[sensor_id - 1];

	if (cs_assert != 0)
	{
		if (sensor_info->state == SENSOR_ENABLED)
		{
			// Since only one sensor can be active, loop through all the other sensors and deselect the active one
			for (uint8_t i = 0; i < SENSOR_COUNT; i++)
			{
				if ((i != (sensor_id - 1)) && (sensor_infos[i].state == SENSOR_ENABLED_AND_SELECTED))
				{
					if (!acc_libgpiod_set(sensor_infos[i].slave_select_pin, PIN_HIGH))
					{
						fprintf(stderr, "%s: Unable to deactivate slave_select_pin for sensor %" PRIsensor_id ".\n", __func__,
						        sensor_id);
						return false;
					}

					sensor_infos[i].state = SENSOR_ENABLED;
				}
			}

			// Select the sensor
			if (!acc_libgpiod_set(sensor_info->slave_select_pin, PIN_LOW))
			{
				fprintf(stderr, "%s: Unable to activate slave_select_pin for sensor %" PRIsensor_id ".\n", __func__, sensor_id);
				return false;
			}

			sensor_info->state = SENSOR_ENABLED_AND_SELECTED;
			return true;
		}
		else if (sensor_info->state == SENSOR_DISABLED)
		{
			fprintf(stderr, "%s: Failure, sensor %" PRIsensor_id " is disabled.\n", __func__, sensor_id);
			return false;
		}
		else if (sensor_info->state == SENSOR_ENABLED_AND_SELECTED)
		{
			fprintf(stdout, "%s: Sensor %" PRIsensor_id " is already selected.\n", __func__, sensor_id);
			return true;
		}
		else
		{
			fprintf(stderr, "%s: Unknown state when selecting sensor %" PRIsensor_id ".\n", __func__, sensor_id);
			return false;
		}
	}
	else
	{
		if (sensor_info->state == SENSOR_ENABLED_AND_SELECTED)
		{
			if (!acc_libgpiod_set(sensor_info->slave_select_pin, PIN_HIGH))
			{
				fprintf(stderr, "%s: Unable to deactivate slave_select_pin for sensor %" PRIsensor_id ".\n", __func__, sensor_id);
				return false;
			}

			sensor_info->state = SENSOR_ENABLED;
		}
	}

	return true;
}


static bool acc_board_wait_for_sensor_interrupt(acc_sensor_id_t sensor_id, uint32_t timeout_ms)
{
	assert(sensor_id <= SENSOR_COUNT);
	return acc_libgpiod_wait_for_interrupt(sensor_infos[sensor_id - 1].interrupt_pin, timeout_ms);
}


static void acc_board_sensor_transfer(acc_sensor_id_t sensor_id, uint8_t *buffer, size_t buffer_length)
{
	assert(sensor_id <= SENSOR_COUNT);
	bool result = pthread_mutex_lock(&spi_mutex) == 0;
	assert(result);

	result = acc_board_chip_select(sensor_id, 1);
	assert(result);

	result = acc_libspi_transfer(spi_speed, buffer, buffer_length);
	assert(result);

	result = acc_board_chip_select(sensor_id, 0);
	assert(result);

	result = pthread_mutex_unlock(&spi_mutex) == 0;
	assert(result);
	(void)result;
}


static float acc_board_get_ref_freq(void)
{
	return ACC_BOARD_REF_FREQ;
}


const acc_hal_t *acc_hal_integration_get_implementation(void)
{
	if (!acc_board_init())
	{
		return NULL;
	}

	static acc_hal_t hal =
	{
		.properties.sensor_count          = SENSOR_COUNT,
		.properties.max_spi_transfer_size = 0,

		.sensor_device.power_on                = acc_board_start_sensor,
		.sensor_device.power_off               = acc_board_stop_sensor,
		.sensor_device.wait_for_interrupt      = acc_board_wait_for_sensor_interrupt,
		.sensor_device.transfer                = acc_board_sensor_transfer,
		.sensor_device.get_reference_frequency = acc_board_get_ref_freq,

		.sensor_device.hibernate_enter = NULL,
		.sensor_device.hibernate_exit  = NULL,

		.os.mem_alloc = NULL,
		.os.mem_free  = NULL,
		.os.gettime   = NULL,

		.log.log_level = ACC_LOG_LEVEL_INFO,
		.log.log       = acc_integration_log,

		.optimization.transfer16 = NULL,
	};

	hal.properties.max_spi_transfer_size = MAX_SPI_TRANSFER_SIZE;

	hal.os.mem_alloc = malloc;
	hal.os.mem_free  = free;
	hal.os.gettime   = acc_integration_get_time;

	return &hal;
}
