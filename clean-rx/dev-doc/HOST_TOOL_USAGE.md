# Host collection and paired WPA2 trials

Desktop tooling and paired hardware procedures for the badge host. These
commands are separate from firmware builds and offline tests. Hardware
execution requires its own authorized scope and matched installed images.

The badge application's independent-peer receive diagnostic is RX-only.
It checks fresh broadcast data addresses, SNAP/payload, fixture sequence,
complete MPDU FCS, HT20 metadata and matching terminal HT-SIG, recording
the received sequence bitmap and duplicate count separately.
The collector uses `auth_bad_fcs_report` to distinguish the rejected
authentication snapshot from a validated response.

The collector accepts:

```sh
build/clean-rx/badge/collector SERIAL evidence.bin CHANNEL \
  --ap-wpa2 BSSID PRIVATE_CREDENTIAL_FILE
```

The credential file belongs to the current user, has mode `0600`, and
contains exactly two LF-terminated lines: SSID (1..32 bytes) and passphrase
(8..63 bytes). The collector derives the PMK and sends it with a fresh
SNonce through the dedicated secret-control path, bound to the BSSID,
channel and request. This is separate from the controller's raw
hardware-key command.

For a paired WPA2 fixture, install the unmodified vendor `usb-wifi-link`
image on one badge and the clean image on the other, leaving both in
BOOTSEL. Run:

```sh
CLEAN_RX_COLLECTOR=build/clean-rx/badge/collector \
  bash clean-rx/tools/badge/wpa2_trial.sh AP_SERIAL STATION_SERIAL build/new-wpa2-trial
```

The runner boots both USB applications before starting the AP, starts
station collection at AP readiness, and requires independent matching-peer
authorization and protected traffic. It writes phase times and both
endpoints' evidence under the new prefix. `--wrong-password` selects the
negative control. Temporary credentials are private and removed on exit;
the runner does not flash either badge.
