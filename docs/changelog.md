# Changelog

## v1.0.1
- Revamped menu structure with logical grouping (scanning, beacon spam, attacks, etc.)
- Simplified command addition and cleaned up documentation in `menu.c`
- Centralized and enum-based settings metadata for improved validation and extensibility
- Enhanced settings with Stop-on-Back feature and ESP reboot command
- Added ESP connection verification and clearer error messaging
- Enabled automatic connectivity check and error recovery for ESP issues
- Unified UI with metadata-driven consistency and better type safety
- Simplified UI view switching and improved error display
- Refined code organization, separating concerns, removing redundancy, and standardizing error handling

## v1.0.2
- Added confirmation dialogs for WebUI-dependent features in the UI
- Improved settings menu with actions submenu, NVS clearing, and log clearing
- Enhanced memory management and improved settings storage/loading robustness
- Added contextual help for WebUI configuration and confirmation dialogs for command safety
- Improved view navigation, state management, and memory cleanup processes
- Added safeguards against `furi_check` failures with NULL checks and memory initialization

## v1.0.3
- Enhanced confirmation view structure and readability with better text alignment
- Added confirmation for "Clear Log Files" with a permanent action warning
- Enabled back press exit on confirmation views with callback context handling
- Improved memory management with context cleanup, view state tracking, and transition fixes
- Added NULL checks, fixed memory leaks, and added state tracking for dialogs

## v1.0.4
- Refined confirmation view line breaks for readability
- Improved ESP Connectity check to decrease false negatives
- Added optional filtering to UART output to improve readability (BETA)
- Added 'App Info' Button in Settings
- Misc Changes (mostly to UI)


## v1.0.5
- Commands will silently fail if UART isn't working rather than crashing
- Fixed double-free memory issue by removing stream buffer cleanup from the worker thread
- Reorganized initialization order
- UART initialization happens in background
- Serial operations don't block app startup
- Optimized storage initialization by deferring file operations until needed
- Improved directory creation efficiency in storage handling


## v1.0.6
- Replaced 'Info' command in ESP Check with 'Stop'
- Slightly improved optional UART filtering
- Memory safety improvements.
- Improved Clear Logs to be faster and more efficient 
- Added details view to each command accessable with hold of center button. (Like BLE Spam)
- Made ESP Not Connected screen more helpful with prompts to reboot/reflash if issues persist.
- Renamed CONF menu option to SET to better align with actual Settings menu since it's header is "Settings" and there is a configuration submenu
- Replaced textbox for ESP Connection Check with scrollable Confirmation View

## TODO

- Replaced select a utility text with prompt to show NEW Help Menu
- Add view log from start/end configuration setting
- Settings get sent to board and get pulled from on ESP Check or Init
- IMPROVE optional filtering to UART output
- Improve directory organisation!!!!!!!!!!!!!!!!!!!!!!!!!!!!