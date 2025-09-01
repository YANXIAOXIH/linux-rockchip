#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* Copyright (c) 2000-2016 Synology Inc. All rights reserved. */

#include "synobios.h"
#include <asm/delay.h>
#include "rk3588_common.h"
#include <linux/thermal.h> 
#include <linux/pwm.h>
#include <linux/synolib.h> 
#include "../led/led_trigger_disk.h"
#include <linux/rtc.h> 

#if defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
extern int (*syno_get_current)(unsigned char, struct tty_struct *);
extern int save_current_data_from_uart(unsigned char ch, struct tty_struct *tty);
extern int synobios_lock_ttyS_current(char *szCommand, char *szBuf);
extern int SetDiskLedStatusByTrigDiskSyno(DISKLEDSTATUS *pLedStatus);
#endif /* CONFIG_SYNO_TTY_MICROP_FUNCTIONS */

int model_addon_init(struct synobios_ops *ops);
int model_addon_cleanup(struct synobios_ops *ops);
int syno_rtd_get_temperature(void);

static PWM_FAN_SPEED_MAPPING gPWMSpeedMapping[] = {
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
static int Uninitialize(void);

static SYNO_HWMON_SENSOR_TYPE rk3588_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor = {{
		.sensor_name = HWMON_HDD_BP_DETECT,
	}, {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	}}
};

static SYNO_HWMON_SENSOR_TYPE rk3588_current_status = {
	.type_name = HWMON_SYS_CURRENT_NAME,
	.sensor_num = 1,
	.sensor[0] = {
		.sensor_name = "ADC",
	},
};

static struct hwmon_sensor_list rk3588_sensor_list = {
	.thermal_sensor = NULL,
	.voltage_sensor = NULL,
	.fan_speed_rpm = NULL,
	.psu_status = NULL,
	.hdd_backplane = &rk3588_hdd_backplane_status,
	.current_sensor = &rk3588_current_status,
};
static struct hwmon_sensor_list *hwmon_sensor_list = &rk3588_sensor_list;

static struct rtc_device *hym8563_rtc_dev = NULL;

/**
 * @brief 将 Synology 的 RTC 时间格式转换为 Linux 内核的 rtc_time 格式
 */
static void syno_to_rtc_time(struct rtc_time *tm, const SYNORTCTIMEPKT *syno_time)
{
    tm->tm_year = syno_time->year;
    tm->tm_mon  = syno_time->month;
    tm->tm_mday = syno_time->day;
    tm->tm_hour = syno_time->hour;
    tm->tm_min  = syno_time->min;
    tm->tm_sec  = syno_time->sec;
}

/**
 * @brief 将 Linux 内核的 rtc_time 格式转换为 Synology 的 RTC 时间格式
 */
static void rtc_to_syno_time(SYNORTCTIMEPKT *syno_time, const struct rtc_time *tm)
{
    syno_time->year  = tm->tm_year;
    syno_time->month = tm->tm_mon;
    syno_time->day   = tm->tm_mday;
    syno_time->hour  = tm->tm_hour;
    syno_time->min   = tm->tm_min;
    syno_time->sec   = tm->tm_sec;
}

/**
 * @brief 实现 synobios 的 get_auto_poweron 接口
 */
static int RK3588_GetAutoPowerOn(SYNO_AUTO_POWERON *autoPowerOn)
{
    struct rtc_wkalrm alarm;
    int ret;

    if (!hym8563_rtc_dev) {
        return -ENODEV;
    }

    // 调用标准的 RTC alarm 读取函数
    ret = rtc_read_alarm(hym8563_rtc_dev, &alarm);
    if (ret < 0) {
        pr_err("synobios_rk3588: failed to read RTC alarm, err=%d\n", ret);
        return ret;
    }

    // 转换数据结构
    autoPowerOn->enabled = alarm.enabled;
    rtc_to_syno_time(&autoPowerOn->RtcAlarmPkt[0].time, &alarm.time);

    // synobios 似乎只支持一个闹钟任务
    autoPowerOn->num = 1;

    return 0;
}

/**
 * @brief 实现 synobios 的 set_auto_poweron 接口
 */
