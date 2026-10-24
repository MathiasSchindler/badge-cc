# Hardware trial protocol

This is the current operator checklist, not a record that a campaign passed
or authorization to access a device. Experiment-specific commands, serial
bindings, hashes and trial results belong in the relevant experiment record.

## Before a trial

1. Obtain explicit authorization for the named devices, operation and RF
   scope. The earlier one-device C04 receive-only authorization has ended.
2. Choose the question, independent oracle, positive/negative controls,
   acceptance limits and stopping conditions before collecting results.
3. Build matched controller, RP2350 host and collector selections. Record
   source revision, tool versions, flags, image hashes and peer configuration.
   A newer collector is not automatically compatible with an installed image.
4. Back up full borrowed-device flash, retain the backup hash and verify
   readback. Establish a restoration/recovery procedure before flashing.
5. Use a new evidence prefix. Keep credentials private and out of reports.
   An offline build/test command must not discover, flash or run devices.

## During and after a trial

- Retain every attempt, including missing captures, invalid controls,
  partial reports, startup faults and failed fresh repeats.
- Distinguish submitted TX, controller completion, retained bytes, actual F2
  delivery and independently received over-air frames.
- Do not treat software FCS, sampled TSF/IFS or mailbox counters as independent
  proof of physical EOF-to-air SIFS or loss-free reception.
- Record build/role/channel/rate/size/duration, denominators and exclusions.
  Rejected hypotheses remain evidence with their tested conditions.
- Remove temporary credentials. Restore borrowed-device flash and compare
  the full readback to the saved backup; retain verification evidence.
- Put results in an experiment record. Update the issue register only when
  the new evidence changes its status; do not copy the trial narrative there.

## Record contents

Each record identifies its question, tested revisions and profiles, device
roles, method and controls, observations, failures, interpretation and limits,
artifact locations and hardware restoration. Older records retain their
original structure where changing it could obscure chronology or provenance.

Current unresolved qualifications are in [the issue register](../issues/README.md).
## Runtime profile acceptance

C05 changes ordinary ACK-path timing and command admission deliberately.
The earlier C04 receive-only device check does not qualify it, and its
one-time hardware authorization has ended. This checklist grants no
authorization; a completed campaign qualifies only its recorded scope.

Before qualifying C05 on hardware, obtain new authorization and retain
backup/restore safeguards. Test the new runtime profile for receive/F2
integrity, normal legacy/HT v1 TX, ACK/CTS response timing, station policy,
passive deadline/fault evidence and recovery. Confirm diagnostic-command
errors without traffic injection. Separately qualify diagnostic ACK-loss,
HT/aggregate and SDIO campaigns only within their authorized scope.
Record matched host/controller profile, flags, hashes and independent
on-device evidence; native/byte-identity results do not substitute for it.

