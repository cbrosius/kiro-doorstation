# Requirements Document

## Introduction

This document specifies the requirements for refactoring the ESP32 SIP Door Station web interface from a single-page vertical layout into a modern tabbed interface with persistent status indicators. The refactoring aims to create a clean, intuitive, and scalable user interface that can accommodate future feature additions without requiring complete restructuring.

## Glossary

- **Web_Interface**: The HTML-based user interface served by the ESP32 device for configuration and monitoring
- **Status_Indicators**: Real-time display elements showing WiFi, SIP connection, Hardware-Events like Button-Presses or Door-Opener and Light status
- **Tab_Navigation**: A horizontal navigation system allowing users to switch between different configuration sections
- **Dashboard_Tab**: The primary view showing system overview and quick actions
- **Configuration_Section**: A logical grouping of related settings (SIP, Network, Hardware, etc.)
- **Responsive_Design**: Interface layout that adapts to different screen sizes (desktop, tablet, mobile)
- **Persistent_Element**: UI component that remains visible regardless of active tab

## Requirements

### Requirement 1

**User Story:** As a system administrator, I want to see WiFi, SIP connection, and hardware event status at all times, so that I can monitor system health and activity while configuring different settings

#### Acceptance Criteria

1. WHEN THE Web_Interface loads, THE Web_Interface SHALL display WiFi status, SIP status, and hardware event indicators in a fixed header area
2. WHILE the user navigates between tabs, THE Web_Interface SHALL maintain visibility of the status indicators
3. THE Web_Interface SHALL update status indicators every 10 seconds without user interaction
4. THE Web_Interface SHALL apply color coding to status indicators (green for connected, red for disconnected, yellow for connecting)
5. THE Web_Interface SHALL display hardware event indicators for button presses, door opener activation, and light status
6. WHEN a status changes from disconnected to connected, THE Web_Interface SHALL display a success notification
7. WHEN a hardware event occurs (button press, door opener, light toggle), THE Web_Interface SHALL update the corresponding indicator within 1 second

### Requirement 2

**User Story:** As a user, I want to navigate between different configuration sections using tabs, so that I can quickly access specific settings without scrolling through a long page

#### Acceptance Criteria

1. THE Web_Interface SHALL provide a horizontal tab navigation bar below the status indicators
2. THE Web_Interface SHALL include the following tabs with defined content:
   - Dashboard: System overview, quick actions, recent activity
   - SIP Settings: SIP server configuration, targets, authentication, connection management
   - Network Settings: WiFi configuration, network scanning, NTP time synchronization
   - Hardware Settings: GPIO pin assignments, DTMF control codes, relay configuration
   - Security Settings: DTMF PIN protection, timeout settings, access control
   - System Logs: SIP connection logs, DTMF security logs with filtering and auto-refresh
3. WHEN a user clicks a tab, THE Web_Interface SHALL display the corresponding content section within 100 milliseconds
4. THE Web_Interface SHALL highlight the currently active tab with distinct visual styling
5. THE Web_Interface SHALL persist the last selected tab in browser session storage
6. THE Web_Interface SHALL support adding new tabs without modifying the core tab switching logic

### Requirement 3

**User Story:** As a user, I want a Dashboard section that provides an overview of system status and quick actions, so that I can perform common tasks without navigating to detailed settings

#### Acceptance Criteria

1. THE Web_Interface SHALL display system information (uptime, free heap, IP address, firmware version) on the Dashboard section
2. THE Web_Interface SHALL provide quick action buttons for system restart, factory reset, OTA update, and SIP connect/disconnect on the Dashboard section
3. THE Web_Interface SHALL display NTP sync status and current time on the Dashboard section
4. THE Web_Interface SHALL show recent activity summary (last 10 log entries) on the Dashboard section
5. THE Web_Interface SHALL update Dashboard information every 10 seconds
6. WHEN the factory reset button is clicked, THE Web_Interface SHALL display a confirmation dialog requiring explicit user confirmation
7. THE Web_Interface SHALL warn the user that factory reset will erase all configuration and require a system restart

### Requirement 3.1

**User Story:** As a system administrator, I want to update the firmware over-the-air, so that I can deploy updates without physical access to the device

#### Acceptance Criteria

