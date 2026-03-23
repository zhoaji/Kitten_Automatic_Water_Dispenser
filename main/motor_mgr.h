#ifndef MOTOR_MGR_H
#define MOTOR_MGR_H

#include <stdint.h>
#include "esp_err.h"

esp_err_t motor_mgr_init(void);
esp_err_t motor_mgr_set_duty(uint32_t duty_percent); // 0-100%

#endif // MOTOR_MGR_H
