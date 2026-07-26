# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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