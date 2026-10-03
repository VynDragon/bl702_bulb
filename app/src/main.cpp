/*
 * Copyright (c) 2022-2026 Project CHIP Authors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adapted from examples/platform/telink/common/src/mainCommon.cpp and
 * AppTaskCommon.cpp. Removed: buttons, LEDs, PWM manager, factory data, OTA,
 * identify effects, power-on factory reset, the application thread.
 *
 */

#include "LightDriver.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/reboot.h>

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <app/server/Server.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <data-model-providers/codegen/Instance.h>
#include <lib/support/CHIPMem.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/OpenThread/GenericNetworkCommissioningThreadDriver.h>
#include <setup_payload/OnboardingCodesUtil.h>

#ifdef CONFIG_WATCHDOG
#define WDT_TIMEOUT_MS     10000
#define WDT_FEED_PERIOD_MS 1000
static const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
#endif

#define STEP_MS		72
#define STEP_CNT	24
#define BR_SCALE(v)	(v / 4U)

LOG_MODULE_REGISTER(app, CONFIG_CHIP_APP_LOG_LEVEL);

/* Commission indicating */
static bool attached = false;
static bool commissioned = false;
static bool started = false;
static size_t step = 0;
const uint8_t pwm_curve[STEP_CNT] = {
	BR_SCALE(0), BR_SCALE(17), BR_SCALE(32), BR_SCALE(45), BR_SCALE(57),  BR_SCALE(68), BR_SCALE(77), BR_SCALE(85), BR_SCALE(91), BR_SCALE(95), BR_SCALE(98), BR_SCALE(100),
	BR_SCALE(100), BR_SCALE(98), BR_SCALE(95), BR_SCALE(91), BR_SCALE(85), BR_SCALE(77), BR_SCALE(68), BR_SCALE(57), BR_SCALE(45), BR_SCALE(32), BR_SCALE(17), BR_SCALE(0),
};

/* Factory reset counter */
#define BOOT_COUNT_KEY		"bulb/boots"
#define BOOT_COUNT_RESET	3
#define BOOT_COUNT_CLEAR_DELAY	K_SECONDS(5)

static struct k_work_delayable boot_count_work;

static void store_boot(uint8_t count)
{
	int rc = settings_save_one(BOOT_COUNT_KEY, &count, sizeof(count));

	if (rc != 0) {
		LOG_ERR("boot count save failed: %d", rc);
	}
}

static void boot_count_clear(struct k_work *work)
{
	ARG_UNUSED(work);
	store_boot(0);
}

static bool check_boot_count()
{
	uint8_t count = 0;

	/* Missing key or read error: start from 0. */
	if (settings_load_one(BOOT_COUNT_KEY, &count, sizeof(count)) < 0) {
		count = 0;
	}

	count++;
	if (count >= BOOT_COUNT_RESET) {
		store_boot(0);
		return true;
	}

	store_boot(count);
	k_work_init_delayable(&boot_count_work, boot_count_clear);
	k_work_schedule(&boot_count_work, BOOT_COUNT_CLEAR_DELAY);

	return false;
}

using namespace chip;
using namespace chip::app;
using namespace chip::DeviceLayer;

