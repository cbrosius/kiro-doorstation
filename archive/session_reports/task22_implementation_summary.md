# Task 22: Certificate Expiration Warnings - Implementation Summary

## Overview
Implemented certificate expiration warnings on the dashboard to alert users when their TLS certificate is expiring or has expired. The implementation provides three levels of warnings based on the urgency of the situation.

## Changes Made

### 1. HTML Structure (main/index.html)

#### Added Warning Banners
Added three warning banner components to the dashboard section, positioned right after the dashboard heading:

1. **Standard Warning Banner** (`cert-expiry-warning-banner`)
   - Displayed when certificate expires within 30 days
   - Yellow/warning color scheme
   - Shows days until expiration
   - Provides "Manage Certificate" button

2. **Critical Warning Banner** (`cert-expiry-critical-banner`)
   - Displayed when certificate expires within 7 days
   - Red/danger color scheme with pulsing animation
   - Emphasizes urgency with "Critical" label
   - Provides two action buttons:
     - "Manage Certificate Now" - navigates to security section
     - "Quick Generate New Certificate" - generates certificate immediately

3. **Expired Certificate Banner** (`cert-expired-banner`)
   - Displayed when certificate has already expired
   - Red/danger color scheme with pulsing animation
   - Shows error state with "Certificate Expired!" message
   - Provides two action buttons:
     - "Generate New Certificate" - quick generation
     - "Certificate Settings" - navigate to security section

### 2. CSS Animations (main/index.html)

Added pulse animation for critical and expired warnings:
```css
@keyframes pulse {
  0%, 100% {
    opacity: 1;
    transform: scale(1);
  }
  50% {
    opacity: 0.7;
    transform: scale(1.1);
  }
}
```

### 3. JavaScript Functions (main/index.html)

#### `checkCertificateExpiration()`
- Called during dashboard initialization
- Fetches certificate information from `/api/cert/info` endpoint
- Evaluates expiration status and displays appropriate warning banner
- Logic:
  - If expired: show expired banner
  - If < 7 days: show critical warning banner
  - If < 30 days: show standard warning banner
  - Otherwise: hide all banners

#### `generateSelfSignedCertificateQuick()`
- Quick action function for generating a new self-signed certificate
- Prompts user for confirmation
- Calls `/api/cert/generate` endpoint with default parameters
- Reloads page after 3 seconds to apply new certificate

#### `scrollToElement(elementId)`
- Helper function to scroll to a specific element
- Adds visual highlight effect to the target element
- Used when navigating from warning banners to certificate management section

#### Modified `initDashboard()`
- Added call to `checkCertificateExpiration()` to check certificate status on dashboard load

## Requirements Addressed

This implementation addresses the following requirements from the design document:

- **Requirement 11.1**: Display warning on dashboard when certificate expires within 30 days ✓
- **Requirement 11.2**: Display critical warning when certificate expires within 7 days ✓
- **Requirement 11.3**: Display error when certificate has expired ✓
- **Requirement 11.4**: Offer option to upload new certificate (via navigation to security section) ✓
- **Requirement 11.5**: Offer option to generate new self-signed certificate ✓

## User Experience

### Warning Levels

1. **30-Day Warning** (Standard)
   - Non-intrusive yellow banner
   - Informative message about upcoming expiration
   - Single action button to manage certificate

2. **7-Day Warning** (Critical)
   - Prominent red banner with pulsing icon
   - Urgent messaging emphasizing immediate action needed
   - Two action buttons for quick resolution

3. **Expired Certificate** (Error)
   - Highly visible red banner with pulsing icon
   - Clear error state messaging
   - Two action buttons prioritizing quick certificate generation

### User Actions

Users can take the following actions from the warning banners:

1. **Navigate to Security Section**: Opens the certificate management interface where users can:
   - View detailed certificate information
   - Upload custom certificates
   - Generate new self-signed certificates
   - Download current certificate

2. **Quick Generate Certificate**: Immediately generates a new self-signed certificate without navigating away from the dashboard

## API Integration

The implementation uses the existing `/api/cert/info` endpoint which returns:
```json
{
  "exists": true,
  "is_self_signed": true,
  "common_name": "doorstation.local",
  "issuer": "doorstation.local",
  "not_before": "2024-01-01 00:00:00",
  "not_after": "2034-01-01 00:00:00",
  "days_until_expiry": 3650,
  "is_expired": false,
  "is_expiring_soon": false
}
```

## Testing Recommendations

To test this implementation:

1. **Test 30-Day Warning**:
   - Generate a certificate with 25 days validity
   - Load dashboard and verify yellow warning banner appears
   - Verify message shows correct number of days

2. **Test 7-Day Warning**:
   - Generate a certificate with 5 days validity
   - Load dashboard and verify red critical banner appears
   - Verify pulsing animation is working
   - Test both action buttons

3. **Test Expired Certificate**:
   - Generate a certificate with past expiration date (or wait for expiration)
   - Load dashboard and verify expired banner appears
   - Test quick certificate generation

4. **Test No Warning**:
   - Generate a certificate with > 30 days validity
   - Load dashboard and verify no warning banners appear

5. **Test Navigation**:
   - Click "Manage Certificate" buttons
   - Verify navigation to security section
   - Verify scroll and highlight effect on certificate panel

## Files Modified

- `main/index.html`: Added warning banners, CSS animation, and JavaScript functions

## Dependencies

- Existing certificate management API endpoints (`/api/cert/info`, `/api/cert/generate`)
- Existing certificate manager module (`cert_manager.c/h`)
- Existing navigation and UI helper functions

## Notes

- The implementation gracefully handles cases where certificate information is not available (silently fails with console warning)
- Warning banners are checked on every dashboard load, ensuring users see current status
- The quick certificate generation feature provides a convenient way to resolve expiration issues without navigating away from the dashboard
- Visual design follows the existing UI patterns and color scheme
- Accessibility considerations: warning banners use semantic HTML and appropriate color contrast
