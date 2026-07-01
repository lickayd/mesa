// Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "microchip/ethernet/switch/api/misc.h"
#include "microchip/ethernet/switch/api/capability.h"
#include "microchip/ethernet/switch/api/init.h"

// The MESA libraries reference the application-provided trace callouts. Some
// capabilities (e.g. MESA_CAP_L2_REDBOX_CNT on the LAN969x family) emit trace
// while being resolved, which triggers lazy binding of these symbols. Without
// them the dumper aborts with a symbol lookup error and the capability ends up
// recorded as <unknown>. The dumper does not care about trace, so provide
// no-op stubs purely to satisfy the linker.
void mesa_callout_trace_printf(const mesa_trace_layer_t layer,
                               const mesa_trace_group_t group,
                               const mesa_trace_level_t level,
                               const char              *file,
                               const int                line,
                               const char              *function,
                               const char              *format,
                               va_list                  args)
{
}

void mesa_callout_trace_hex_dump(const mesa_trace_layer_t layer,
                                 const mesa_trace_group_t group,
                                 const mesa_trace_level_t level,
                                 const char              *file,
                                 const int                line,
                                 const char              *function,
                                 const uint8_t           *byte_p,
                                 const int                byte_cnt)
{
}

int main(int argc, char *argv[])
{
    int           i, cap;
    void         *handle;
    mesa_inst_t   inst = 0; // NULL -> capabilities are read from the default instance
    unsigned long target;
    uint32_t (*capability)(mesa_inst_t, int);
    mesa_rc (*inst_get)(mesa_target_type_t, mesa_inst_create_t *);
    mesa_rc (*inst_create)(const mesa_inst_create_t *, mesa_inst_t *);

    char *error;

    if (argc < 4) {
        printf("Usage: %s <so-file> <target> <capability>...\n", argv[0]);
        printf("  <target> is the mesa_target_type_t value (hex or decimal) to\n"
               "  instantiate so that instance-dependent capabilities resolve.\n"
               "  Pass 0 to query the default (NULL) instance only.\n");
        exit(1);
    }

    handle = dlopen(argv[1], RTLD_LAZY);
    if (!handle) {
        fputs(dlerror(), stderr);
        exit(1);
    }

    capability = dlsym(handle, "mesa_capability");
    if ((error = dlerror()) != NULL) {
        fputs(error, stderr);
        exit(1);
    }

    // Some capabilities (e.g. MESA_CAP_L2_REDBOX_CNT and the other
    // mesa_feature()-gated ones) are answered from an instance's state rather
    // than a compile-time constant. Create an instance for the requested target
    // so those resolve to real values instead of aborting on a NULL instance.
    target = strtoul(argv[2], NULL, 0);
    if (target != 0) {
        inst_get = dlsym(handle, "mesa_inst_get");
        inst_create = dlsym(handle, "mesa_inst_create");
        if (inst_get && inst_create) {
            mesa_inst_create_t create;
            if ((*inst_get)((mesa_target_type_t)target, &create) != MESA_RC_OK ||
                (*inst_create)(&create, &inst) != MESA_RC_OK) {
                // Fall back to the default instance; static capabilities still
                // resolve, instance-dependent ones return 0.
                fprintf(stderr, "%s: could not create instance for target 0x%lx\n", argv[1],
                        target);
                inst = 0;
            }
        } else {
            fprintf(stderr, "%s: mesa_inst_get/mesa_inst_create not found\n", argv[1]);
        }
    }

    for (i = 3; i < argc; ++i) {
        cap = atoi(argv[i]);
        printf("%d %u\n", cap, (*capability)(inst, cap));
        fflush(stdout);
    }

    dlclose(handle);

    return 0;
}