1. THE Web_Interface SHALL provide an OTA Update section in the sidebar menu
2. THE Web_Interface SHALL display current firmware version and build date
3. THE Web_Interface SHALL provide a file upload interface for firmware binary files (.bin)
4. THE Web_Interface SHALL validate firmware file format before upload
5. THE Web_Interface SHALL display upload progress with percentage indicator
6. WHEN firmware upload is complete, THE Web_Interface SHALL display a confirmation dialog before applying the update
7. THE Web_Interface SHALL warn the user that the device will restart during the update process
8. THE Web_Interface SHALL display update status (uploading, verifying, applying, complete, failed)
9. IF the update fails, THE Web_Interface SHALL display an error message and retain the current firmware
10. THE Web_Interface SHALL provide an option to check for updates from a URL (optional future feature)
11. THE Web_Interface SHALL display a warning if the device loses power during update
12. THE Web_Interface SHALL show estimated time remaining during update process

### Requirement 4

**User Story:** As a system administrator, I want all SIP-related settings grouped in a dedicated tab, so that I can configure SIP parameters in one logical location

#### Acceptance Criteria

1. THE Web_Interface SHALL provide a SIP Settings tab containing SIP server, username, password, and target configuration fields
2. THE Web_Interface SHALL include test call buttons for each SIP target within the SIP Settings tab
3. THE Web_Interface SHALL provide SIP connection management buttons (connect, disconnect, test configuration) in the SIP Settings tab
4. THE Web_Interface SHALL validate SIP configuration fields before allowing form submission
5. WHEN SIP configuration is saved successfully, THE Web_Interface SHALL display a success notification and update the SIP status indicator

### Requirement 5

**User Story:** As a user, I want all network-related settings grouped in a dedicated section, so that I can manage WiFi, IP configuration, and NTP settings in one place

#### Acceptance Criteria

1. THE Web_Interface SHALL provide a Network Settings section containing WiFi, IP configuration, and NTP subsections
2. THE Web_Interface SHALL include a WiFi network scanner with selectable network list in the Network Settings section
3. THE Web_Interface SHALL provide IP configuration options with DHCP/Static toggle
4. WHEN static IP is selected, THE Web_Interface SHALL display input fields for IP address, subnet mask, gateway, and DNS servers
5. THE Web_Interface SHALL validate IP address format before allowing form submission
6. THE Web_Interface SHALL display current IP configuration (DHCP or Static) with active values
7. THE Web_Interface SHALL display NTP server and timezone configuration fields in the Network Settings section
8. THE Web_Interface SHALL provide a force sync button for NTP time synchronization in the Network Settings section
9. WHEN network configuration is saved successfully, THE Web_Interface SHALL display a success notification

### Requirement 6

**User Story:** As a system administrator, I want hardware and GPIO settings grouped in a dedicated section, so that I can view hardware information and configure hardware behavior

#### Acceptance Criteria

1. THE Web_Interface SHALL provide a Hardware Settings section displaying hardware information:
   - ESP32-S3 variant/model (e.g., ESP32-S3-WROOM-1)
   - Chip revision
   - Flash size (total and available)
   - PSRAM size (if available)
   - Free heap memory (current and minimum)
2. THE Web_Interface SHALL display GPIO pin assignments with current state indicators
3. THE Web_Interface SHALL show DTMF control codes and their associated actions in the Hardware Settings section
4. THE Web_Interface SHALL display current doorbell button assignments (Bell 1, Bell 2) in the Hardware Settings section
5. THE Web_Interface SHALL show relay assignments (door opener, light control) with current state in the Hardware Settings section
6. THE Web_Interface SHALL include testing information for the BOOT button in the Hardware Settings section
7. THE Web_Interface SHALL provide a foundation for future button-to-action mapping configuration
8. THE Web_Interface SHALL update memory information every 10 seconds

### Requirement 7

**User Story:** As a security-conscious administrator, I want DTMF security settings grouped in a dedicated tab, so that I can configure PIN protection and access control

#### Acceptance Criteria