namespace {

/* Network Commissioning cluster on endpoint 0, backed by OpenThread. */
Clusters::NetworkCommissioning::InstanceAndDriver<NetworkCommissioning::GenericThreadDriver>
	sThreadNetworkDriver(0 /* endpointId */);

void ChipEventHandler(const ChipDeviceEvent *event, intptr_t)
{
	switch (event->Type) {
	case DeviceEventType::kCHIPoBLEAdvertisingChange:
		LOG_INF("BLE advertising %s, %u connection(s)",
			ConnectivityMgr().IsBLEAdvertisingEnabled() ? "on" : "off",
			ConnectivityMgr().NumBLEConnections());
		break;
	case DeviceEventType::kThreadStateChange:
		LOG_INF("Thread: provisioned %d, attached %d",
			ConnectivityMgr().IsThreadProvisioned(),
			ConnectivityMgr().IsThreadAttached());
			if (ConnectivityMgr().IsThreadAttached() == true) {
				attached = true;
			}
		break;
	case DeviceEventType::kCommissioningComplete:
		commissioned = true;
		LOG_INF("Commissioning complete");
		break;
	case DeviceEventType::kFailSafeTimerExpired:
		LOG_ERR("Commissioning fail-safe expired");
		break;
	case DeviceEventType::kCHIPoBLEConnectionClosed:
#if !CHIP_DEVICE_CONFIG_SUPPORTS_CONCURRENT_CONNECTION
		/*
		 * Non-concurrent commissioning: Matter shuts the BLE layer down
		 * after answering ConnectNetwork. Once the link is really closed,
		 * tell the network commissioning cluster it may start Thread.
		 */
		if (!ConnectivityMgr().GetBleLayer()->IsInitialized()) {
			ChipDeviceEvent opEvent;
			int rc = bt_disable();

			if (rc != 0) {
				LOG_ERR("bt_disable failed: %d", rc);
			}

			opEvent.Type = DeviceEventType::kOperationalNetworkStarted;
			LOG_INF("BLE closed, starting Thread");
			if (PlatformMgr().PostEvent(&opEvent) != CHIP_NO_ERROR) {
				LOG_ERR("Could not post network-started event");
			}
		}
#endif
		break;
	default:
		break;
	}
}

void RestoreLightState()
{
	using Protocols::InteractionModel::Status;

	bool on = false;
	DataModel::Nullable<uint8_t> level;
	Clusters::ColorControl::ColorModeEnum mode;

	if (Clusters::LevelControl::Attributes::CurrentLevel::Get(LIGHT_ENDPOINT_ID, level) ==
	    Status::Success && !level.IsNull()) {
		LightDriver::SetLevel(level.Value());
	}

	if (Clusters::OnOff::Attributes::OnOff::Get(LIGHT_ENDPOINT_ID, &on) == Status::Success) {
		LightDriver::SetOn(on);
	}

	if (Clusters::ColorControl::Attributes::ColorMode::Get(LIGHT_ENDPOINT_ID, &mode)
	    != Status::Success) {
		return;
	}

	switch (mode) {
	case Clusters::ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation: {
		uint8_t hue = 0, sat = 0;

		Clusters::ColorControl::Attributes::CurrentHue::Get(LIGHT_ENDPOINT_ID, &hue);
		Clusters::ColorControl::Attributes::CurrentSaturation::Get(LIGHT_ENDPOINT_ID, &sat);
		LightDriver::SetColorHS(hue, sat);
		break;
	}
	case Clusters::ColorControl::ColorModeEnum::kCurrentXAndCurrentY: {
		uint16_t x = 0, y = 0;

		Clusters::ColorControl::Attributes::CurrentX::Get(LIGHT_ENDPOINT_ID, &x);
		Clusters::ColorControl::Attributes::CurrentY::Get(LIGHT_ENDPOINT_ID, &y);
		LightDriver::SetColorXY(x, y);
		break;
	}
	case Clusters::ColorControl::ColorModeEnum::kColorTemperatureMireds: {
		uint16_t mireds = 0;

		Clusters::ColorControl::Attributes::ColorTemperatureMireds::Get(LIGHT_ENDPOINT_ID, &mireds);
		LightDriver::SetColorTemperature(mireds);
		break;
	}
	default:
		break;
	}
}

/*
 * Runs on the Matter thread, so the heavy part of start-up uses the Matter
 * stack (not main's) and the Matter stack lock is already held.
 */
void InitServer(intptr_t)
{
	static CommonCaseDeviceServerInitParams initParams;

	CHIP_ERROR err = initParams.InitializeStaticResourcesBeforeServerInit();
	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Server params init failed: %" CHIP_ERROR_FORMAT, err.Format());
		return;
	}

	initParams.dataModelProvider =
		CodegenDataModelProviderInstance(initParams.persistentStorageDelegate);

	err = Server::GetInstance().Init(initParams);
	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Server init failed: %" CHIP_ERROR_FORMAT, err.Format());
		return;
	}

	if (Server::GetInstance().GetFabricTable().FabricCount() > 0) {
		commissioned = true;
	}

	/* QR code payload and manual pairing code, on the UART. */
	ConfigurationMgr().LogDeviceConfig();
	PrintOnboardingCodes(RendezvousInformationFlags(RendezvousInformationFlag::kBLE));

	RestoreLightState();

	started = true;

	if (check_boot_count()) {
		LOG_INF("Power-cycle pattern detected, factory reset");
		chip::Server::GetInstance().ScheduleFactoryReset();
	}

	LOG_INF("Matter server started");
}

