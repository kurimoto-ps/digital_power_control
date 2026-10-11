# Repository instructions

This repository hosts parallel board targets under `targets/`:

- `targets/nucleo_h755zi_q/`: existing STM32H755 dual-core starter.
- `targets/jparc_rev58/`: STM32H745 + MAX10 development scaffold.

Read `README.rst`, `docs/DEVELOPMENT.rst`, and `docs/PROJECT_STATUS.rst` before
substantial changes. Follow any target-local AGENTS.md instructions.
Nucleo core ownership, pin assignments, and protocols are target-specific;
rev58 ownership and interfaces must be defined from its supplied hardware documents.

Keep board-specific applications, overlays, protocols, and build definitions
within their target directory. Root `west.yml`, `scripts/`, and `docs/` manage
the shared workspace. Do not commit Zephyr, modules, `.west/`, or build output.

After Nucleo source, Devicetree, Kconfig, CMake, protocol, or shared build-script
changes, run `./scripts/build.sh nucleo_h755zi_q` from the repository root.
Both M7 and M4 images must build. Never infer hardware behavior from build success.
The rev58 target is not buildable yet; document its limitations explicitly.

Update user-facing paths and commands and `docs/PROJECT_STATUS.rst` when changing
the layout, capabilities, or next priorities. Keep commits scoped and descriptive.
Push completed work to the configured remote so it survives host loss.