1. THE Web_Interface SHALL provide a Security Settings tab containing DTMF PIN configuration
2. THE Web_Interface SHALL include PIN enable/disable toggle, PIN code input, timeout, and max attempts fields in the Security Settings tab
3. THE Web_Interface SHALL validate PIN code format (1-8 digits) before allowing form submission
4. THE Web_Interface SHALL display current security configuration status in the Security Settings tab
5. WHEN security configuration is saved successfully, THE Web_Interface SHALL display a success notification

### Requirement 8

**User Story:** As a system administrator, I want to view SIP and DTMF logs in a dedicated section, so that I can troubleshoot issues without cluttering the configuration interface

#### Acceptance Criteria

1. THE Web_Interface SHALL provide a System Logs section containing SIP connection logs and DTMF security logs
2. THE Web_Interface SHALL display logs in separate, scrollable sections within the System Logs section
3. THE Web_Interface SHALL provide auto-refresh toggle and manual refresh buttons for each log section
4. THE Web_Interface SHALL include clear display buttons for each log section
5. THE Web_Interface SHALL apply color coding to log entries based on type (error, info, sent, received)

### Requirement 8.1

**User Story:** As an installer or integrator, I want to test hardware components before mounting the device, so that I can verify functionality during installation and setup

#### Acceptance Criteria

1. THE Web_Interface SHALL provide a Hardware Testing section in the sidebar menu
2. THE Web_Interface SHALL include test buttons for doorbell inputs:
   - Simulate Doorbell Button 1 press
   - Simulate Doorbell Button 2 press
3. THE Web_Interface SHALL include test controls for relay outputs:
   - Trigger door opener relay with configurable duration (1-10 seconds)
   - Toggle light relay on/off
4. THE Web_Interface SHALL display real-time feedback showing the result of each test action
5. THE Web_Interface SHALL include safety warnings for relay testing (e.g., "Door opener will physically activate")
6. THE Web_Interface SHALL show the current state of each relay (active/inactive) with visual indicators

### Requirement 8.2

**User Story:** As a user, I want to access documentation and help information within the web interface, so that I can understand features and troubleshoot issues without external resources

#### Acceptance Criteria

1. THE Web_Interface SHALL provide a Documentation section in the sidebar menu
2. THE Web_Interface SHALL include the following documentation subsections:
   - Quick Start Guide (initial setup steps)
   - Hardware Reference (GPIO pinout, wiring diagrams, specifications)
   - SIP Configuration Guide (server setup, authentication, troubleshooting)
   - DTMF Commands Reference (control codes and their functions)
   - Network Configuration Guide (WiFi, static IP, NTP setup)
   - API Reference (REST endpoints for integration)
   - Troubleshooting Guide (common issues and solutions)
   - FAQ (frequently asked questions)
3. THE Web_Interface SHALL provide a table of contents with anchor links for easy navigation
4. THE Web_Interface SHALL include collapsible sections to organize content hierarchically
5. THE Web_Interface SHALL display firmware version and build date in the documentation
6. THE Web_Interface SHALL include example configurations with copy-to-clipboard buttons
7. THE Web_Interface SHALL provide links to external resources (GitHub, support forum, etc.)
8. THE Web_Interface SHALL include a "Report Issue" button that opens a pre-filled issue template
9. THE Web_Interface SHALL support searching within documentation content
10. THE Web_Interface SHALL display documentation in a readable format with syntax highlighting for code examples

### Requirement 9

**User Story:** As a mobile user, I want the web interface to work well on my smartphone, so that I can configure and monitor the system from any device

#### Acceptance Criteria

1. THE Web_Interface SHALL convert the sidebar to a collapsible hamburger menu on screens smaller than 768 pixels wide
2. THE Web_Interface SHALL maintain full functionality on mobile devices
3. THE Web_Interface SHALL use touch-friendly button sizes (minimum 44x44 pixels) on mobile devices
4. THE Web_Interface SHALL display status indicators in a grid layout that adapts to screen width
5. THE Web_Interface SHALL ensure all form inputs are easily accessible on mobile keyboards
6. THE Web_Interface SHALL support touch gestures (swipe to close sidebar on mobile)
7. THE Web_Interface SHALL prevent zoom on form input focus (viewport meta tag configuration)
8. THE Web_Interface SHALL use appropriate input types (tel, email, url) for better mobile keyboard experience

### Requirement 10

**User Story:** As a developer, I want the web interface code to be modular and maintainable, so that future features can be added without major refactoring

