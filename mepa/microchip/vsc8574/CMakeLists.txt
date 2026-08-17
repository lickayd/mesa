MEPA_DRV(LIB_NAME mepa_drv_vsc8574
         SRCS     src/vsc8574.c
         DEFS     -DMEPA_HAS_VSC8574
         INCL_PUB ../../../me/include ../../include
         INCL_PRI src)

option(MEPA_vsc8574 "Add vsc8574 support in libmeba" OFF)
if (${MEPA_vsc8574})
    set(mepa_drvs ${mepa_drvs} mepa_drv_vsc8574 CACHE INTERNAL "")
    set(mepa_defs ${mepa_defs} -DMEPA_HAS_VSC8574 CACHE INTERNAL "")
endif()
