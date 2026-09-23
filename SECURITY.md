# Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
# SPDX-License-Identifier: MIT

# Security Policy

## What MESA is

MESA (Microsemi Ethernet Switch API), MEPA (Microsemi Ethernet PHY API), and
MEBA (Microsemi Ethernet Board API) are C libraries that provide a unified,
hardware-abstracted API for configuring and controlling Microchip Ethernet
switch ASICs, PHY devices, and evaluation boards. They are the foundation that
network equipment vendors build their management planes on.

The MEPA and MEBA sources and libraries are distributed as part of the MESA
release package. A MEPA-only customer still downloads the MESA release but
uses only the MEPA libraries and headers.

The libraries are operating-system independent: they have no OS runtime
dependency, no threading model, and no network stack. They are provided out of
context. They are intended to be linked into a vendor application that owns the
OS integration, the management interface, and all security decisions.

> **The MESA, MEPA, and MEBA libraries are considered production-quality and
> suitable for use in commercial products when integrated correctly.** The
> security properties of the resulting product depend on the choices made by
> the integrating application - in particular, input validation, OS hardening,
> and network exposure.

Every MESA release ships two artefacts:

1. **The libraries** - C headers, source code, and pre-compiled binaries for
   arm, arm64, and mipsel targeting a Linux runtime environment.

2. **A demonstration firmware image** - a complete Linux system image that boots
   on Microchip evaluation hardware and runs the `mesa-demo` application and
   `mesa-cmd` CLI commands. This is a convenience for evaluation and integration
   work, not a production software deliverable.

These two artefacts have very different security profiles. The sections below
address them separately.

---

## Use case A: Library integration

This is the primary use case. A vendor application links against the MESA/MEPA/MEBA
libraries and uses them to configure the switch/phy hardware.

**Security responsibilities of the integrating application:**

- Validate and sanitise all inputs before passing them to MESA/MEPA/MEBA API
  calls and to any other APIs provided by the system.
- Select a suitable OS for the target product and apply its security mechanisms
  appropriately - file-system permissions, process isolation, mandatory access
  controls, and network stack hardening as relevant to the deployment.
- Control which network-facing management interfaces are exposed and to whom.
- Manage authentication, authorisation, and access control for the management
  plane.

MESA, MEPA, and MEBA do not accept input from the network, from untrusted
processes, or from any source outside the calling application. The libraries
configure hardware registers in response to API calls; the security of those
calls is entirely the application's responsibility.

**SBOM and CVE scope for library integration:**

All source code needed to build the MESA, MEPA, and MEBA libraries is included
in the release tarball. A dedicated source-only SBOM, `mesa-source.spdx.json`,
is published alongside the release. It covers exactly the library source
packages and nothing else -- no kernel, no rootfs, no demo OS packages.

**For library integration, scan `mesa-source.spdx.json`.** This gives a
focused CVE report against the library components only, without the noise of
hundreds of kernel and rootfs findings that are irrelevant to library use.

The VEX file `.vex/mesa-source.openvex.json` records Microchip's
exploitability assessments for findings against the library source components.
To scan with those suppressions applied:

```
grype --vex .vex/mesa-source.openvex.json sbom:mesa-source.spdx.json
```

---

## Use case B: Demonstration firmware

The demonstration firmware provides a self-contained environment for exercising
and evaluating the MESA/MEPA/MEBA APIs on real Microchip hardware without
needing a full product application.

**What the demonstration firmware consists of:**

The release tarball contains only the sources needed to build the core
MESA/MEPA/MEBA libraries, and code to demonstrate how to integrate the API
into a Linux user-space process (the `mesa/demo/` folder). To produce a
runnable demonstration image, the build system compiles the code in
`mesa/demo/` against those libraries, then combines the resulting binaries with
a **pre-built Linux kernel and root filesystem from the associated Microchip
BSP** (board support package). The BSP provides a complete Buildroot-based
Linux environment. The `mesa-demo` application is installed as
`/usr/bin/mesa-demo`; the operator interacts with it through the `mesa-cmd`
CLI.

> **The code in `mesa/demo/` is not production quality.** It is an
> illustrative example showing one way to integrate the MESA/MEPA API into a
> Linux process. It does not perform comprehensive input validation, does not
> check all return values, and does not apply OS hardening. It must not be used
> as a template for production software without significant additional work.

### Assumed use

The purpose of the demonstration firmware is to make it easy to test and
evaluate the capabilities of the offered APIs. The following conditions are
assumed to hold for any use of the demonstration firmware:

- The device is operated in a **safe, controlled environment**. If connected
  to a network, that network is isolated and operator-controlled; the operator
  has full visibility of and control over all traffic, and no untrusted hosts
  can reach the device.
- The operator is a **trusted party** with physical or console access to the
  device.

> **Security notice:** The demonstration firmware is not hardened for
> deployment on untrusted networks. Network-facing services in the root
> filesystem are not configured with security in mind, and the root account
> uses a publicly known password.

**SBOM and CVE scope for demonstration firmware:**

The full release SBOM, `mesa-binary.spdx.json`, covers both the library source
components and the demonstration OS. In addition to the library source packages,
it includes the Buildroot root filesystem from the BSP, grouped by architecture
under `SPDXRef-brsdk-rootfs-arm`, `SPDXRef-brsdk-rootfs-arm64`, and
`SPDXRef-brsdk-rootfs-mipsel`. Scanning `mesa-binary.spdx.json` will produce a
large number of findings against the kernel and rootfs packages.

Microchip does not publish a VEX file for the demonstration firmware. The
volume of kernel and rootfs CVEs is large, and the vast majority are not
exploitable under the assumed use above -- a safe, isolated, operator-controlled
environment with no untrusted network access. Rather than maintain a
per-finding VEX document that would require continuous updates as new CVEs are
published, Microchip's position is stated once here: **findings against
`SPDXRef-brsdk-rootfs-*` packages in `mesa-binary.spdx.json` should be
evaluated against the assumed use described above.** In a deployment that meets
those conditions, network-reachable attack vectors are not applicable and
local-access vectors require a trusted operator.

Users who require a stricter assurance level for the demonstration firmware
components, or who cannot meet the assumed-use conditions, should conduct their
own assessment against `mesa-binary.spdx.json`.

```
grype sbom:mesa-binary.spdx.json
```

---

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues,
pull requests, or any other public forum.**

Suspected security vulnerabilities in MESA, MEPA, or MEBA should be reported
to Microchip's Product Security Incident Response Team (PSIRT):

- <https://www.microchip.com/psirt>

Responsible disclosure gives us the opportunity to investigate and address the
issue before it is made public.

---

## Vulnerability monitoring

*Automated vulnerability monitoring via CI is planned and not yet in place.*

Until then, if your use of MESA requires active vulnerability tracking:

- For library integration: scan `mesa-source.spdx.json` and apply
  `.vex/mesa-source.openvex.json` to suppress known not-affected findings.
- For demonstration firmware: scan `mesa-binary.spdx.json` and evaluate
  findings against the assumed use described above.
- Establish your own alerting and remediation pipeline according to your risk
  tolerance and timelines.
