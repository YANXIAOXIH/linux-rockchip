# drivers/syno/synobios/Makefile

subdir-ccflags-y := -I$(src)/include

ifeq ($(CONFIG_SYNO_PLATFORM_PURLEY),y)
    include $(src)/Makefile_vdsm.inc
    synobios-y += $(vdsm-common-obj)
endif

ifeq ($(CONFIG_SYNO_PLATFORM_RK3588),y)
    include $(src)/Makefile_rk3588.inc
    synobios-y += $(rk3588-synobios-objs) 
    ccflags-y += -DSYNO_PLATFORM='"synology_rk3588_ds423"'
endif


synobios-y += synobios.o

obj-$(CONFIG_SYNOBIOS) += synobios.o