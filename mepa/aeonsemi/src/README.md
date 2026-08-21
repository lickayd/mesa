# Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

# AeonSemi AS2XXXX PHY Driver Overview

## Architecture at a Glance

```
┌─────────────────────────────────────────────┐
│                 Application                 │
└───────────────────────┬─────────────────────┘
                        │ mepa_driver_t callbacks
┌───────────────────────▼─────────────────────┐
│               as2xxxx.c                     │  ← MEPA API layer
│  probe / poll / conf_set / loopback_set ... │
└───────────────────────┬─────────────────────┘
                        │ as2xxxx_priv_*()
┌───────────────────────▼─────────────────────┐
│            as2xxxx_priv.c                   │  ← Core logic layer
│  firmware boot / speed config / IRQ / eye   │
└──────┬────────────────┬──────────┬──────────┘
       │                │          │
 ┌─────▼──────┐   ┌─────▼──────┐   │
 │   eye.c    │──►│   ipc.c    │   │
 │   diag     │   │  FW msgs   │   │
 └────────────┘   └─────┬──────┘   │
                        │          │
                        └────┬─────┘
                             │
                      ┌──────▼──────┐
                      │   mdio.c    │
                      │   CL22/45   │
                      └──────┬──────┘
                             │
                    MEPA callouts
              (miim_read/write, mmd_read/write)
                             │
                      ┌──────▼──────┐
                      │   Hardware  │
                      │   AS2XXXX   │
                      └─────────────┘
```

---

## Layer Descriptions

### `as2xxxx.c` — MEPA API Layer

Implements the `mepa_driver_t` vtable: translates MEPA types to chip ones, holds the
device lock for the whole MEPA call, applies driver defaults, renders the debug dump.

**`mepa_if_set()` has an ordering requirement.** The host lane does not rate-adapt: it runs
at the line side speed, and nothing in the PHY bridges a mismatch. The driver reports
`MEPA_CAP_HOST_RATE_FOLLOWS_LINE` = 1 to say the application has to take part. On link-up the
application must configure its own MAC and SerDes to the negotiated line speed and **then**
call `mepa_if_set()`.

The call carries an interface, not a speed — and `MESA_PORT_INTERFACE_SFI` spans 5G/10G/25G —
so it cannot say which rate the MAC is at. It does not need to: the host rate is whatever the
line negotiated, so the call means "my side is now at the line side speed" and the driver
derives the rest. Calling it *before* configuring the MAC moves the PHY away from the MAC and
takes the link down.

Before the first link there is no line speed to follow, so `if_set()` leaves the lane as the
firmware booted it — probe-time calls are harmless.

### `as2xxxx_priv.c` — Core Logic Layer

Parameter translation, sequencing, error propagation.

Every entry point takes an `as2xxxx_priv_data_t *`, not a `mepa_device_t *`. The handle
is allocated by `as2xxxx.c` inside its private data and carries the device pointer, so
this layer assumes nothing about where in the allocation it sits. `as2xxxx_priv_probe()`
is the exception: it takes both and records the device on the handle.

**This layer does not lock.** The MEPA API layer holds the device lock across the whole
operation, so one call is one critical section and composites such as `poll` see a
consistent snapshot. Every `as2xxxx_priv_*()` must be called with the lock held.

### `mdio.c` — Register Access

Thin wrappers around the MEPA callouts. All hardware access is centralised here.

### `ipc.c` — Firmware IPC

For operations the firmware owns: AN config, SerDes tuning, eye capture, temperature.

```
ipc_send_msg(dev, state, msg)     →  write cmd + data regs, poll, read response
ipc_do_operation(dev, state, op)  →  write input buffer, send DBGCMD, poll, read output
```

A **parity bit** (bit 15 of the command register) toggles every transaction and the
firmware echoes it in its status, which is how stale responses are detected.

```
Host writes:  [ CMD | parity=P ]  →  data regs 0..7
Poll:         status register until READY or ERROR
Host reads:   response from data regs 0..7
Next command: parity=!P
```

