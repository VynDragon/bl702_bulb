/* SPDX-License-Identifier: Apache-2.0 */

#include "LightDriver.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/led.h>

#include <algorithm>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

namespace {

const struct device *const leds = DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(pwm_leds));

#define SQUARED_LIGHT(v, s)						\
	(((v) < 0.5f || (s) < 0.0f) ? 0.0f :				\
	(std::clamp((v) * (s) * (s) / 10000.0f, 1.0f, 100.0f) + 0.5f))

#define WHITE_GAIN(v) ((v) * 0.08f)

/* Limits of the two white LEDs, in mireds (1 000 000 / kelvin). */
#define COLD_WHITE_MIREDS	153.0f /* 6500 K */
#define WARM_WHITE_MIREDS	370.0f /* 2700 K */

#define PRINT_F(v) (int)(v), (int)((v) * 1000.0f) % 1000

struct {
	bool on;
	/* All 0 to 100 */
	float scale;
	float r;
	float g;
	float b;
	float cw; /* Cold White */
	float ww; /* Warm White */
} State = { false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, };

void apply()
{
	LOG_DBG("light: %s, %d.%03d", State.on ? "on" : "off", PRINT_F(State.scale));
	LOG_DBG("rgb = { %d.%03d, %d.%03d, %d.%03d }, cw = %d.%03d, ww = %d.%03d",
		PRINT_F(State.r), PRINT_F(State.g), PRINT_F(State.b),
		PRINT_F(State.cw), PRINT_F(State.ww));

	if (State.on) {
		led_set_brightness(leds, 0, SQUARED_LIGHT(State.r, State.scale));
		led_set_brightness(leds, 1, SQUARED_LIGHT(State.g, State.scale));
		led_set_brightness(leds, 2, SQUARED_LIGHT(State.b, State.scale));
		led_set_brightness(leds, 3, SQUARED_LIGHT(State.cw, State.scale));
		led_set_brightness(leds, 4, SQUARED_LIGHT(State.ww, State.scale));
	} else {
		led_set_brightness(leds, 0, 0);
		led_set_brightness(leds, 1, 0);
		led_set_brightness(leds, 2, 0);
		led_set_brightness(leds, 3, 0);
		led_set_brightness(leds, 4, 0);
	}
}

/* Extract white part to enable brighter compound colors */
void rgb_white()
{
	float white = State.r;

	if (State.g < white) {
		white = State.g;
	}
	if (State.b < white) {
		white = State.b;
	}

	if (white > 50.0f) {
		State.r -= white;
		State.g -= white;
		State.b -= white;
		State.cw = WHITE_GAIN(white) * 0.5f;
		State.ww = WHITE_GAIN(white) * 0.5f;
	} else {
		State.cw = 0.0f;
		State.ww = 0.0f;
	}
}

/* Scale so the strongest of r, g, b is 100; brightness comes from the level. */
void rgb_normalize()
{
	float max = State.r;

	if (State.g > max) {
		max = State.g;
	}
	if (State.b > max) {
		max = State.b;
	}
	if (max > 0.0f) {
		State.r = State.r * 100.0f / max;
		State.g = State.g * 100.0f / max;
		State.b = State.b * 100.0f / max;
	}
}

} /* namespace */

namespace LightDriver {

int Init()
{
	if (!device_is_ready(leds)) {
		return -ENODEV;
	}

	led_set_brightness(leds, 0, 0);
	led_set_brightness(leds, 1, 0);
	led_set_brightness(leds, 2, 0);
	led_set_brightness(leds, 3, 0);
	led_set_brightness(leds, 4, 0);

	return 0;
}

void SetOn(bool on)
{
	State.on = on;
	apply();
}

void SetAll(uint8_t scale, uint8_t r, uint8_t g, uint8_t b, uint8_t cw, uint8_t ww)
{
	State.scale = std::clamp((float)scale, 0.0f, 100.0f);
	if (scale > 0) {
		State.on = true;
	} else {
		State.on = false;
	}
	State.r = std::clamp((float)r, 0.0f, 100.0f);
	State.g = std::clamp((float)g, 0.0f, 100.0f);
	State.b = std::clamp((float)b, 0.0f, 100.0f);
	State.cw = std::clamp((float)cw, 0.0f, 100.0f);
	State.ww = std::clamp((float)ww, 0.0f, 100.0f);
	apply();
}

void SetLevel(uint8_t level)
{
	LOG_DBG("light: level %u", level);
	State.scale = (float)level * 100.0f / 254.0f;
	apply();
}

void SetColorXY(uint16_t x_in, uint16_t y_in)
{
	float x = (float)x_in / 65536.0f;
	float y = (float)y_in / 65536.0f;
	float X, Z;

	LOG_DBG("light: colour xy %u/%u", x_in, y_in);

	if (y < 0.0001f) {
		return;
	}

	/* CIE xy to XYZ with Y = 1. */
	X = x / y;
	Z = (1.0f - x - y) / y;

	/* XYZ to linear sRGB (D65). */
	State.r = std::max(324.04542f * X - 153.71385f - 49.85314f * Z, 0.0f);
	State.g = std::max(-96.92660f * X + 187.60108f + 4.15560f * Z, 0.0f);
	State.b = std::max(5.56434f * X - 20.40259f + 105.72252f * Z, 0.0f);

	rgb_normalize();
	rgb_white();
	apply();
}

void SetColorHS(uint8_t hue, uint8_t sat)
{
	float h = (float)hue * 6.0f / 254.0f; /* 0..6, one unit per 60 degrees */
	float s = (float)sat / 2.54f;
	int sector = (int)h;
	float f = h - (float)sector;
	float p = 100.0f - s;
	float q = 100.0f - s * f;
	float t = 100.0f - s * (1.0f - f);

	LOG_DBG("light: colour hue %u sat %u", hue, sat);

	switch (sector % 6) {
	case 0: State.r = 100.0f; State.g = t; State.b = p; break;
	case 1: State.r = q; State.g = 100.0f; State.b = p; break;
	case 2: State.r = p; State.g = 100.0f; State.b = t; break;
	case 3: State.r = p; State.g = q; State.b = 100.0f; break;
	case 4: State.r = t; State.g = p; State.b = 100.0f; break;
	default: State.r = 100.0f; State.g = p; State.b = q; break;
	}
	rgb_white();
	apply();
}

void SetColorTemperature(uint16_t mireds)
{
	float warm = std::clamp(((float)mireds - COLD_WHITE_MIREDS) /
			     (WARM_WHITE_MIREDS - COLD_WHITE_MIREDS), 0.0f, 1.0f);

	LOG_DBG("light: colour temperature %u mireds", mireds);

	State.ww = warm * 100.0f;
	State.cw = 100.0f - State.ww;
	State.r = 0;
	State.g = 0;
	State.b = 0;
	apply();
}

} /* namespace LightDriver */
