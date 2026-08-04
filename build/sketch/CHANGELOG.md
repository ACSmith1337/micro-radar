#line 1 "/home/hermes/micro-radar/CHANGELOG.md"
# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.4] - 2026-07-27

### Added
- **Gold phosphor color scheme** (P4 amber) toggleable via web UI — warm gold rings, labels, scan line, and aircraft
- **Aircraft trail dots** — dotted track history behind moving aircraft (toggleable)
- **Squawk code alerts** — emergency squawk detection (7500, 7600, 7700) with flashing red text overlay
- **Display legend** in web UI explaining aircraft icons, colors, fade behavior, and squawk codes
- Variable fade rates — strong signals persist ~9.5s, weak signals ~5.5s

### Changed
- Beam illumination now sets aircraft to full brightness (scan line level) on contact, then decays
- Beam wedge reduced from 36° to 6° — minimal trailing shadow, no hidden aircraft
- Beam edge cleanup prevents outer ring artifacts
- Military aircraft color changed from red to orange (red reserved for squawk alerts)
- Green phosphor restored to original P1 values
- Range labels and cardinal directions brightened to near-scan-line brightness
- Gold profile: darker rings, no crosshairs, gold beam (less orange)
- Crosshairs dimmed on both profiles
- Aircraft glow radius reduced for better trail dot visibility

### Fixed
- Web portal character encoding (added UTF-8 charset, replaced non-ASCII characters)
- Amber palette corrected (previous values rendered as green, not amber)
- Aircraft trail dots now visible (proper brightness, draw order, and sizing)
- Beam overshoot artifacts eliminated at outer ring boundary

### Removed
- Removed problematic v1.1 and v1.2 releases that had incorrect beam fade behavior

## [1.3] - 2026-07-26

### Fixed
- Corrected beam fade direction to proper trailing edge effect
- Fixed aircraft icons: planes as triangles, helicopters as hollow circles with X, others as ?
- Made cardinal direction text bolder using double-draw technique
- Made range text brighter by using CLR_RING_BRIGHT instead of CLR_RING
- Improved aircraft persistence during readsb network failures
- Reduced HTTP timeout for faster failure detection

### Removed
- Removed problematic v1.1 and v1.2 releases that had incorrect beam fade behavior

## [1.2] - 2026-07-26

### Fixed
- Resolved issue where radar would get stuck on warmup screen by implementing a 10-second timeout
- Fixed beam fade direction to properly follow the scan line
- Improved performance and responsiveness by optimizing scan interval and decay timing
- Enhanced outer ring smoothness with improved bridge steps calculation

## [1.1] - 2026-07-26

### Added
- Enhanced realistic PPI effects with improved phosphor glow
- More pronounced aircraft glow effects for better visibility
- Enhanced phosphor trail gradient for authentic bloom effect
- Differentiated commercial aircraft color from scan line

### Changed
- Optimized brightness decay timing for more responsive fade
- Improved color palette for better visual distinction
- Enhanced scan line brightness for better visibility

## [1.0] - 2026-07-26

### Added
- Authentic PPI radar display with phosphor glow effects
- Smooth aircraft interpolation between data updates
- Color-coded aircraft (green for commercial, red for military)
- Web-based configuration interface
- Support for readsb/dump1090 aircraft.json feeds
- Hardware assembly instructions
- Deployment script for easy firmware flashing

### Changed
- Improved warmup sequence with flashing "RADAR WARMUP" text
- Removed hash marks at cardinal directions (0°, 90°, 180°, 270°)
- Adjusted N/S/E/W label sizing for better visibility
- Optimized grid drawing to preserve background elements during text flashing
- Enhanced beam fade behavior for authentic PPI experience

### Fixed
- Text overlap issues during warmup sequence
- Grid element disruption during flashing text
- Sync status message interference
- Direction indicator visibility

## [Unreleased]

### Added
- Support for additional ADS-B data sources
- Enhanced military aircraft detection algorithms
- Improved interpolation algorithms for smoother aircraft movement
- Additional display customization options