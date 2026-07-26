#line 1 "/home/hermes/micro-radar/PROJECT_NOTES.md"
# Project Notes

## Stopping Point — 2026-07-26

### Current branch
- `readsb-support` (tracking `origin/readsb-support`)

### Latest shipped behavior
- PPI sweep/trail visibility restored and strengthened.
- Beam-hit persistence increased so blips visibly re-energize on sweep contact.
- Fade/refresh background artifacts reduced (smaller erase radius, no hard erase on each fade step, static grid restore under erase).
- Startup now includes **10-second radar warm-up** before first readsb sync.
- After warm-up, display waits on first valid readsb frame, then starts normal sweep.

### Recent commits (newest first)
- `10d873e` — tune(startup): set radar warm-up standby to 10 seconds
- `84db033` — feat(startup): add radar warm-up standby screen before first readsb sync
- `d46b85a` — tune(radar): restore visible PPI trail and stronger beam-hit persistence
- `6f26a82` — fix(radar): eliminate fade refresh background artifacts around aircraft
- `c06ee23` — feat(radar): gate sweep on first readsb sync and smooth persistence fade

### Validation state
- PlatformIO build for `nodemcu`: **passing**.
- Exported binary refreshed at `bin/firmware.bin`.

### Next on-device verification pass
1. Confirm warm-up countdown runs 10s and transitions cleanly to sync wait.
2. Confirm visible PPI sweep on low ambient brightness and no square/black patch artifacts around moving aircraft.
3. Confirm aircraft with missing location are not rendered and stale targets decay smoothly.
4. Confirm ring labels/range display remain aligned with configured max range.
