/*
 * Copyright (c) 2000-2024 Your Name. All rights reserved.
 *
 * Rockchip RK3588 common hardware abstraction layer for Synology synobios.
 * This file implements platform-agnostic features for RK3588 boards.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/synolib.h>
#include <linux/thermal.h>
#include <linux/pwm.h>
#include <linux/rtc.h>
#include <linux/cpufreq.h> // For cpufreq_quick_get

#include "synobios.h"
#include "rk3588_common.h"
#include "../led/led_trigger_disk.h" // For LED trigger functions
#include <linux/bcd.h>
/*
 * Forward declarations for functions defined in other files (e.g., rk3588_ds423.c)
 * This is crucial to resolve linking errors.
 */
int model_addon_init(struct synobios_ops *ops);
int model_addon_cleanup(struct synobios_ops *ops);
int InitModuleType(struct synobios_ops *ops);
int GetModel(void);
const char *syno_get_hw_version(void); // Assuming this exists for logging

/*
 * Forward declaration for the function in led_trigger_disk.c.
 * We need this because we are assigning its address to a function pointer.
 */
extern int SetDiskLedStatusByTrigDiskSyno(DISKLEDSTATUS *pLedStatus);

/*
 * Global variables for this module
 */
static struct pwm_device *fan_pwm_dev = NULL; // Pointer to our fan's PWM device
static struct rtc_device *hym8563_rtc_dev = NULL; // Pointer to our RTC device

/*
 * PWM Fan Speed Mapping Table.
 * Translates Synology's abstract fan speed levels to concrete PWM duty cycles (in percent).
 */
static const PWM_FAN_SPEED_MAPPING gPWMSpeedMapping[] = {
	{ .fanSpeed = FAN_SPEED_STOP,       .iDutyCycle = 0  },
	{ .fanSpeed = FAN_SPEED_ULTRA_LOW,  .iDutyCycle = 20 },
	{ .fanSpeed = FAN_SPEED_VERY_LOW,   .iDutyCycle = 30 },
	{ .fanSpeed = FAN_SPEED_LOW,        .iDutyCycle = 40 },
	{ .fanSpeed = FAN_SPEED_MIDDLE,     .iDutyCycle = 50 },
	{ .fanSpeed = FAN_SPEED_HIGH,       .iDutyCycle = 65 },
	{ .fanSpeed = FAN_SPEED_VERY_HIGH,  .iDutyCycle = 80 },
	{ .fanSpeed = FAN_SPEED_ULTRA_HIGH, .iDutyCycle = 99 },
	{ .fanSpeed = FAN_SPEED_FULL,       .iDutyCycle = 99 },
};

/************************************************************************
 * RTC and Timed Power On functions
 ************************************************************************/

/**
 * @brief Converts Synology's simplified alarm structure to Linux's full rtc_time structure.
 *
 * Since the Synology alarm does not contain year, month, or day, we have to
 * make some intelligent guesses. We'll set the alarm for the *next*
 * possible occurrence of the specified hour and minute.
 */
static void syno_alarm_to_rtc_time(struct rtc_time *tm, const SYNORTCALARMPKT *syno_alarm)
{
    struct timespec64 ts; // 定义一个 timespec64 变量
	struct rtc_time now;

    ktime_get_real_ts64(&ts);
    rtc_time64_to_tm(ts.tv_sec, &now); // <-- 将当前时间读入 'now' 变量


	// Copy the parts we know
	tm->tm_hour = bcd2bin(syno_alarm->hour);
	tm->tm_min  = bcd2bin(syno_alarm->min);
	tm->tm_sec  = 0; // Synology alarm has no seconds

	// Now, figure out the date. Assume the alarm is for today.
	tm->tm_mday = now.tm_mday;
	tm->tm_mon  = now.tm_mon;
	tm->tm_year = now.tm_year;

	// If the calculated alarm time is in the past (e.g., it's 3 PM now, and alarm is for 10 AM),
	// we set the alarm for tomorrow.
	if ( (tm->tm_hour < now.tm_hour) ||
		 (tm->tm_hour == now.tm_hour && tm->tm_min <= now.tm_min) ) {

		// This is a simplified way to advance one day.
		// A more robust implementation would handle month/year rollovers.
		// For now, this is sufficient.
		tm->tm_mday++;
		// TODO: Add full logic for rolling over month and year if tm_mday exceeds days in month.
	}

	// Note: We are ignoring the 'weekdays' field for now, as mapping it to a
	// specific date is complex. This implementation sets a one-time alarm.
}