#### Acceptance Criteria

1. THE Web_Interface SHALL be implemented as a single HTML file with embedded CSS and JavaScript to minimize ESP32-S3 memory usage
2. THE Web_Interface SHALL use client-side navigation (show/hide content sections) to avoid page reloads and reduce server load
3. THE Web_Interface SHALL separate CSS styles into logical sections (layout, sidebar, forms, status, logs) within the single file
4. THE Web_Interface SHALL organize JavaScript functions by feature area (SIP, WiFi, NTP, DTMF, system) with clear code comments
5. THE Web_Interface SHALL use a consistent naming convention for HTML element IDs and CSS classes
6. THE Web_Interface SHALL implement a reusable navigation switching mechanism that supports adding new menu items
7. THE Web_Interface SHALL minimize HTTP requests by embedding all resources (no external CSS/JS libraries)
8. THE Web_Interface SHALL compress well with gzip to reduce transmission size from ESP32-S3
9. THE Web_Interface SHALL document all API endpoints used by the interface in code comments
10. THE Web_Interface SHALL include a favicon using an embedded data URI or inline SVG to provide visual identification in browser tabs and bookmarks

### Requirement 11

**User Story:** As a user, I want to switch between light and dark display modes, so that I can use the interface comfortably in different lighting conditions

#### Acceptance Criteria

1. THE Web_Interface SHALL provide a theme toggle control in the header area
2. THE Web_Interface SHALL support both light mode and dark mode color schemes
3. THE Web_Interface SHALL persist the user's theme preference in browser local storage
4. THE Web_Interface SHALL detect the user's system theme preference on first load (prefers-color-scheme media query)
5. WHEN the user toggles the theme, THE Web_Interface SHALL apply the new theme to all interface elements within 100 milliseconds
6. THE Web_Interface SHALL use appropriate contrast ratios in both themes to ensure readability (WCAG AA compliance minimum)
7. THE Web_Interface SHALL apply theme colors to status indicators, forms, buttons, logs, and all UI components

### Requirement 11.1

**User Story:** As a user, I want modern UI interactions and feedback, so that the interface feels responsive and professional

#### Acceptance Criteria

1. THE Web_Interface SHALL display loading spinners when fetching data from API endpoints
2. THE Web_Interface SHALL show visual feedback (button state changes) when buttons are clicked
3. THE Web_Interface SHALL display confirmation dialogs for destructive actions (restart system, disconnect SIP)
4. THE Web_Interface SHALL provide keyboard shortcuts for common actions (e.g., Ctrl+S to save configuration)
5. THE Web_Interface SHALL show form validation errors inline next to the relevant input fields
6. THE Web_Interface SHALL display success/error toast notifications with appropriate icons
7. THE Web_Interface SHALL use smooth transitions when switching between sidebar menu items (fade in/out)
8. THE Web_Interface SHALL indicate unsaved changes with a visual indicator (e.g., asterisk or dot on menu item)
9. THE Web_Interface SHALL provide a "Save" button that is disabled when no changes have been made
10. THE Web_Interface SHALL show connection status in real-time (e.g., "Connecting...", "Connected", "Failed")

### Requirement 11.2

**User Story:** As a user, I want to quickly find information in logs, so that I can troubleshoot issues efficiently

#### Acceptance Criteria

1. THE Web_Interface SHALL provide a search/filter input field in the System Logs section
2. WHEN a user types in the search field, THE Web_Interface SHALL filter log entries in real-time to show only matching entries
3. THE Web_Interface SHALL highlight search terms within filtered log entries
4. THE Web_Interface SHALL provide filter buttons for log types (error, info, sent, received)
5. THE Web_Interface SHALL display the count of filtered entries (e.g., "Showing 15 of 234 entries")
6. THE Web_Interface SHALL allow exporting logs to a text file for offline analysis
7. THE Web_Interface SHALL provide a "Jump to bottom" button for quick navigation in long logs

### Requirement 11.3

**User Story:** As a user, I want to quickly find specific settings across all configuration sections, so that I don't need to remember which menu contains each setting

#### Acceptance Criteria

