# Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

# The MESA API libraries. These are built here (rather than in the top-level
# CMakeLists) so that their build output and SOURCE_DIR live under mesa/, which
# places them under bin/<arch>/mesa/ in the release packet -- mirroring meba/ and
# mepa/. The inputs (API_UNI_SRC, API_MESA_SRC as absolute paths, GLOBAL_DEFS, the
# lm_utils object library and the include directories) are set up by the
# top-level CMakeLists before add_subdirectory(mesa) and inherited here.

# The mesa-ag sources (mesa.c/mesa.h) are generated at build time by the
# top-level CMakeLists (mesa_src target). Their GENERATED source-file property is
# directory-scoped, so mark them GENERATED here too, otherwise add_library()
# below rejects them as "Cannot find source file" before they exist on disk.
set_source_files_properties(
    "${CMAKE_BINARY_DIR}/mesa-ag/mesa.c"
    "${CMAKE_BINARY_DIR}/mesa-ag/mesa.h"
    PROPERTIES GENERATED TRUE)

set(mesa_active_target )
macro(API_TARGET)
  set(oneValueArgs LIB CHIP)
  set(multiValueArgs DEFS)

  cmake_parse_arguments(A "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  if (${BUILD_SHARED_LIBS})
      option(${A_LIB}        "Build the API for ${A_CHIP} (${A_LIB})" OFF)
  endif()
  option(${A_LIB}_static "Build the static API for ${A_CHIP} (${A_LIB})" OFF)
  if (${BUILD_MESA_ALL} OR ${BUILD_ALL})
      if (${BUILD_SHARED_LIBS})
          set(${A_LIB} ON CACHE BOOL "" FORCE)
      endif()

      if (${BUILD_STATIC_LIBS})
          set(${A_LIB}_static ON CACHE BOOL "" FORCE)
      endif()
  endif()

  set(DEFS ${GLOBAL_DEFS} -DVTSS_CHIP_${A_CHIP} ${A_DEFS})

  if (${BUILD_SHARED_LIBS})
      if (${MESA_USE_INLINE_UTILS})
          add_library(${A_LIB} SHARED $<TARGET_OBJECTS:lm_utils> ${API_UNI_SRC} ${API_MESA_SRC})
      else ()
          add_library(${A_LIB} SHARED ${API_UNI_SRC} ${API_MESA_SRC})
          target_link_libraries(${A_LIB} lm_utils)
      endif ()

      target_compile_options(${A_LIB} PUBLIC ${DEFS})

      if (${A_LIB})
          message(STATUS "Adding ${A_LIB} (${A_CHIP}) MESA=${has_mesa} Defines: [${DEFS}]")
          set_target_properties(${A_LIB} PROPERTIES EXCLUDE_FROM_ALL FALSE)
      else()
          set_target_properties(${A_LIB} PROPERTIES EXCLUDE_FROM_ALL TRUE)
      endif()
  endif()

  if (${MESA_USE_INLINE_UTILS})
      add_library(${A_LIB}_static STATIC $<TARGET_OBJECTS:lm_utils> ${API_UNI_SRC} ${API_MESA_SRC})
  else ()
      add_library(${A_LIB}_static STATIC ${API_UNI_SRC} ${API_MESA_SRC})
      target_link_libraries(${A_LIB}_static lm_utils)
  endif ()
  set_target_properties(${A_LIB}_static PROPERTIES OUTPUT_NAME "${A_LIB}")
  target_compile_options(${A_LIB}_static PUBLIC ${DEFS}
                                         PRIVATE -Wno-format-security -Wno-sign-compare -Wno-type-limits -Wno-implicit-fallthrough -Wno-array-parameter -Wno-enum-conversion)

  if (${A_LIB}_static)
      message(STATUS "Adding ${A_LIB}_static (${A_CHIP}) MESA=${has_mesa} Defines: [${DEFS}]")
      set_target_properties(${A_LIB}_static PROPERTIES EXCLUDE_FROM_ALL FALSE)
      list(APPEND mesa_active_target ${A_LIB}_static)
  else()
      set_target_properties(${A_LIB}_static PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()

  # The generated mesa-ag/{mesa.c,mesa.h} are produced by the mesa_src target in
  # the top-level CMakeLists. As these libraries now live in this subdirectory,
  # the implicit generated-source dependency does not cross the directory
  # boundary, so depend on the generator target explicitly to order the build.
  # mesa_src only exists when the mesa-ag layer is generated (MESA_WRAP and not
  # MESA_PRE_AG); with a pre-generated layer (e.g. when MESA is built inside
  # LMStax) there is nothing to depend on, so guard on the target existing.
  if (TARGET mesa_src)
      add_dependencies(${A_LIB}_static mesa_src)
      if (TARGET ${A_LIB})
          add_dependencies(${A_LIB} mesa_src)
      endif()
  endif()

  add_custom_command(
      OUTPUT "include_${A_LIB}/vtss/api/options.h"
      COMMAND mkdir -p include_${A_LIB}/vtss/api
      COMMAND gcc -dM -E ${CMAKE_SOURCE_DIR}/include/vtss/api/options.h ${DEFS} | grep VTSS > include_${A_LIB}/vtss/api/options.h
      DEPENDS "${CMAKE_SOURCE_DIR}/include/vtss/api/options.h"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      COMMENT "Preprocessing options.h for ${A_LIB}"
  )

  list(APPEND LIST_OF_API_TARGETS ${A_LIB})
endmacro(API_TARGET)

API_TARGET(LIB vsc7410               CHIP SERVAL_T)
API_TARGET(LIB vsc7415               CHIP SERVAL_TP)
API_TARGET(LIB vsc7423               CHIP CARACAL_LITE)
API_TARGET(LIB vsc7424               CHIP SPARX_III_10)
API_TARGET(LIB vsc7425               CHIP SPARX_III_18)
API_TARGET(LIB vsc7426               CHIP SPARX_III_24)
API_TARGET(LIB vsc7427               CHIP SPARX_III_26)
API_TARGET(LIB vsc7428               CHIP CARACAL_1     DEFS -DVTSS_OPT_PORT_COUNT=10)
API_TARGET(LIB vsc7429               CHIP CARACAL_2)
API_TARGET(LIB vsc7430               CHIP SERVAL_TE)
API_TARGET(LIB vsc7435               CHIP SERVAL_TEP)
API_TARGET(LIB vsc7436               CHIP SERVAL_2_LITE DEFS -DVTSS_OPT_PORT_COUNT=9)
API_TARGET(LIB vsc7437               CHIP SERVAL_TE10)
API_TARGET(LIB vsc7438               CHIP SERVAL_2)
API_TARGET(LIB vsc7440               CHIP SPARX_IV_34   DEFS -DVTSS_OPT_PORT_COUNT=9)
API_TARGET(LIB vsc7442               CHIP SPARX_IV_52)
API_TARGET(LIB vsc7444               CHIP SPARX_IV_44)
API_TARGET(LIB vsc7448               CHIP SPARX_IV_80   DEFS -DVTSS_OPT_PORT_COUNT=51)
API_TARGET(LIB vsc7449               CHIP SPARX_IV_90)
API_TARGET(LIB vsc7464               CHIP LYNX_2)
API_TARGET(LIB vsc7468               CHIP JAGUAR_2      DEFS -DVTSS_OPT_PORT_COUNT=51)
API_TARGET(LIB vsc7511               CHIP 7511          DEFS -DVTSS_OPT_PORT_COUNT=8)
API_TARGET(LIB vsc7512               CHIP 7512)
API_TARGET(LIB vsc7513               CHIP 7513          DEFS -DVTSS_OPT_AFI_OPTIMIZE_FOR_TIMERS=1)
API_TARGET(LIB vsc7514               CHIP 7514          DEFS -DVTSS_OPT_AFI_OPTIMIZE_FOR_TIMERS=1)
API_TARGET(LIB vsc7546               CHIP 7546          DEFS -DVTSS_OPT_PORT_COUNT=29)
API_TARGET(LIB vsc7549               CHIP 7549          DEFS -DVTSS_OPT_PORT_COUNT=53)
API_TARGET(LIB vsc7552               CHIP 7552          DEFS -DVTSS_OPT_PORT_COUNT=57)
API_TARGET(LIB vsc7556               CHIP 7556          DEFS -DVTSS_OPT_PORT_COUNT=57)
API_TARGET(LIB vsc7558               CHIP 7558          DEFS -DVTSS_OPT_PORT_COUNT=57)
API_TARGET(LIB vsc7546TSN            CHIP 7546TSN       DEFS -DVTSS_OPT_PORT_COUNT=29)
API_TARGET(LIB vsc7549TSN            CHIP 7549TSN       DEFS -DVTSS_OPT_PORT_COUNT=53)
API_TARGET(LIB vsc7552TSN            CHIP 7552TSN       DEFS -DVTSS_OPT_PORT_COUNT=57)
API_TARGET(LIB vsc7556TSN            CHIP 7556TSN       DEFS -DVTSS_OPT_PORT_COUNT=57)
API_TARGET(LIB vsc7558TSN            CHIP 7558TSN       DEFS -DVTSS_OPT_PORT_COUNT=57)
API_TARGET(LIB vsc7558MSEC           CHIP 7558TSN       DEFS -DVTSS_OPT_PORT_COUNT=57)
API_TARGET(LIB lan966x               CHIP 966X)
API_TARGET(LIB lan966x_lm            CHIP 966X          DEFS -DVTSS_OPT_LIGHT=1 -DVTSS_OPT_TRACE=1)
API_TARGET(LIB lan9698RED            CHIP 969X          DEFS -DVTSS_OPT_PORT_COUNT=30)
API_TARGET(LIB lan969x               CHIP 969X          DEFS -DVTSS_OPT_PORT_COUNT=30)
API_TARGET(LIB lan969x_lm            CHIP 969X          DEFS -DVTSS_OPT_PORT_COUNT=30 -DVTSS_OPT_LIGHT=1 -DVTSS_OPT_TRACE=0 -DVTSS_OPT_TRACE_ERROR=0)
API_TARGET(LIB p64h_lm               CHIP P64H          DEFS -DVTSS_OPT_LIGHT=1)
API_TARGET(LIB p64h                  CHIP P64H)

mesa_export_compile_commands_recursive(${mesa_active_target})
# Write the MISRA target database to MESA's own project binary dir (lm-cov-runner
# reads build-<arch>/cmake_target_db.txt). Use PROJECT_BINARY_DIR, not
# CMAKE_BINARY_DIR: when MESA is built as a subdirectory of another project (e.g.
# LMStax) CMAKE_BINARY_DIR is the outer project's build dir, and writing there
# would collide with the outer project's own cmake_target_db.txt.
mesa_export_targets_recursive("${PROJECT_BINARY_DIR}/cmake_target_db.txt" ${mesa_active_target})