/**
 * @brief Converts Linux's full rtc_time structure to Synology's simplified alarm structure.
 */
static void rtc_time_to_syno_alarm(SYNORTCALARMPKT *syno_alarm, const struct rtc_time *tm)
{
    syno_alarm->hour = bin2bcd(tm->tm_hour);
    syno_alarm->min  = bin2bcd(tm->tm_min);

	// Convert tm_wday (0=Sun, 1=Mon...) to Synology's bitmask format
	if (tm->tm_wday >= 0 && tm->tm_wday < 7) {
		syno_alarm->weekdays = (1 << tm->tm_wday);
	} else {
		syno_alarm->weekdays = 0;
	}
}

static int __maybe_unused RK3588_GetAutoPowerOn(SYNO_AUTO_POWERON *autoPowerOn)
{
	struct rtc_wkalrm alarm;
	int ret;

	if (!hym8563_rtc_dev) {
		return -ENODEV;
	}

	ret = rtc_read_alarm(hym8563_rtc_dev, &alarm);
	if (ret < 0) {
		pr_err("synobios_rk3588: failed to read RTC alarm, err=%d\n", ret);
		return ret;
	}

	autoPowerOn->enabled = alarm.enabled;
	// Convert from rtc_time to SYNORTCALARMPKT
	rtc_time_to_syno_alarm(&autoPowerOn->RtcAlarmPkt[0], &alarm.time);
	autoPowerOn->num = 1;

	return 0;
}

static int __maybe_unused RK3588_SetAutoPowerOn(SYNO_AUTO_POWERON *autoPowerOn)
{
	struct rtc_wkalrm alarm;
	int ret;

	if (!hym8563_rtc_dev) {
		return -ENODEV;
	}

	memset(&alarm, 0, sizeof(alarm));

	alarm.enabled = autoPowerOn->enabled;
	if (alarm.enabled && autoPowerOn->num > 0) {
		// Convert from SYNORTCALARMPKT to rtc_time
		syno_alarm_to_rtc_time(&alarm.time, &autoPowerOn->RtcAlarmPkt[0]);
	}

	ret = rtc_set_alarm(hym8563_rtc_dev, &alarm);
	if (ret < 0) {
		pr_err("synobios_rk3588: failed to set RTC alarm, err=%d\n", ret);
		return ret;
	}

	return 0;
}

/************************************************************************
 * Temperature and Fan Control functions
 ************************************************************************/

int GetCPUTemperature(struct _SynoCpuTemp *pCPUTemp)
{
	struct thermal_zone_device *tz;
	int temp, ret = -1;

	if (!pCPUTemp) {
		return -EINVAL;
	}

	tz = thermal_zone_get_zone_by_name("soc_thermal");
	if (IS_ERR(tz)) {
		pr_err("synobios_rk3588: Could not find thermal zone 'soc-thermal'\n");
		return PTR_ERR(tz);
	}

	ret = thermal_zone_get_temp(tz, &temp);
	//thermal_zone_put(tz); // Release the thermal zone reference
	if (ret) {
		pr_err("synobios_rk3588: Failed to read temperature, err=%d\n", ret);
		return ret;
	}

	pCPUTemp->cpu_num = 1;
	pCPUTemp->cpu_temp[0] = temp / 1000;

	return 0;
}