static int RK3588_SetAutoPowerOn(SYNO_AUTO_POWERON *autoPowerOn)
{
    struct rtc_wkalrm alarm;
    int ret;

    if (!hym8563_rtc_dev) {
        return -ENODEV;
    }

    // 清空 alarm 结构体
    memset(&alarm, 0, sizeof(alarm));

    // 转换数据结构
    alarm.enabled = autoPowerOn->enabled;
    if (alarm.enabled && autoPowerOn->num > 0) {
        syno_to_rtc_time(&alarm.time, &autoPowerOn->RtcAlarmPkt[0].time);
    }

    // 调用标准的 RTC alarm 设置函数
    ret = rtc_set_alarm(hym8563_rtc_dev, &alarm);
    if (ret < 0) {
        pr_err("synobios_rk3588: failed to set RTC alarm, err=%d\n", ret);
        return ret;
    }

    return 0;
}

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
int (*GetMaxInternalHostNum)(void) = NULL;

int GetMaxInternalDiskNum(void)
{
	int iMaxInternalDiskNum = 0;

	switch(GetModel()) {
		case MODEL_DS124:
			iMaxInternalDiskNum = 1;
			break;
		case MODEL_DS223j:
		case MODEL_DS223:
			iMaxInternalDiskNum = 2;
			break;
		case MODEL_DS423:
			iMaxInternalDiskNum = 4;
			break;
		case MODEL_RK3588_DS423:
			iMaxInternalDiskNum = 4;
			break;
		default:
			iMaxInternalDiskNum = 0;
			break;
	}
	return iMaxInternalDiskNum;
}
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

int GetCPUTemperature(struct _SynoCpuTemp *pCPUTemp)
{
	struct thermal_zone_device *tz;
	int temp, ret = -1;

	if (!pCPUTemp) {
		return -EINVAL;
	}

	tz = thermal_zone_get_zone_by_name("cpu-thermal");
	if (IS_ERR(tz)) {
		pr_err("synobios_rk3588: Could not find thermal zone 'cpu-thermal'\n");
		return PTR_ERR(tz);
	}

	ret = thermal_zone_get_temp(tz, &temp);
	thermal_zone_put(tz);
	if (ret) {
		pr_err("synobios_rk3588: Failed to read temperature, err=%d\n", ret);
		return ret;
	}

	pCPUTemp->cpu_num = 1;
	pCPUTemp->cpu_temp[0] = temp / 1000; 

	return 0;
}

static
int HWMONGetHDDBackPlaneStatusByGPIO(struct _SYNO_HWMON_SENSOR_TYPE *hdd_backplane)
{
	int iRet = -1;
	int index = 1;
	unsigned long hdd_detect = 0;
	unsigned long hdd_enable = 0;

	if (NULL == hdd_backplane || NULL == hwmon_sensor_list) {
		printk("hdd_backplane null\n");
		goto End;
	}

	memcpy(hdd_backplane, hwmon_sensor_list->hdd_backplane, sizeof(SYNO_HWMON_SENSOR_TYPE));

	while (HAVE_HDD_DETECT(index)) {
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
		hdd_detect |= ((SYNO_GPIO_READ(HDD_DETECT_PIN(index)) ^ HDD_DETECT_POLARITY(index)) & 0x01) << (index - 1);
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
		hdd_detect |= ((SYNO_GPIO_READ(HDD_DETECT_PIN(index)) ^ HDD_DETECT_POLARITY()) & 0x01) << (index - 1);
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

		index++;
	}

	index = 1;
	while (HAVE_HDD_ENABLE(index)) {
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
		hdd_enable |= ((SYNO_GPIO_READ(HDD_ENABLE_PIN(index)) ^ HDD_ENABLE_POLARITY(index)) & 0x01) << (index - 1);
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
		hdd_enable |= ((SYNO_GPIO_READ(HDD_ENABLE_PIN(index)) ^ HDD_ENABLE_POLARITY()) & 0x01) << (index - 1);
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

		index++;
	}

	snprintf(hdd_backplane->sensor[0].value, sizeof(hdd_backplane->sensor[0].value), "%lu", hdd_detect);
	snprintf(hdd_backplane->sensor[1].value, sizeof(hdd_backplane->sensor[1].value), "%lu", hdd_enable);

	iRet = 0;

End:
	return iRet;
}

