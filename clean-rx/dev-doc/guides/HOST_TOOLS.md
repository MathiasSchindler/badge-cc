# Host tools and report interpretation

Desktop tooling and paired hardware procedures for the badge host. These
commands are separate from firmware builds and offline tests. Hardware
execution requires [the trial protocol](HARDWARE_TRIALS.md), its own authorized
scope and matched installed images.

## WPA2 collection

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

## Paired WPA2 trials

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


## Desktop report fields

HT probe/data/ACK collector records expose `ht20_format`,
`ht20_width_mhz`, `ht20_gi_ns`, `ht20_streams`, `ht20_rate_kbps`,
`ht20_symbols`, `ht20_expected_airtime_us` and `ht20_lsig_length`.
These describe the requested configuration and calculated airtime, not
independently measured on-air duration. Passive monitoring does not invent
a requested configuration.

Peer reports expose `ht_peer_profile_source` and `ht_peer_runtime_updates`.
The selectable candidate is reported in `ht20_candidate_tx_mcs_mask` and
`ht20_candidate_max_mcs`; unavailable candidates use mask 0 and MCS -1.
The local width, GI, stream count, RX mask and serialized target IE do not
indicate negotiated HT operation. `ht20_active` and `ht_advertised` remain
false.

WMM collector fields are `wmm_report_valid`, `wmm_offered`,
`wmm_negotiated` and `wmm_policy_committed_updates`. The last counts applied,
controller-readback-verified policies, not queued requests.
`wmm_policy_queued_updates` is an equal compatibility alias.
`wmm_edca_enabled` uses controller EDCA status at USB detail offset 752.
`protected_amsdu_frames` and `protected_amsdu_subframes` count newly delivered
validated protected aggregates, not retries, F2 fragments or A-MPDUs.

## Finite aggregate reports

Use [diagnostic profile navigation](DIAGNOSTIC_PROFILES.md) for selection and
campaign evidence rather than treating report booleans as general capability.

- `multi_mpdu_radio_capture` describes retained PSM snapshots.
  `delivered_subframe_bitmap`, `delivery_payloads_valid`,
  `actual_multi_mpdu_delivery` and `all_subframes_delivered` describe actual
  delivered payloads, not the snapshot alone.
- `shm_delivery_enabled` and `dma_experiment_enabled` identify distinct
  paths. SHM delivery reports `shm_delivery_enabled:true` and
  `dma_experiment_enabled:false`. Saved bytes occupy detail offsets `1664 + 80 * sequence` in this
  mode's 2,048-byte report, replacing its beacon inventory.
- `shm_terminal` reports publication/claim/release counters, claimed state,
  PSM TSF timestamps and controller snapshot hold time. These use the TSF
  domain, not an independent physical EOF/SIFS clock.
- `ba_experiment` contains the bounded response bitmap/errors and sampled
  EOF/IFS times. Pair `compressed_ba_return` contains actual sender receipt.
  An armed positive pair requires a valid returned BA bitmap matching
  delivered bodies; local TX counters do not substitute for receipt.
- `immediate_ba`, `sifs_timing_qualified` and `ht_qualified` are distinct
  qualification claims, not synonyms for a successful finite diagnostic.

The finite three-MPDU collector options are `--ht-ampdu-probe-tx` and
`--ht-ampdu-probe-rx`, each followed by a fresh 28-hex-digit challenge and
MCS0..7. The paired runner is `clean-rx/tools/badge/ht_probe.sh` with
`TX_SERIAL RX_SERIAL NEW_PREFIX MCS --ampdu-probe`. These are hardware
operations subject to the trial protocol, not offline checks.
