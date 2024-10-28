# Changelog

## v1.0.1
- [menu.c] Revamped menu – commands are structured better, grouped logically (scanning, beacon spam, attacks, etc.)
- [menu.c] Made it easier to add new commands to the menu
- [menu.c] Cleaned up documentation on menu items and properties
- [settings] Centralized metadata for all settings
- [settings] Settings are now enum-based – better for validation
- [settings] Tightened up validation and error handling in settings
- [settings] Extensible settings structure via SettingMetadata
- [settings] Added Stop-on-Back feature for command interruption
- [settings] Added ESP reboot command in the settings menu
- [connectivity] Added ESP connection verification
- [connectivity] Clearer error messages on connectivity issues
- [connectivity] Automatic connectivity check before executing commands
- [connectivity] Error recovery in case of ESP issues, with return to the menu
- [ui] UI now builds off metadata for consistency
- [ui] Better type safety in UI operations
- [ui] Simplified view switching across the interface
- [ui] Improved error message display
- [code organization] Separated out concerns for cleaner structure
- [code organization] Removed redundant functions
- [code organization] Better type definitions for clarity
- [code organization] More consistent error handling throughout
- [code organization] Reorganized code for better readability

## v1.0.2
- [ui] Added confirmation dialogs for WebUI-dependent features
- [ui] Improved settings menu organization with dedicated actions submenu
- [settings] Added NVS clearing with confirmation dialog
- [settings] Added log clearing functionality
- [settings] Improved robustness of settings storage and loading
- [menu] Added contextual help for WebUI configuration requirements
- [menu] Improved command execution safety with confirmation dialogs
- [memory] Enhanced memory management and cleanup processes
- [error handling] Added NULL checks throughout for better stability
- [code organization] Restructured menu command definitions for easier maintenance
- [ui] Improved view navigation and state management
- [cleanup] Added proper cleanup sequences for all components
- [stability] Added safeguards against potential furi_check failures
- [memory] Added proper initialization of all structure fields

## v1.0.3

### View & Display Improvements 
- Improved display and structure of confirmation view
 - Shortened border width around dialog
 - Improved text alignment
- Added > to Configuration submenu
### Settings & Confirmation Additions
- Added confirmation view to Clear Log Files setting
 - Added warning about permanent action
 - Requires confirmation before executing 
 - Matches WebUI confirmation pattern

- Made confirmation view exit on back press
 - Fixed callback context handling
 - Updated view state tracking
 - Added view state restoration
 - Fixed memory cleanup
 - Added callback cleanup
 - Fixed transitions between dialogs

### Memory Management  
- Added context cleanup in dialogs
- Fixed context sharing between views
- Added null checks 
- Fixed memory leaks
- Added state tracking


## v1.0.4

### Improvements
- Refined confirmation view line breaks for readability
- Improved ESP Connectity check to decrease false negatives
- Addded optional filtering to UART output to improve readability (BETA)


## TODO
- Add Help menu
- Add view log from start/end configuration setting
- Settings get sent to board and get pulled from on ESP Check or Init
- Improve directory organisation