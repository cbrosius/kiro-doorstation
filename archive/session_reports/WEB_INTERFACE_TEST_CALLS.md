# Web Interface - Test Call Feature

## Visual Layout

### SIP Configuration Section

```
┌─────────────────────────────────────────────────────────────┐
│ SIP Configuration                                           │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│ SIP-Target1 (Apartment 1, e.g., sip:apartment1@domain.com):│
│ ┌──────────────────────────────────────┐ ┌──────────────┐ │
│ │ sip:apartment1@domain.com            │ │  Test Call   │ │
│ └──────────────────────────────────────┘ └──────────────┘ │
│                                                             │
│ SIP-Target2 (Apartment 2, e.g., sip:apartment2@domain.com):│
│ ┌──────────────────────────────────────┐ ┌──────────────┐ │
│ │ sip:apartment2@domain.com            │ │  Test Call   │ │
│ └──────────────────────────────────────┘ └──────────────┘ │
│                                                             │
│ SIP Server (e.g., sip.domain.com):                         │
│ ┌──────────────────────────────────────────────────────┐  │
│ │ sip.example.com                                       │  │
│ └──────────────────────────────────────────────────────┘  │
│                                                             │
│ Username:                                                   │
│ ┌──────────────────────────────────────────────────────┐  │
│ │ doorbell                                              │  │
│ └──────────────────────────────────────────────────────┘  │
│                                                             │
│ Password:                                                   │
│ ┌──────────────────────────────────────────────────────┐  │
│ │ ••••••••                                              │  │
│ └──────────────────────────────────────────────────────┘  │
│                                                             │
│ ┌──────────────┐ ┌──────────────┐ ┌──────────┐ ┌────────┐│
│ │Save Config   │ │Test Config   │ │Connect   │ │Disconnect││
│ └──────────────┘ └──────────────┘ └──────────┘ └────────┘│
└─────────────────────────────────────────────────────────────┘
```

## Button Behavior

### Test Call Buttons
- **Location**: Inline with each target input field
- **Style**: Secondary button (gray background)
- **States**:
  - Normal: "Test Call"
  - During call: "Calling..." (disabled)
  - After call: Returns to "Test Call" (enabled)

### User Interaction Flow

```
┌─────────────────┐
│ User enters     │
│ Target1 URI     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ User clicks     │
│ "Test Call"     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐      ┌──────────────────┐
│ Validation      │─────▶│ Show error toast │
│ - Empty?        │      │ if validation    │
│ - Registered?   │      │ fails            │
└────────┬────────┘      └──────────────────┘
         │
         │ Valid
         ▼
┌─────────────────┐
│ Button shows    │
│ "Calling..."    │
│ (disabled)      │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ API call to     │
│ /api/sip/testcall│
└────────┬────────┘
         │
         ▼
┌─────────────────┐      ┌──────────────────┐
│ SIP client      │─────▶│ Target device    │
│ initiates call  │      │ rings            │
└────────┬────────┘      └──────────────────┘
         │
         ▼
┌─────────────────┐
│ Success toast   │
│ shown           │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Button returns  │
│ to "Test Call"  │
│ (enabled)       │
└─────────────────┘
```

## Toast Notifications

### Success Messages
- ✅ "📞 Test call initiated to Target1"
- ✅ "📞 Test call initiated to Target2"

### Error Messages
- ❌ "Please enter SIP-Target1 first"
- ❌ "Please enter SIP-Target2 first"
- ❌ "Not registered to SIP server. Please connect first."
- ❌ "Error initiating test call"

## SIP Log Entries

When test call button is pressed, the following entries appear in the log:

```
[2024-10-16 14:23:45] [USER] 🔘 Test Call button pressed for Target1
[2024-10-16 14:23:45] [USER] Test call initiated to: sip:apartment1@domain.com
[2024-10-16 14:23:45] [INFO] Initiating call - State: CALLING
[2024-10-16 14:23:45] [SENT] INVITE message prepared
```

## Responsive Design

### Desktop View
- Target input and Test Call button side-by-side
- Full width for input field
- Fixed width for button

### Mobile View
- Same layout maintained
- Buttons wrap if screen too narrow
- Touch-friendly button size

## Accessibility

- Clear button labels
- Disabled state visually distinct
- Toast notifications for screen readers
- Keyboard navigation supported
- Focus indicators on buttons

## Color Scheme

### Buttons
- **Primary (Save Config)**: Green (#4CAF50)
- **Secondary (Test Call, Test Config)**: Gray (#6c757d)
- **Action (Connect)**: Green (#4CAF50)
- **Danger (Disconnect)**: Red (#dc3545)

### Toast Notifications
- **Success**: Green background (#28a745)
- **Error**: Red background (#dc3545)
- **Info**: Blue background (#17a2b8)

## Integration with Existing Features

### Works With
- ✅ SIP registration status
- ✅ SIP connection/disconnection
- ✅ SIP log viewer
- ✅ Auto-refresh log
- ✅ Configuration save/load

### Requires
- ✅ Active SIP registration
- ✅ Valid target configuration
- ✅ Network connectivity
- ✅ SIP server availability

## Example Usage Scenario

1. **Initial Setup**:
   ```
   User opens web interface
   → Sees empty target fields
   → Fills in Target1: sip:101@pbx.example.com
   → Fills in Target2: sip:102@pbx.example.com
   → Saves configuration
   ```

2. **Connection**:
   ```
   User clicks "Connect to SIP"
   → Status changes to "Registered"
   → Test Call buttons become usable
   ```

3. **Testing Target1**:
   ```
   User clicks "Test Call" next to Target1
   → Button shows "Calling..."
   → Toast: "📞 Test call initiated to Target1"
   → Extension 101 rings
   → User answers and hears silence (no audio yet)
   → User hangs up
   → Button returns to "Test Call"
   ```

4. **Testing Target2**:
   ```
   User clicks "Test Call" next to Target2
   → Extension 102 rings
   → Confirms Target2 works correctly
   ```

5. **Physical Test**:
   ```
   User presses physical doorbell button 1
   → Extension 101 rings (Target1)
   → Confirms doorbell → Target mapping
   ```
