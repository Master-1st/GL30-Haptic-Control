# Security Policy

## Supported versions

The project is pre-alpha. Only the latest `main` branch and the most recent tagged release, when one exists, receive security fixes. No firmware or hardware revision is currently certified for safety-critical use.

## Reporting a vulnerability

Do not open a public issue for a vulnerability that could enable unsafe motor activation, bypass a protection gate, expose credentials, corrupt an update, or compromise a host system.

Use GitHub's private vulnerability reporting for this repository:

<https://github.com/Master-1st/GL30-Haptic-Control/security/advisories/new>

Include affected commit/version, reproduction steps, impact, hardware state, and any proposed mitigation. Avoid attaching secrets or unnecessary personal data. An initial acknowledgement is targeted within seven days, but this community project cannot guarantee a fixed response time.

For ordinary functional defects with no security or physical-safety impact, use the bug report template.

## Physical safety is separate

A security review does not establish electrical, mechanical, thermal, functional-safety, EMC, or regulatory compliance. Follow the staged hardware gates in `docs/hardware-gates.md` and stop testing when their conditions are not met.
