#ifndef AIRBURST_H
#define AIRBURST_H

#include <stdbool.h>

bool airburst_init(void);

float airburst_get_distance_m(void);
bool airburst_arm_bomb(void);
bool airburst_bust_bomb(void);

bool airburst_destroy(void);

#endif //AIRBURST_H
