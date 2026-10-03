/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Endpoint that carries the light clusters in lighting-app.zap. */
#define LIGHT_ENDPOINT_ID 1

/*
 * Hardware boundary of the application. Everything Matter-side calls only
 * these functions; all of them are stubs that log and remember the value.
 * All calls arrive on the Matter thread.
 */
namespace LightDriver {

int Init();
void SetOn(bool on);
void SetAll(uint8_t scale, uint8_t r, uint8_t g, uint8_t b, uint8_t cw, uint8_t ww);
void SetLevel(uint8_t level);
void SetColorXY(uint16_t x, uint16_t y);
void SetColorHS(uint8_t hue, uint8_t sat);
void SetColorTemperature(uint16_t mireds);

} // namespace LightDriver
