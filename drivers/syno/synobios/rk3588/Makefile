obj-m += rk3588-ds423-synobios.o

common-obj += \
		../common/common.o \
		../i2c/i2c-linux.o \
		../rtc/localtime.o \
		../rtc/alarmtime.o \
		../syno_ttyS/syno_ttyS.o \
		../synobios.o \
		../mapping.o \
		../rtc/rtc-pericom-lib.o \
		../rtc/rtc-linux-pericom.o \
		../led/led_trigger_disk.o \
		rk3588_common.o

rk3588-ds423-synobios-objs = rk3588_ds423.o $(common-obj)