static int PWMFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(gPWMSpeedMapping); ++i) {
		if (gPWMSpeedMapping[i].fanSpeed == speed) {
			iDutyCycle = gPWMSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

int SetFanStatusByPWM(FAN_STATUS status, FAN_SPEED speed)
{
	int duty_cycle_percent;
	unsigned int period_ns;
	unsigned int duty_ns;

	if (!fan_pwm_dev || IS_ERR(fan_pwm_dev)) {
		return -ENODEV;
	}

	if (status == FAN_STATUS_STOP || speed == FAN_SPEED_STOP) {
		pwm_disable(fan_pwm_dev);
		return 0;
	}

	duty_cycle_percent = PWMFanSpeedMapping(speed);
	if (duty_cycle_percent < 0) {
		return -EINVAL;
	}

	period_ns = pwm_get_period(fan_pwm_dev);
	duty_ns = (period_ns * duty_cycle_percent) / 100;

	pwm_config(fan_pwm_dev, duty_ns, period_ns);
	pwm_enable(fan_pwm_dev);

	return 0;
}

int GetFanStatusByPWM(int fanno, FAN_STATUS *pStatus)
{
    if (!pStatus) {
        return -EINVAL;
    }
    // We can't easily get the real status from PWM, so we report RUNNING
    // if the PWM is enabled, and STOP if disabled.
    if (!fan_pwm_dev || IS_ERR(fan_pwm_dev) || !pwm_is_enabled(fan_pwm_dev)) {
        *pStatus = FAN_STATUS_STOP;
    } else {
        *pStatus = FAN_STATUS_RUNNING;
    }
    return 0;
}

/************************************************************************
 * System Information functions
 ************************************************************************/

void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength)
{
	if (!cpu) {
		return;
	}
	cpu->core = num_online_cpus();
	// cpufreq_quick_get returns frequency in KHz
	snprintf(cpu->clock, maxLength, "%d", cpufreq_quick_get(0) / 1000);
}

/************************************************************************
 * Main ops structure and Init/Cleanup functions
 ************************************************************************/

static struct synobios_ops synobios_ops = {
	.owner                = THIS_MODULE,
	.get_brand            = GetBrand,
	.get_model            = GetModel,
	.get_fan_status       = GetFanStatusByPWM,
	.set_fan_status       = SetFanStatusByPWM,
	.get_cpu_temperature  = GetCPUTemperature,
	.module_type_init     = InitModuleType,
	.get_cpu_info         = GetCPUInfo,
    .set_disk_led         = SetDiskLedStatusByTrigDiskSyno,

    // Functions to be implemented or using stubs
	.get_rtc_time         = NULL, // TODO: Implement using rtc_read_time
	.set_rtc_time         = NULL, // TODO: Implement using rtc_set_time
	.get_auto_poweron     = RK3588_GetAutoPowerOn,
	.set_auto_poweron     = RK3588_SetAutoPowerOn,
	.get_gpio_pin         = NULL, // TODO: Implement generic GPIO read
	.set_gpio_pin         = NULL, // TODO: Implement generic GPIO write

    // Functions intentionally left NULL as they are implemented in model-specific files
	.set_power_led        = NULL,

    // Functions intentionally left NULL as they are not supported on this platform
    .get_sys_temperature  = NULL,
    .hwmon_get_backplane_status = NULL,
};

int GetBrand(void)
{
    return BRAND_SYNOLOGY;
}
EXPORT_SYMBOL(GetBrand);

int SetDiskLedStatusByTrigDiskSyno(DISKLEDSTATUS *pLedStatus)
{
    // This is a stub function because the original source is not available.
    // We do nothing but return success.
    pr_warn("synobios_rk3588: STUB: SetDiskLedStatusByTrigDiskSyno called but not implemented.\n");
    return 0;
}
EXPORT_SYMBOL(SetDiskLedStatusByTrigDiskSyno);

int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	// Get a reference to the PWM fan device described in the DTS
    fan_pwm_dev = pwm_get(NULL, "pwm-fan");
    if (IS_ERR(fan_pwm_dev)) {
        pr_err("synobios_rk3588: could not get fan pwm. Fan control will be disabled.\n");
        fan_pwm_dev = NULL; // Ensure it's NULL on failure
    } else {
        // Set a safe initial speed
        SetFanStatusByPWM(FAN_STATUS_RUNNING, FAN_SPEED_MIDDLE);
    }

	// Get a reference to the HYM8563 RTC device
	hym8563_rtc_dev = rtc_class_open("rtc-hym8563");
	if (IS_ERR(hym8563_rtc_dev)) {
		pr_err("synobios_rk3588: failed to open rtc-hym8563. Timed power on will be disabled.\n");
		hym8563_rtc_dev = NULL; // Ensure it's NULL on failure
	}

	// Initialize GPIOs defined by the model
	syno_gpio_init();
	pr_info("Synobios RK3588 GPIO initialized\n");

	// Initialize the module type descriptor
	if (synobios_ops.module_type_init) {
		synobios_ops.module_type_init(&synobios_ops);
	}

	// Pass our ops structure back to the main synobios driver
	*ops = &synobios_ops;

	// Call the model-specific addon initializer (e.g., rk3588_ds423.c)
	model_addon_init(*ops);

	return 0;
}

int synobios_model_cleanup(struct file_operations *fops, struct synobios_ops **ops)
{
	// Release the PWM device
	if (fan_pwm_dev) {
        pwm_disable(fan_pwm_dev);
        pwm_put(fan_pwm_dev);
    }

	// Release the RTC device
	if (hym8563_rtc_dev) {
		rtc_class_close(hym8563_rtc_dev);
	}

	// Call the model-specific cleanup
	model_addon_cleanup(*ops);

	// Cleanup GPIOs
	syno_gpio_cleanup();

	return 0;
}