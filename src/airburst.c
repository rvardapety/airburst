#include "airburst.h"

#include <stdio.h>
#include <stdlib.h>

#include "acc_detector_distance.h"
#include "acc_hal_definitions.h"
#include "acc_hal_integration.h"
#include "acc_rss.h"
#include "acc_rss_assembly_test.h"

#define DEFAULT_SENSOR_ID 2
#define EXAMPLE_HWAAS    (63)
#define EXAMPLE_START_M  (0.02f)
#define EXAMPLE_LENGTH_M (1.50f)
#define EXAMPLE_PROFILE  ACC_SERVICE_PROFILE_2

bool success = true;
acc_detector_distance_handle_t distance_handle;
acc_rss_assembly_test_configuration_t configuration;

static void set_config(acc_detector_distance_configuration_t distance_configuration) {
    acc_detector_distance_configuration_requested_start_set(distance_configuration, EXAMPLE_START_M);
    acc_detector_distance_configuration_requested_length_set(distance_configuration, EXAMPLE_LENGTH_M);
    acc_detector_distance_configuration_service_profile_set(distance_configuration, EXAMPLE_PROFILE);
    acc_detector_distance_configuration_hw_accelerated_average_samples_set(distance_configuration, EXAMPLE_HWAAS);
}

static bool run_test(acc_rss_assembly_test_configuration_t configuration)
{
    acc_rss_assembly_test_result_t test_results[ACC_RSS_ASSEMBLY_TEST_MAX_NUMBER_OF_TESTS];
    uint16_t                       nr_of_test_results = ACC_RSS_ASSEMBLY_TEST_MAX_NUMBER_OF_TESTS;
    bool                           all_passed         = true;

    if (!acc_rss_assembly_test(configuration, test_results, &nr_of_test_results))
    {
        printf("Airburst check test: Failed to complete\n");
        return false;
    }

    for (uint16_t i = 0; i < nr_of_test_results; i++)
    {
        const bool passed = test_results[i].test_passed;
        printf("Name: %s, result: %s\n", test_results[i].test_name, passed ? "Pass" : "Fail");

        if (!passed)
        {
            all_passed = false;
        }
    }

    return all_passed;
}

bool airburst_init(void) {
    const acc_hal_t *hal = acc_hal_integration_get_implementation();

    if (!acc_rss_activate(hal)) {
        printf("airburst_init() failed\n");
        return false;
    }

    return true;
}

void airburst_destroy(void) {
    acc_rss_deactivate();
}

bool airburst_distance_init(void) {
    acc_detector_distance_configuration_t distance_configuration = acc_detector_distance_configuration_create();

    if (distance_configuration == NULL) {
        printf("airburst_distance_init() failed\n");
        acc_rss_deactivate();
        return false;
    }

    set_config(distance_configuration);
    distance_handle = acc_detector_distance_create(distance_configuration);

    if (distance_handle == NULL) {
        printf("acc_detector_distance_create() failed\n");
        acc_detector_distance_configuration_destroy(&distance_configuration);
        acc_rss_deactivate();
        return false;
    }

    acc_detector_distance_configuration_destroy(&distance_configuration);

    if (!acc_detector_distance_activate(distance_handle)) {
        printf("acc_detector_distance_activate() failed\n");
        acc_detector_distance_destroy(&distance_handle);
        acc_rss_deactivate();
        return false;
    }

    return true;
}

float airburst_get_distance_m(void) {
    uint16_t number_of_peaks = 1;
    acc_detector_distance_result_t result[number_of_peaks];
    acc_detector_distance_result_info_t result_info;

    success = acc_detector_distance_get_next(distance_handle, result, number_of_peaks, &result_info);

    if (!success) {
        printf("airburst_get_distance_m() failed\n");
        return 0;
    }

    return result[0].distance_m;
}

bool airburst_distance_destroy(void) {
    bool deactivated = acc_detector_distance_deactivate(distance_handle);

    acc_detector_distance_destroy(&distance_handle);

    if (deactivated && success) {
        printf("Application finished OK\n");
        return false;
    }

    return true;
}

void airburst_test_init(void) {
    configuration = acc_rss_assembly_test_configuration_create();
    acc_rss_assembly_test_configuration_sensor_set(configuration, DEFAULT_SENSOR_ID);

    // Disable all tests (they are enabled by default)
    acc_rss_assembly_test_configuration_all_tests_disable(configuration);

    // Enable and run: write Read Test
    acc_rss_assembly_test_configuration_communication_write_read_test_enable(configuration);
}

bool airburst_test (void) {
    if (!run_test(configuration))
    {
        printf("Airbust check test: Write Read Test failed\n");
        acc_rss_assembly_test_configuration_destroy(&configuration);
        return true;
    }
    return false;
}

void airburst_test_destroy(void) {
    acc_rss_assembly_test_configuration_communication_write_read_test_disable(configuration);
}

bool airburst_arm_bomb(void) {
    return true;
}

bool airburst_bust_bomb(void) {
    return true;
}