1. THE Web_Interface SHALL provide a global search input field in the header or sidebar
2. WHEN a user types in the global search field, THE Web_Interface SHALL search across all setting labels, descriptions, and menu items
3. THE Web_Interface SHALL display search results as a dropdown list showing the setting name and which section it belongs to
4. WHEN a user clicks a search result, THE Web_Interface SHALL navigate to the appropriate section and highlight the matching setting
5. THE Web_Interface SHALL support keyboard navigation (arrow keys, Enter) in search results
6. THE Web_Interface SHALL show "No results found" message when search yields no matches
7. THE Web_Interface SHALL provide a keyboard shortcut (e.g., Ctrl+K or /) to focus the search field

### Requirement 11.4

**User Story:** As a system administrator, I want to backup and restore my configuration, so that I can migrate settings between devices or recover from configuration errors

#### Acceptance Criteria

1. THE Web_Interface SHALL provide a "Backup Configuration" button in the Dashboard or System section
2. WHEN the backup button is clicked, THE Web_Interface SHALL download a JSON file containing all current configuration settings
3. THE Web_Interface SHALL include a timestamp and firmware version in the backup JSON file
4. THE Web_Interface SHALL provide a "Restore Configuration" button with file upload capability
5. WHEN a backup file is uploaded, THE Web_Interface SHALL validate the JSON format and display a preview of settings to be restored
6. THE Web_Interface SHALL display a confirmation dialog before applying restored configuration
7. WHEN configuration is restored successfully, THE Web_Interface SHALL display a success notification and prompt for system restart
8. IF the backup file is invalid or corrupted, THE Web_Interface SHALL display an error message without modifying current configuration
9. THE Web_Interface SHALL exclude sensitive information (passwords) from backup files or encrypt them
10. THE Web_Interface SHALL provide an option to include or exclude passwords in the backup

### Requirement 11.5

**User Story:** As a system administrator, I want to receive automated reports and backups via email, so that I can monitor system health and maintain configuration backups without manual intervention

#### Acceptance Criteria

1. THE Web_Interface SHALL provide an Email Reports section in the sidebar menu
2. THE Web_Interface SHALL include SMTP server configuration fields (server, port, username, password, sender email)
3. THE Web_Interface SHALL provide a recipient email address field for report delivery
4. THE Web_Interface SHALL include a "Test Email" button to verify SMTP configuration
5. THE Web_Interface SHALL provide scheduling options for automated reports:
   - Daily reports (with time selection)
   - Weekly reports (with day and time selection)
   - Monthly reports (with day of month and time selection)
6. THE Web_Interface SHALL allow users to select report content:
   - System status summary (uptime, memory, connectivity)
   - Activity logs (SIP calls, DTMF events, button presses)
   - Configuration backup (JSON file attachment)
7. THE Web_Interface SHALL display the last report send time and status (success/failed)
8. THE Web_Interface SHALL provide a "Send Report Now" button for manual report generation
9. THE Web_Interface SHALL validate email addresses and SMTP settings before saving
10. THE Web_Interface SHALL display error messages if email delivery fails with troubleshooting hints

### Requirement 12

**User Story:** As a product owner, I want the interface architecture to accommodate planned future features, so that we can add new capabilities without restructuring the entire interface

#### Acceptance Criteria

1. THE Web_Interface SHALL reserve space in the sidebar navigation for future feature menu items including:
   - Authentication & Certificates: Web interface login, TLS/SSL certificate management
   - Access Control: IP whitelist/blacklist configuration, firewall rules
   - Home Automation: MQTT broker configuration, topic mapping, event publishing
   - Video Settings: Camera configuration, video codec settings, resolution, frame rate
   - Video Preview: Live video stream display, snapshot capture, recording settings
   - Button Actions: Configurable button-to-action mapping (e.g., Button 1 → Call Target 1, Button 2 → Call Target 2 + Turn on Light)
2. THE Web_Interface SHALL support dynamic menu item addition through a configuration-based approach
3. THE Web_Interface SHALL implement a modular API client pattern that allows adding new endpoint handlers for authentication, access control, and MQTT features
4. WHERE future features require new menu items, THE Web_Interface SHALL allow menu insertion without modifying existing navigation logic
5. THE Web_Interface SHALL use a menu ordering system that allows logical grouping (e.g., security-related items together)
