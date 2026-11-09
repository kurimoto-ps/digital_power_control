# Additional verification tests

J-PARC rev58 / TEP521C motherboard — PLC/UART, ACC inputs, mixed ADC/DAC

[日本語](ADDITIONAL_TESTS.md)

**Perform these tests after updating MAX10 firmware to v2.0.2.** The MAX10 update procedure will be supplied separately and is outside this guide.

This guide assumes you have completed the previous ADC/DAC tests, cloned the repository and retained the same Docker development environment. Complete common preparation here once, then follow the three test pages in order. Do not reprogram STM32 between tests.

## Test sequence

| Order and procedure | What to verify | Connections and equipment |
|---|---|---|
| **1. [PLC_UART_TEST](PLC_UART_TEST_en.md)** | Sixteen PLC→STM32 inputs, sixteen STM32→PLC outputs, bidirectional RS232 | PLC, motherboard J3/J4, J5 RS232 and external terminal at 115200 baud / 8N1 |
| **2. [ACC_INPUT_TEST](ACC_INPUT_TEST_en.md)** | Held High/Low mapping of 96 kHz and TRIG to MAX10/STM32; 12 MHz start/stop detection | J16/J17 coax with 0/3.3 V source, **separate J6 optical input for 12 MHz**, waveform measurement equipment |
| **3. [MIXED_ADC_V2](MIXED_ADC_V2_en.md)** | 48-byte / eleven-channel reception, ADC24 dummy validation, new ADC optical calibration, APPLY and DAC linkage | ADC / O/E / E/O paths, known analog inputs, DAC voltage measurement equipment |

J16/J17 coax cannot test the 12 MHz path. The 96 kHz/TRIG test checks instantaneous wiring levels, not frequency or trigger history.

The current STM32 `mixed_adc` image also includes Ethernet, SPI4, SPI1 circular DMA and the 8 MiB SDRAM startup test. M7 runs these functions; M4 remains disabled. The additional functions have build/host validation; these physical tests are still to be performed by the operator.

## Common preparation 1: update the existing repository

In the existing Docker terminal:

```sh
cd /workdir/zephyrproject/digital_power_control
git status --short
```

If this prints nothing, continue below. If it lists `M`, `??` or other entries, use the local-edit procedure first.

```sh
git switch feature/jparc-rev58-zephyr-layout
git pull --ff-only origin feature/jparc-rev58-zephyr-layout
git rev-parse --short HEAD
```

Run one line at a time; stop on errors. `Already up to date.` is a successful result. Record the final commit ID.

### Local edits or pull blocked by edits

`Your local changes ... would be overwritten` means Git stopped to preserve your edits. Do not delete the files.

1. Copy important edits and test logs outside the repository, including logs ignored by Git.
2. Save local changes temporarily from the repository root:

   ```sh
   git stash push -u -m "before-additional-tests"
   git stash list
   git status --short
   ```

   Stash saves edits locally and restores clean working files. `-u` includes new files but not ignored files. Confirm `before-additional-tests` appears in the list and short status is empty. Stashes are not uploaded to GitHub; preserve this environment and your separate backups.
3. Run the switch, pull and commit-ID commands above. **Leave personal edits stashed until testing is complete, so the delivered version is used.**
4. To restore edits afterwards, inspect `git stash list`. Only if `before-additional-tests` is at `stash@{0}`, run:

   ```sh
   git stash apply 'stash@{0}'
   ```

   Apply retains the saved stash. If the entry has another number or Git reports `CONFLICT`, stop and consult the maintainer. Restored code differs from the delivered version tested here.

If Git reports `Not possible to fast-forward`, divergent branches or conflicts, stop. Local commits need review and are not resolved by stashing. Send the full error and these outputs to the maintainer; do not force updates or delete files:

```sh
git status
git log --oneline -5
```

For authentication/network errors, send the full error too; stashing does not fix those problems.

## Common preparation 2: update STM32 once

Confirm MAX10 v2.0.2 has already been installed. With the existing ST-LINK connection and power arrangement, run each line from the repository root, proceeding only after success:

```sh
./scripts/build.sh jparc_rev58 mixed_adc
python3 targets/jparc_rev58/scripts/verify_build.py ../build-jparc-rev58-mixed_adc
./scripts/flash.sh jparc_rev58 mixed_adc
```

Use the image generated from the newly updated source, not an older image with the same profile name. Output: `../build-jparc-rev58-mixed_adc/zephyr/zephyr.hex`. The existing BCM4 disabled / BCM7 enabled configuration remains required. Stop and retain logs if any command fails.

## Common preparation 3: Ethernet and version check

Use the previous wired network: host `192.168.100.1/24`, STM32 `192.168.100.58`, TCP port `4244`. Scripts use only the Python 3 standard library; no pip packages are required.

```sh
cd /workdir/zephyrproject/digital_power_control/targets/jparc_rev58/scripts
python3 spi4_client.py info
```

| Field | Expected value |
|---|---|
| device_id | `0x4a503538` |
| version | `0x00020002` (v2.0.2) |
| capabilities | `0x000000ff` |

If there is no response or values differ, resolve connectivity/programmed versions before testing. For another address, put `--host IP` before each script's subcommand.

**All commands on the test pages run from this scripts directory.** Repeat the absolute `cd` above when opening another terminal. Run one client at a time.

Continue to **[1. PLC/UART testing](PLC_UART_TEST_en.md)**, then follow the Next links at the bottom of each page.

## Retain results

Record board ID, date, operator, Git commit and FPGA version. Keep generated JSON and external terminal/instrument observations. JSON alone cannot verify external PLC indications or analog DAC voltage.

- PLC: all sixteen inputs and sixteen outputs, final outputs low. UART: received data and transmitted data observed on the external terminal.
- ACC: J16/J17 mapping and J6 optical 12 MHz stop→start→stop as separate results. Mark unavailable tests as not performed.
- Mixed ADC: dummy validation, channel mapping, optical calibration settings, APPLY results and measured DAC-linked voltages.

Copy logs outside the repository before future Git operations. Record failures and untested items explicitly. Follow each test page's final-output instructions.