int SetHDDActLed(SYNO_LED ledStatus)
{
	int err = -1;
	switch (ledStatus) {
		case SYNO_LED_OFF:
			SYNO_HDD_LED_SET(1, DISK_LED_OFF);
			SYNO_HDD_LED_SET(2, DISK_LED_OFF);
			break;
		case SYNO_LED_ON:
			SYNO_HDD_LED_SET(1, DISK_LED_GREEN_BLINK);
			SYNO_HDD_LED_SET(2, DISK_LED_GREEN_BLINK);
			break;
		default:
			goto ERR;
	}
	err = 0;
ERR:
	return err;
}

int SetPhyLed(SYNO_LED ledStatus)
{
	int iError = -1;

	switch(ledStatus){
		case SYNO_LED_ON:
			SYNO_ENABLE_PHY_LED(1);
			break;
		case SYNO_LED_OFF:
			SYNO_ENABLE_PHY_LED(0);
			break;
		default:
			goto ERR;
	}

	iError = 0;
ERR:
	return iError;
}

extern unsigned int cpufreq_quick_get(unsigned int cpu);
void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength)
{
	if (!cpu) {
		return;
	}

	cpu->core = num_online_cpus(); // 使用内核 API 获取在线核心数

	// cpufreq_quick_get 返回的是 KHz
	snprintf(cpu->clock, maxLength, "%d", cpufreq_quick_get(0) / 1000);
}

static
int SYNOIOGetCurrentStatusByMicroP(unsigned long* pulSysCurrent)
{
	int iRet = -1;
	int len = 0;
	unsigned char szTTYResult[TTY_BUF_SIZE] = {'\0'};
	unsigned char szTTYCur[CURRENT_DATA_LEN+1] = {'\0'};
	unsigned long ulTTYCur = 0;

	len = synobios_lock_ttyS_current(SZ_UART_CMD_CURRENT_GET, szTTYResult);
	if (len < CURRENT_DATA_LEN) {
		goto End;
	}
	memcpy(szTTYCur, szTTYResult + len - CURRENT_DATA_LEN, CURRENT_DATA_LEN);
	iRet = kstrtoul(szTTYCur, 10, &ulTTYCur);
	if (0 > iRet) {
		goto End;
	}
	// microP return value * 16.13 = System current (mA) (reference : uP #130)
	*pulSysCurrent = (ulTTYCur * 16) + (ulTTYCur * 13 / 100);

	iRet = 0;
End:
	return iRet;
}

static
int HWMONGetCurrentStatusByMicroP(struct _SYNO_HWMON_SENSOR_TYPE *SysCurrent)
{
	int iRet = -1;
	int len = 0;
	unsigned char szTTYResult[TTY_BUF_SIZE] = {'\0'};
	unsigned char szCurrent[CURRENT_DATA_LEN+1] = {'\0'};
	unsigned long ulCurrent = 0;

	if (NULL == SysCurrent || NULL == hwmon_sensor_list) {
		printk("SysCurrent null\n");
		goto End;
	}

	memcpy(SysCurrent, hwmon_sensor_list->current_sensor, sizeof(SYNO_HWMON_SENSOR_TYPE));

	len = synobios_lock_ttyS_current(SZ_UART_CMD_CURRENT_GET, szTTYResult);
	if (len < CURRENT_DATA_LEN) {
		goto End;
	}
	memcpy(szCurrent, szTTYResult + len - CURRENT_DATA_LEN, CURRENT_DATA_LEN);
	iRet = kstrtoul(szCurrent, 10, &ulCurrent);
	if (0 > iRet) {
		goto End;
	}
	snprintf(SysCurrent->sensor[0].value, sizeof(SysCurrent->sensor[0].value), "%ld", (ulCurrent * 16) + (ulCurrent * 13 / 100));

	iRet = 0;
End:
	return iRet;
}

