#!/usr/bin/env bash
root=$(cd "$(dirname "$0")/../../.." && pwd) || exit 1
exec bash "$root/clean-rx/tools/badge/wpa2_trial.sh" "$@"
