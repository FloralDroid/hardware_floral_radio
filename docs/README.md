# Floral Radio Simulation

[简体中文](README.zh-CN.md)

This module owns the simulated cellular identity, registration, signal, cell,
SIM, call, and SMS state. It provides the standard Android Radio HAL adapter
through `floral-rild` and publishes
`floral.device.radio.IRadioState/default` for FloralDevice control.

At boot the service first validates
`/ipc/floral_stream/radio.json`. A valid mounted profile becomes the device
identity and enables cellular simulation for that boot. Missing, unreadable,
or invalid content leaves the Radio HAL available but reports radio off, no
SIM, no registration, unknown signal, and no cells. No generated or persisted
profile is used as a fallback.

The validated [example profile](../examples/radio.json) can be copied directly
into a host instance directory as `radio.json`. Identity fields are accepted
only as one coherent profile: MCC and MNC must prefix the IMSI, IMEI and ICCID
must have valid check digits, and phone and SIM fields must satisfy their
declared formats.

Runtime FHC1 control uses bounded leases for registration, signal, cell, and
SIM state. Calls and SMS events remain available through the normal Android
telephony APIs and FHC1 inspection commands. The wire format is documented in
`packages/services/FloralDevice/docs/protocols/FHC1.md`.

## Build integration

```sh
source build/envsetup.sh
lunch redroid_x86_64-userdebug
m floral-rild floral_radio_model_test
```