static struct synobios_ops synobios_ops = {
	.owner                = THIS_MODULE,
	.get_brand            = GetBrand,
	.get_model            = GetModel,
	//.get_rtc_time         = ..., // LemonPi 的 RTC 驱动可能不同，需要适配
	//.set_rtc_time         = ...,
	.get_fan_status       = GetFanStatusByPWM,    // <--【修改】使用新的 PWM 实现
	.set_fan_status       = SetFanStatusByPWM,    // <--【修改】使用新的 PWM 实现
	.get_gpio_pin         = GetGpioPin,           // 通用，保留
	.set_gpio_pin         = SetGpioPin,           // 通用，保留
	.set_power_led        = NULL,                 // 将在 lemonpi.c 中实现并挂载
	.set_disk_led         = SetDiskLedStatusByTrigDiskSyno, // 通用，保留
	.get_sys_temperature  = NULL,                 // 如果有系统温度计，可以实现
	.get_cpu_temperature  = GetCPUTemperature,    // <--【修改】使用我们新写的函数
	// ... (RTC 相关的 auto power on 可能需要适配或移除) ...
	.module_type_init     = InitModuleType,       // 将在 lemonpi.c 中实现
	.uninitialize         = Uninitialize,
	.get_cpu_info		  = GetCPUInfo,           // <--【修改】使用我们新写的函数
	.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO, // 通用，保留
	.get_auto_poweron     = RK3588_GetAutoPowerOn, 
	.set_auto_poweron     = RK3588_SetAutoPowerOn,
	//.hwmon_get_sys_current = NULL,               // <--【移除】删除 MicroP 相关实现
	//.get_sys_current     = NULL,                 // <--【移除】删除 MicroP 相关实现
};
static struct pwm_device *fan_pwm_dev;
int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	module_t* pSynoModule = NULL;

    hym8563_rtc_dev = rtc_class_open("rtc-hym8563");
    if (IS_ERR(hym8563_rtc_dev)) {
        pr_err("synobios_rk3588: failed to open rtc device 'rtc-hym8563'. Timed power on will be disabled.\n");
        hym8563_rtc_dev = NULL; // 确保置空
    }

    fan_pwm_dev = pwm_get(NULL, "pwm-fan");
    if (IS_ERR(fan_pwm_dev)) {
        pr_err("synobios_rk3588: could not get fan pwm\n");
    } else {
        // 可以在这里设置一个初始速度
        SetFanStatusByPWM(FAN_STATUS_RUNNING, FAN_SPEED_LOW);
    }

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
    // for those model that have mix ahci and 9xxx internal disk, please implement GetMaxInternalHostNum
    GetMaxInternalHostNum = NULL;
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
	syno_gpio_init();
#ifdef MY_DEF_HERE
	printk("Synobios %s GPIO initialized\n", syno_get_hw_version());
#endif /* MY_DEF_HERE */

	if (synobios_ops.module_type_init) {
		synobios_ops.module_type_init(&synobios_ops);
	}

	pSynoModule = module_type_get();

	*ops = &synobios_ops;
	if( synobios_ops.init_auto_poweron ) {
		synobios_ops.init_auto_poweron();
	}
#if defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
	syno_get_current = save_current_data_from_uart;
#endif /* CONFIG_SYNO_TTY_MICROP_FUNCTIONS */

	model_addon_init(*ops);
	
	return 0;
}

static int Uninitialize(void)
{
	if (synobios_ops.uninit_auto_poweron) {
		synobios_ops.uninit_auto_poweron();
	}

	return 0;
}

int synobios_model_cleanup(struct file_operations *fops, struct synobios_ops **ops)
{   
	 if (!IS_ERR(fan_pwm_dev)) {
        pwm_disable(fan_pwm_dev);
        pwm_put(fan_pwm_dev);
    }
	syno_gpio_cleanup();
	model_addon_cleanup(*ops);

	return 0;
}

int PWMFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gPWMSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gPWMSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gPWMSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

static struct pwm_device *fan_pwm_dev; 
int SetFanStatusByPWM(FAN_STATUS status, FAN_SPEED speed)
{
	int duty_cycle_percent;
	unsigned int period_ns;
	unsigned int duty_ns;

	if (IS_ERR(fan_pwm_dev)) {
		return PTR_ERR(fan_pwm_dev);
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
    *pStatus = FAN_STATUS_RUNNING; 
    return 0;
}