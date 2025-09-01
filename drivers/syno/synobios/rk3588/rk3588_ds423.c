// Copyright (c) 2000-2015 Synology Inc. All rights reserved.

#include <linux/cpumask.h>
#include "rk3588_common.h"
#include "syno_ttyS.h"
#ifdef CONFIG_SYNO_LEDS_TRIGGER_DISK
#include "../led/led_trigger_disk.h"
#include <linux/syno_gpio.h> 
#define GPIO_POWER_LED 35 
#endif /* CONFIG_SYNO_LEDS_TRIGGER_DISK */
#define MAX_SUPPORTED_DISKS 4

#include <linux/mm.h>
#include <linux/sched.h>

int GetModel(void)
{
	return MODEL_RK3588_DS423;
}

int InitModuleType(struct synobios_ops *ops)
{
	PRODUCT_MODEL model = GetModel();
	module_t *pType = NULL;

	module_t type_rk3588_ds423 = MODULE_T_RK3588_DS423; 

	switch (model) {
	case MODEL_RK3588_DS423:
		pType = &type_rk3588_ds423;
		break;
	default:
		break;
	}

	if (pType) {
		module_type_set(pType);
	}

	return 0;
}

int SetPowerLedStatus(SYNO_LED status)
{
	char szCommand[5] = {0};
	int err = -1;

	switch(status){
		case SYNO_LED_ON:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_ON);
			break;
		case SYNO_LED_OFF:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_OFF);
			break;
		default:
			goto ERR;
	}

	err = 0;
ERR:
	return err;
}

static int  __maybe_unused SetHddLed(SYNO_LED status)
{
	int err = -1;

	switch(status) {
		case SYNO_LED_ON:
			SYNO_ENABLE_HDD_LED(1);
			break;
		case SYNO_LED_OFF:
			SYNO_ENABLE_HDD_LED(0);
			break;
		default:
			goto ERR;
	}

	err = 0;
ERR:
	return err;
}

static int __maybe_unused Fake_HddDetect(unsigned int disk_nr)
{

    if (disk_nr > 0 && disk_nr <= MAX_SUPPORTED_DISKS) { 
        return 0; 
    }
    return 1;
}

static int __maybe_unused Fake_HddEnable(unsigned int disk_nr, int enable)
{
	// Do nothing because we can't control power via GPIO
	pr_info("Fake_HddEnable called for disk %u with state %d\n", disk_nr, enable);
	return 0; // Always return success
}

int SetPowerLedStatus_RK3588(SYNO_LED status)
{
	// 这里我们暂时还使用占位符，因为直接操作GPIO需要更复杂的内核API
	// 但是我们的目标是替换掉错误的串口实现
	// 在下一阶段，我们会用 gpiod_set_value() 来实现它
	pr_info("SetPowerLedStatus_RK3588 called with status: %d\n", status);
	return 0;
}
/*
 *  DS423 GPIO config table
 *
 *  Pin     In/Out    Function
 *
 *   3      Out       HDD green LED 1
 *   4      Out       HDD green LED 2
 *  18      Out       HDD orange LED 1
 *  19      Out       HDD orange LED 2
 *  22      Out       Front Panel LED Control
 *  34      Out       HDD LED Control
 *  35      Out       LAN LED Control
 *  41       In       HDD Detect 1
 *  42       In       HDD Detect 2
 *  51      Out       LAN LED
 *  57      Out       Fan control full
 *  59      Out       Fan control high
 *  60      Out       Fan control middle
 *  61      Out       Fan control low
 *  62      Out       HDD Power Enable 2
 *  67      Out       HDD Power Enable 1
 *  68      Out       Fan control voltage
 *  70      Out       USB3 Power Enable 1
 *  71       In       USB3 OC 1
 *  72      Out       USB3 Power Enable 2
 *  73       In       USB3 OC 2
 *
 */

/*
 *  DS423 other control
 *
 *  Fan fail		MicroP
 *  LAN LED			Realtek driver (r8169soc_1619.c, rtd-1619b-synology-ds423.dts)
 *
 */

static SYNO_GPIO_INFO __maybe_unused sys_led_gpio = {
	.nr_gpio		= 1,
	.gpio_port		= {27}, // GPIO0_PD3 -> 编号 27
	.gpio_polarity	= ACTIVE_LOW, // DTS 中明确定义为 ACTIVE_LOW
};

// 假设的硬盘相关 GPIO (您需要找到实际的引脚)
static SYNO_GPIO_INFO  __maybe_unused hdd_detect = {
	.nr_gpio		= 2,
	.gpio_port		= {100, 101}, // 假设的 GPIO 编号
	.gpio_polarity	= ACTIVE_LOW,
};
static SYNO_GPIO_INFO  __maybe_unused hdd_enable = {
	.nr_gpio		= 2,
	.gpio_port		= {102, 103}, // 假设的 GPIO 编号
	.gpio_polarity	= ACTIVE_HIGH,
};


void syno_gpio_init(void)
{
	// 清理所有旧的注册，防止冲突
	syno_gpio.hdd_detect = NULL;
	syno_gpio.hdd_enable = NULL;
	syno_gpio.hdd_fail_led = NULL;
	syno_gpio.hdd_present_led = NULL;
	syno_gpio.disk_led_ctrl	= NULL;
	syno_gpio.phy_led_ctrl = NULL;
	
	// 如果你的板子有风扇故障检测GPIO，在这里注册
	// syno_gpio.fan_fail = &fan_fail;
}

void syno_gpio_cleanup(void)
{
	// 保持为空或与 init 对应
}

int model_addon_init(struct synobios_ops *ops)
{
	// 使用我们新的、基于GPIO的电源灯函数（目前是占位符）
	ops->set_power_led = SetPowerLedStatus_RK3588;

	// 硬盘灯函数暂时也禁用，因为我们无法控制
	ops->set_hdd_led = NULL;
	
	// PHY LED, DS423没有，您的板子也没有，设为NULL是正确的
	ops->set_phy_led = NULL;

	// 风扇状态获取，将在 rk3588_common.c 中实现
	// GetFanStatusByPWM 仍然是我们的目标
	ops->get_fan_status = GetFanStatusByPWM;
	
	return 0;
}

int model_addon_cleanup(struct synobios_ops *ops)
{
	return 0;
}
