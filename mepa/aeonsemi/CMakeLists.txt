# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

set(MEPA_as2xxxx_custom_defs)

MEPA_DRV(LIB_NAME mepa_drv_as2xxxx
         SRCS     src/as2xxxx.c
                  # private sources
                  src/priv/as2xxxx_priv.c
                  src/priv/mdio.c
                  src/priv/ipc.c
                  src/priv/eye.c
		 DEFS	  ${MEPA_as2xxxx_custom_defs}
         INCL_PUB ../../me/include ../include
         INCL_PRI src)

option(MEPA_as2xxxx "Add AS2XXXX support in libmeba" OFF)

if (${MEPA_as2xxxx})
    message(STATUS "MEPA_as2xxxx defined")
    set(mepa_drvs ${mepa_drvs} mepa_drv_as2xxxx CACHE INTERNAL "")
    set(mepa_defs ${mepa_defs} -DMEPA_HAS_AS2XXXX CACHE INTERNAL "")
endif()
