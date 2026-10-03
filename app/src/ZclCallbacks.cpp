/*
 * Copyright (c) 2021-2026 Project CHIP Authors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adapted from examples/lighting-app/telink/src/ZclCallbacks.cpp.
 * The Telink version posts to an application thread; this one calls the
 * light driver directly, which saves that thread's stack and queue.
 *
 * LLM-generated
 */

#include "LightDriver.h"

#include <string.h>

#include <zephyr/logging/log.h>

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace chip;
using namespace chip::app::Clusters;

static uint16_t ReadU16(const uint8_t *value)
{
	uint16_t v;

	memcpy(&v, value, sizeof(v));
	return v;
}

/* Called on the Matter thread after any attribute is written. */
void MatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath &attributePath,
				       uint8_t type, uint16_t size, uint8_t *value)
{
	static uint16_t sX, sY;
	static uint8_t sHue, sSat;

	const ClusterId clusterId = attributePath.mClusterId;
	const AttributeId attributeId = attributePath.mAttributeId;

	if (attributePath.mEndpointId != LIGHT_ENDPOINT_ID) {
		return;
	}

	if (clusterId == OnOff::Id && attributeId == OnOff::Attributes::OnOff::Id) {
		LightDriver::SetOn(*value != 0);
	} else if (clusterId == LevelControl::Id &&
		   attributeId == LevelControl::Attributes::CurrentLevel::Id) {
		LightDriver::SetLevel(*value);
	} else if (clusterId == ColorControl::Id) {
		switch (attributeId) {
		case ColorControl::Attributes::CurrentX::Id:
			sX = ReadU16(value);
			LightDriver::SetColorXY(sX, sY);
			break;
		case ColorControl::Attributes::CurrentY::Id:
			sY = ReadU16(value);
			LightDriver::SetColorXY(sX, sY);
			break;
		case ColorControl::Attributes::CurrentHue::Id:
			sHue = *value;
			LightDriver::SetColorHS(sHue, sSat);
			break;
		case ColorControl::Attributes::CurrentSaturation::Id:
			sSat = *value;
			LightDriver::SetColorHS(sHue, sSat);
			break;
		case ColorControl::Attributes::ColorTemperatureMireds::Id:
			LightDriver::SetColorTemperature(ReadU16(value));
			break;
		default:
			break;
		}
	}
}
