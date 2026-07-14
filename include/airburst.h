#ifndef AIRBURST_H
#define AIRBURST_H

#include <stdbool.h>

bool airburst_init(void);
void airburst_destroy(void);

bool airburst_distance_init(void);
float airburst_get_distance_m(void);
bool airburst_distance_destroy(void);

void airburst_test_init (void);
bool airburst_test (void);
void airburst_test_destroy(void);

bool airburst_arm_bomb(void);
bool airburst_bust_bomb(void);

#endif //AIRBURST_H