The per-device `ipc_state_t` holding that flag is passed in rather than looked up, so
this file needs no knowledge of the private data layout. `as2xxxx_priv.c` owns the
storage and supplies `&pd->ipc`.

### `eye.c` — Eye Diagram

31 IPC reads of one phase group each (4 phases × 254 voltage steps), printed straight to
ASCII. Resolution is compile-time because every buffer is sized from it:

```
EYE_FULL_RESOLUTION 0   (default)  64×64   — every 2nd group, every 4th row, ~6.7 kB stack
EYE_FULL_RESOLUTION 1              124×254 — all groups, all rows,          ~33.4 kB stack
```

No heap, no intermediate bitmap. A sample is how often the offset slicer disagreed with
the reference decision, out of `EYE_COMPARES_PER_POINT` (1024): low is open, half is
uncorrelated. Word 0 of each group is a tag echoing the request, and the driver checks it.

| | AS21xxx | AS22xxx |
|---|---|---|
| Group index in the request | high byte | **low byte** |
| Sample level inside the eye | 0 | ~a quarter of the comparisons |

Get the request order wrong and the reply is still well formed, but always for the same
phase — the diagram looks like real-but-poor data rather than an error. The AS22xxx order
is the vendor's: `aeon_ipc_eye_scan()` in 2.5.1 sends `data[0] = sds_id, data[1] = grp`.
The open/closed threshold is derived per scan from the measured levels, because the inner
level is part-dependent.

`EYE_RAW_HEX_DUMP` dumps the raw words for replotting in an external tool.

## File Layout

Only the MEPA API layer and shared helpers sit at the top level. Everything the
application has no business seeing lives in `priv/`.

```
as2xxxx.c                MEPA API layer: vtable, policy, debug dump
as2xxxx_utils.h          shared: logging config, logger, RC macros, NULL_CHECK, misc
priv/as2xxxx_priv.{c,h}  core logic
priv/as2xxxx_registers.h register addresses, masks, command enums
priv/as2xxxx_bitfields.h FIELD_PREP / FIELD_GET / GENMASK / BIT_MODIFY macros
priv/as2xxxx_fw_img.h    embedded firmware images, one per family
priv/ipc.{c,h}           firmware mailbox
priv/eye.{c,h}           eye diagram capture
priv/mdio.{c,h}          register access
```

`as2xxxx_bitfields.h` is separate from `as2xxxx_utils.h` because
`as2xxxx_registers.h` needs the bit-field macros, and pulling `utils.h` in would drag the
OS and trace headers into the register map.

## Logging

Three switches in `as2xxxx_utils.h`, which is the only place their values live.

`AS2XXXX_LOG_TO_PRINTF` picks the back end. At 1 every enabled line goes to stdout as
`AS2XXXX [E] function:line: message`, with no trace level to set per boot — that is what
makes bring-up readable from the console alone. At 0 the lines go through MEPA's
`T_D/T_I/T_W/T_E` and the CLI trace level applies, so the driver stays quiet unless asked;
a shipping build wants 0. The firmware load progress bar writes to stdout either way.

`AS2XXXX_LOG_ENABLE_PRIV` and `AS2XXXX_LOG_ENABLE_IPC` enable the lower layers, off in a
normal build. A failure is reported by every layer it passes through, so leaving them on
multiplies one fault into several lines — worst on the poll path, once per second per
port. `PRIV` for a misbehaving private-layer call; `IPC` only when the mailbox itself is
suspect, since it logs every transaction.

The MEPA API layer has no switch: it calls MEPA's `T_D/T_I/T_W/T_E` directly, because that
is what the application sees. Layers below bind their module once:

```c
/* top of as2xxxx_priv.c */
#define LOG_E(fmt, ...)        AS2XXXX_LOG_E(PRIV, fmt, ##__VA_ARGS__)
#define RC_LOG(expr, msg, ...) AS2XXXX_RC_LOG(PRIV, expr, msg, ##__VA_ARGS__)
```

A disabled module costs no code and no format strings, but its arguments are still
type-checked, so it cannot rot. The module name is compile-time only and does not appear
in the output, which identifies its origin by function and line.

---