/* Use the chip's hardware ID as the Matter serial number, saved once. */
int SetSerialFromHardwareId()
{
	uint8_t id[8];
	char serial[2 * sizeof(id) + 1];
	char current[ConfigurationManager::kMaxSerialNumberLength + 1] = {};

	ssize_t len = hwinfo_get_device_id(id, sizeof(id));
	if (len <= 0) {
		LOG_WRN("No hardware ID, keeping default serial number");
		return -ENOTSUP;
	}

	bin2hex(id, len, serial, sizeof(serial));

	if (GetDeviceInstanceInfoProvider()->GetSerialNumber(current, sizeof(current)) == CHIP_NO_ERROR &&
	    strcmp(current, serial) == 0) {
		return 0;
	}

	if (ConfigurationMgr().StoreSerialNumber(serial, strlen(serial)) != CHIP_NO_ERROR) {
		LOG_ERR("Could not store serial number");
		return -EIO;
	}

	return 0;
}

CHIP_ERROR InitMatter()
{
	ReturnErrorOnFailure(Platform::MemoryInit());
	ReturnErrorOnFailure(PlatformMgr().InitChipStack());

	(void)SetSerialFromHardwareId();

	/*
	 * Bring Bluetooth up before OpenThread starts. In non-concurrent mode
	 * Matter defers bt_enable() until advertising begins, which is after Thread has started
	 * when a dataset is already stored.
	 */
	if (!bt_is_ready()) {
		int rc = bt_enable(nullptr);

		if (rc != 0) {
			LOG_ERR("bt_enable failed: %d", rc);
			return CHIP_ERROR_INTERNAL;
		}
	}

	ReturnErrorOnFailure(ThreadStackMgr().InitThreadStack());
	/* Always-on end device, matching CONFIG_OPENTHREAD_MTD=y. */
	ReturnErrorOnFailure(ConnectivityMgr().SetThreadDeviceType(
		ConnectivityManager::kThreadDeviceType_MinimalEndDevice));
	ReturnErrorOnFailure(sThreadNetworkDriver.Init());

	/* Test attestation credentials, valid for vendor ID 0xFFF1 only. */
	Credentials::SetDeviceAttestationCredentialsProvider(
		Credentials::Examples::GetExampleDACProvider());

	ReturnErrorOnFailure(PlatformMgr().AddEventHandler(ChipEventHandler, 0));

	/* Creates the Matter thread; from here the event queue is drained. */
	ReturnErrorOnFailure(PlatformMgr().StartEventLoopTask());

	return PlatformMgr().ScheduleWork(InitServer, 0);
}

} /* namespace */

#ifdef CONFIG_WATCHDOG

static void wdt_feed_thread(void *, void *, void *)
{
	int wdt_channel_id;
	int ret;
	struct wdt_timeout_cfg wdt_config = {
		.window = { .min = 0, .max = WDT_TIMEOUT_MS },
		.callback = nullptr,
		.flags = WDT_FLAG_RESET_SOC,
	};

	if (!device_is_ready(wdt)) {
		/* Reboot currently hangs the CPU but if wdt is installed, the result is the same */
		sys_reboot(0);
	}

	wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
	if (wdt_channel_id < 0) {
		sys_reboot(0);
	}

	ret = wdt_setup(wdt, 0);
	if (ret < 0) {
		sys_reboot(0);
	}

	while (true) {
		wdt_feed(wdt, wdt_channel_id);
		k_msleep(WDT_FEED_PERIOD_MS);
	}
}

K_THREAD_DEFINE(wdt_feed_tid, 512, wdt_feed_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

#endif

int main(void)
{
	if (LightDriver::Init() < 0) {
		LOG_ERR("Failed to initialize LEDs");
		sys_reboot(0);
	}

	CHIP_ERROR err = InitMatter();

	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Matter init failed: %" CHIP_ERROR_FORMAT, err.Format());
		return -1;
	}

	while (!commissioned) {
		/* Breath animation when pairing */
		if (started && !commissioned) {
			if (!attached) {
				LightDriver::SetAll(100, 0, pwm_curve[step], 0, 0, 0);
			} else {
				LightDriver::SetAll(100, pwm_curve[step], 0, 0, 0, 0);
			}
			step++;
			if (step >= STEP_CNT) {
				step = 0;
			}
		}
		k_msleep(STEP_MS);
	}

	return 0;
}
