# Design Document: Tabbed Web Interface Refactoring

## Overview

This document describes the design for refactoring the ESP32 SIP Door Station web interface from a single-page vertical layout into a modern sidebar-based navigation system with persistent status indicators. The design follows Mockup B (Sidebar Navigation with Collapsible Status) and implements a single-page application (SPA) architecture optimized for ESP32-S3 hardware constraints.

### Design Goals

1. **Modern UX**: Sidebar navigation with collapsible menu, light/dark themes, smooth transitions
2. **Always-Visible Status**: Persistent status panel showing WiFi, SIP, and hardware events
3. **Scalability**: Architecture supports adding future features without restructuring
4. **Performance**: Single HTML file with embedded CSS/JS, optimized for ESP32-S3 memory constraints
5. **Accessibility**: WCAG AA compliance, keyboard navigation, mobile-responsive
6. **Maintainability**: Modular code organization, clear separation of concerns

## Architecture

### High-Level Structure

```
┌──────────────────────────────────────────────────────────────────┐
│  Header Bar (Fixed)                                              │
│  [☰ Menu] ESP32 SIP Door Station  [🔍 Search] [🌙 Theme] v1.0   │
├──────────┬───────────────────────────────────────────────────────┤
│          │  Status Panel (Collapsible)                           │
│  Sidebar │  ┌─ Status ──────────────────────────────────────┐   │
│  (Fixed) │  │ WiFi: ✓ Connected  │  SIP: ✓ Registered      │   │
│          │  │ Door: ○ Closed     │  Light: ○ Off           │   │
│ 🏠 Dash  │  └─────────────────────────────────────────────────┘   │
│ 📞 SIP   ├───────────────────────────────────────────────────────┤
│ 🌐 Net   │                                                       │
│ ⚙️ Hard  │  Main Content Area (Scrollable)                      │
│ 🔒 Sec   │                                                       │
│ 📋 Logs  │  (Content changes based on selected menu item)       │
│ 🧪 Test  │                                                       │
│ 📧 Email │                                                       │
│ 🔄 OTA   │                                                       │
│ 📖 Docs  │                                                       │
│          │                                                       │
└──────────┴───────────────────────────────────────────────────────┘
```

### Technology Stack

- **HTML5**: Semantic markup, single-file architecture
- **CSS3**: Custom properties for theming, Flexbox/Grid for layout, transitions
- **Vanilla JavaScript**: No external libraries, ES6+ features, modular functions
- **REST API**: JSON communication with ESP32 backend


## Components and Interfaces

### 1. Header Component

**Purpose**: Fixed top bar with branding, global search, and theme toggle

**Structure**:
```html
<header class="app-header">
  <button class="menu-toggle" aria-label="Toggle menu">☰</button>
  <h1 class="app-title">ESP32 SIP Door Station</h1>
  <div class="header-actions">
    <input type="search" class="global-search" placeholder="Search settings... (Ctrl+K)">
    <button class="theme-toggle" aria-label="Toggle theme">🌙</button>
    <span class="version">v1.0</span>
  </div>
</header>
```

**Functionality**:
- Menu toggle button (mobile: hamburger menu, desktop: collapse sidebar)
- Global search with keyboard shortcut (Ctrl+K or /)
- Theme toggle (light/dark mode)
- Version display

**CSS Variables**:
```css
--header-height: 60px;
--header-bg: var(--color-surface);
--header-shadow: 0 2px 4px rgba(0,0,0,0.1);
```


### 2. Sidebar Navigation Component

**Purpose**: Fixed left sidebar with menu items and icons

**Structure**:
```html
<nav class="sidebar" id="sidebar">
  <ul class="nav-menu">
    <li class="nav-item active" data-section="dashboard">
      <span class="nav-icon">🏠</span>
      <span class="nav-label">Dashboard</span>
    </li>
    <li class="nav-item" data-section="sip">
      <span class="nav-icon">📞</span>
      <span class="nav-label">SIP Settings</span>
      <span class="unsaved-indicator" hidden>●</span>
    </li>
    <li class="nav-item" data-section="network">
      <span class="nav-icon">🌐</span>
      <span class="nav-label">Network</span>
    </li>
    <li class="nav-item" data-section="hardware">
      <span class="nav-icon">⚙️</span>
      <span class="nav-label">Hardware</span>
    </li>
    <li class="nav-item" data-section="security">
      <span class="nav-icon">🔒</span>
      <span class="nav-label">Security</span>
    </li>
    <li class="nav-item" data-section="logs">
      <span class="nav-icon">📋</span>
      <span class="nav-label">System Logs</span>
    </li>
    <li class="nav-item" data-section="testing">
      <span class="nav-icon">🧪</span>
      <span class="nav-label">Hardware Testing</span>
    </li>
    <li class="nav-item" data-section="email">
      <span class="nav-icon">📧</span>
      <span class="nav-label">Email Reports</span>
    </li>
    <li class="nav-item" data-section="ota">
      <span class="nav-icon">🔄</span>
      <span class="nav-label">OTA Update</span>
    </li>
    <li class="nav-item" data-section="docs">
      <span class="nav-icon">📖</span>
      <span class="nav-label">Documentation</span>
    </li>
  </ul>
</nav>
```

**Functionality**:
- Click to navigate between sections
- Active state highlighting
- Unsaved changes indicator (dot)
- Collapsible on mobile (hamburger menu)
- Keyboard navigation (arrow keys, Enter)

**CSS Variables**:
```css
--sidebar-width: 240px;
--sidebar-collapsed-width: 60px;
--sidebar-bg: var(--color-surface);
--sidebar-item-hover: var(--color-hover);
--sidebar-item-active: var(--color-primary);
```


### 3. Status Panel Component

**Purpose**: Collapsible panel showing real-time system status

**Structure**:
```html
<div class="status-panel" id="status-panel">
  <button class="status-toggle" aria-label="Toggle status panel">
    <span>Status</span>
    <span class="toggle-icon">▼</span>
  </button>
  <div class="status-content">
    <div class="status-grid">
      <div class="status-card" id="wifi-status" data-status="connected">
        <span class="status-icon">📶</span>
        <div class="status-info">
          <span class="status-label">WiFi</span>
          <span class="status-value">Connected</span>
        </div>
      </div>
      <div class="status-card" id="sip-status" data-status="registered">
        <span class="status-icon">📞</span>
        <div class="status-info">
          <span class="status-label">SIP</span>
          <span class="status-value">Registered</span>
        </div>
      </div>
      <div class="status-card" id="door-status" data-status="closed">
        <span class="status-icon">🚪</span>
        <div class="status-info">
          <span class="status-label">Door</span>
          <span class="status-value">Closed</span>
        </div>
      </div>
      <div class="status-card" id="light-status" data-status="off">
        <span class="status-icon">💡</span>
        <div class="status-info">
          <span class="status-label">Light</span>
          <span class="status-value">Off</span>
        </div>
      </div>
    </div>
  </div>
</div>
```

**Functionality**:
- Collapsible/expandable (saves state in localStorage)
- Auto-refresh every 10 seconds
- Color-coded status indicators:
  - Green: connected/active
  - Red: disconnected/error
  - Yellow: connecting/warning
  - Gray: inactive/off
- Click to expand for more details

**Status Data Model**:
```javascript
{
  wifi: { status: 'connected', ip: '192.168.1.100', rssi: -45 },
  sip: { status: 'registered', state: 'IDLE', server: 'sip.domain.com' },
  door: { status: 'closed', lastActivation: timestamp },
  light: { status: 'off', lastToggle: timestamp }
}
```


### 4. Main Content Area Component

**Purpose**: Scrollable area displaying section-specific content

**Structure**:
```html
<main class="main-content" id="main-content">
  <!-- Dashboard Section -->
  <section class="content-section active" id="section-dashboard">
    <!-- Dashboard content -->
  </section>
  
  <!-- SIP Settings Section -->
  <section class="content-section" id="section-sip" hidden>
    <!-- SIP settings content -->
  </section>
  
  <!-- Other sections... -->
</main>
```

**Navigation Logic**:
```javascript
function navigateToSection(sectionId) {
  // Hide all sections
  document.querySelectorAll('.content-section').forEach(section => {
    section.hidden = true;
    section.classList.remove('active');
  });
  
  // Show selected section with fade-in
  const targetSection = document.getElementById(`section-${sectionId}`);
  targetSection.hidden = false;
  setTimeout(() => targetSection.classList.add('active'), 10);
  
  // Update sidebar active state
  document.querySelectorAll('.nav-item').forEach(item => {
    item.classList.toggle('active', item.dataset.section === sectionId);
  });
  
  // Save to sessionStorage
  sessionStorage.setItem('activeSection', sectionId);
}
```

**Section Transition**:
- Fade-in animation (200ms)
- Scroll to top on section change
- Preserve scroll position within section


### 5. Theme System

**Purpose**: Light/dark mode with system preference detection

**CSS Custom Properties**:
```css
:root {
  /* Light theme (default) */
  --color-primary: #4CAF50;
  --color-primary-dark: #45a049;
  --color-danger: #dc3545;
  --color-warning: #ffc107;
  --color-info: #17a2b8;
  
  --color-bg: #f4f4f4;
  --color-surface: #ffffff;
  --color-text: #333333;
  --color-text-secondary: #666666;
  --color-border: #dddddd;
  --color-hover: #f8f9fa;
  
  --shadow-sm: 0 1px 2px rgba(0,0,0,0.1);
  --shadow-md: 0 2px 4px rgba(0,0,0,0.1);
  --shadow-lg: 0 4px 8px rgba(0,0,0,0.15);
}

[data-theme="dark"] {
  /* Dark theme */
  --color-primary: #66bb6a;
  --color-primary-dark: #4caf50;
  --color-danger: #ef5350;
  --color-warning: #ffca28;
  --color-info: #29b6f6;
  
  --color-bg: #1e1e1e;
  --color-surface: #2d2d2d;
  --color-text: #e0e0e0;
  --color-text-secondary: #b0b0b0;
  --color-border: #404040;
  --color-hover: #3a3a3a;
  
  --shadow-sm: 0 1px 2px rgba(0,0,0,0.3);
  --shadow-md: 0 2px 4px rgba(0,0,0,0.3);
  --shadow-lg: 0 4px 8px rgba(0,0,0,0.4);
}
```

**Theme Toggle Logic**:
```javascript
function initTheme() {
  // Check localStorage first
  const savedTheme = localStorage.getItem('theme');
  if (savedTheme) {
    document.documentElement.setAttribute('data-theme', savedTheme);
    return;
  }
  
  // Check system preference
  if (window.matchMedia('(prefers-color-scheme: dark)').matches) {
    document.documentElement.setAttribute('data-theme', 'dark');
  }
}

function toggleTheme() {
  const currentTheme = document.documentElement.getAttribute('data-theme');
  const newTheme = currentTheme === 'dark' ? 'light' : 'dark';
  document.documentElement.setAttribute('data-theme', newTheme);
  localStorage.setItem('theme', newTheme);
  
  // Update toggle button icon
  updateThemeIcon(newTheme);
}
```


### 6. Global Search Component

**Purpose**: Search across all settings and navigate to results

**Structure**:
```html
<div class="search-container">
  <input type="search" 
         class="global-search" 
         id="global-search"
         placeholder="Search settings... (Ctrl+K)"
         autocomplete="off">
  <div class="search-results" id="search-results" hidden>
    <div class="search-result" data-section="sip" data-field="server">
      <span class="result-label">SIP Server</span>
      <span class="result-section">SIP Settings</span>
    </div>
    <!-- More results... -->
  </div>
</div>
```

**Search Index**:
```javascript
const searchIndex = [
  { label: 'SIP Server', section: 'sip', field: 'server', keywords: ['sip', 'server', 'domain'] },
  { label: 'WiFi SSID', section: 'network', field: 'ssid', keywords: ['wifi', 'network', 'ssid'] },
  { label: 'Static IP', section: 'network', field: 'static-ip', keywords: ['ip', 'static', 'address'] },
  { label: 'DTMF PIN', section: 'security', field: 'pin', keywords: ['dtmf', 'pin', 'security'] },
  // ... more entries
];

function performSearch(query) {
  const results = searchIndex.filter(item => {
    const searchText = query.toLowerCase();
    return item.label.toLowerCase().includes(searchText) ||
           item.keywords.some(kw => kw.includes(searchText));
  });
  return results.slice(0, 10); // Limit to 10 results
}
```

**Navigation to Result**:
```javascript
function navigateToSearchResult(section, field) {
  // Navigate to section
  navigateToSection(section);
  
  // Wait for section to render
  setTimeout(() => {
    const element = document.getElementById(field);
    if (element) {
      element.scrollIntoView({ behavior: 'smooth', block: 'center' });
      element.classList.add('highlight');
      setTimeout(() => element.classList.remove('highlight'), 2000);
    }
  }, 300);
}
```


### 7. Form Handling and Validation

**Purpose**: Consistent form behavior across all sections

**Form Structure**:
```html
<form class="config-form" id="sip-form" data-api="/api/sip/config">
  <div class="form-group">
    <label for="sip-server">SIP Server *</label>
    <input type="text" 
           id="sip-server" 
           name="server" 
           required
           pattern="^[a-zA-Z0-9.-]+$"
           data-error="Please enter a valid server address">
    <span class="error-message" hidden></span>
  </div>
  
  <div class="form-actions">
    <button type="submit" class="btn btn-primary" disabled>
      <span class="btn-text">Save Configuration</span>
      <span class="btn-spinner" hidden>⏳</span>
    </button>
    <button type="reset" class="btn btn-secondary">Reset</button>
  </div>
</form>
```

**Validation Logic**:
```javascript
function validateForm(form) {
  let isValid = true;
  const inputs = form.querySelectorAll('input[required], select[required]');
  
  inputs.forEach(input => {
    const errorSpan = input.nextElementSibling;
    
    if (!input.validity.valid) {
      isValid = false;
      errorSpan.textContent = input.dataset.error || 'This field is required';
      errorSpan.hidden = false;
      input.classList.add('invalid');
    } else {
      errorSpan.hidden = true;
      input.classList.remove('invalid');
    }
  });
  
  return isValid;
}
```

**Unsaved Changes Tracking**:
```javascript
let formInitialState = {};

function trackFormChanges(form) {
  const formData = new FormData(form);
  const currentState = Object.fromEntries(formData);
  const hasChanges = JSON.stringify(currentState) !== JSON.stringify(formInitialState);
  
  // Enable/disable save button
  form.querySelector('button[type="submit"]').disabled = !hasChanges;
  
  // Show unsaved indicator in sidebar
  const section = form.closest('.content-section').id.replace('section-', '');
  const navItem = document.querySelector(`[data-section="${section}"]`);
  navItem.querySelector('.unsaved-indicator').hidden = !hasChanges;
}
```


### 8. Toast Notification System

**Purpose**: User feedback for actions and events

**Structure**:
```html
<div class="toast-container" id="toast-container">
  <!-- Toasts are dynamically added here -->
</div>
```

**Toast Creation**:
```javascript
function showToast(message, type = 'info', duration = 3000) {
  const toast = document.createElement('div');
  toast.className = `toast toast-${type}`;
  
  const icon = {
    success: '✅',
    error: '❌',
    warning: '⚠️',
    info: 'ℹ️'
  }[type] || 'ℹ️';
  
  toast.innerHTML = `
    <span class="toast-icon">${icon}</span>
    <span class="toast-message">${message}</span>
    <button class="toast-close" aria-label="Close">×</button>
  `;
  
  const container = document.getElementById('toast-container');
  container.appendChild(toast);
  
  // Animate in
  setTimeout(() => toast.classList.add('show'), 10);
  
  // Auto-remove
  setTimeout(() => {
    toast.classList.remove('show');
    setTimeout(() => toast.remove(), 300);
  }, duration);
  
  // Manual close
  toast.querySelector('.toast-close').onclick = () => {
    toast.classList.remove('show');
    setTimeout(() => toast.remove(), 300);
  };
}
```

**Toast Queue**:
```javascript
const toastQueue = [];
let isShowingToast = false;

function queueToast(message, type, duration) {
  toastQueue.push({ message, type, duration });
  if (!isShowingToast) {
    processToastQueue();
  }
}

function processToastQueue() {
  if (toastQueue.length === 0) {
    isShowingToast = false;
    return;
  }
  
  isShowingToast = true;
  const { message, type, duration } = toastQueue.shift();
  showToast(message, type, duration);
  
  setTimeout(processToastQueue, duration + 500);
}
```


## Data Models

### Configuration Data Model

```javascript
// Complete configuration structure
const configModel = {
  sip: {
    server: 'sip.domain.com',
    username: 'doorstation',
    password: '********',
    target1: 'sip:apartment1@domain.com',
    target2: 'sip:apartment2@domain.com'
  },
  network: {
    wifi: {
      ssid: 'MyNetwork',
      password: '********',
      mode: 'dhcp' // or 'static'
    },
    static_ip: {
      ip: '192.168.1.100',
      subnet: '255.255.255.0',
      gateway: '192.168.1.1',
      dns1: '8.8.8.8',
      dns2: '8.8.4.4'
    },
    ntp: {
      server: 'pool.ntp.org',
      timezone: 'Europe/Berlin'
    }
  },
  security: {
    dtmf: {
      pin_enabled: true,
      pin_code: '1234',
      timeout_ms: 10000,
      max_attempts: 3
    }
  },
  hardware: {
    gpio: {
      bell1: 21,
      bell2: 4,
      door_relay: 5,
      light_relay: 6,
      boot_button: 0
    },
    dtmf_codes: {
      '*1': 'open_door',
      '*2': 'toggle_light',
      '#': 'hangup'
    }
  },
  email: {
    smtp: {
      server: 'smtp.gmail.com',
      port: 587,
      username: 'user@gmail.com',
      password: '********',
      sender: 'doorstation@domain.com'
    },
    reports: {
      enabled: true,
      recipient: 'admin@domain.com',
      schedule: 'daily', // 'daily', 'weekly', 'monthly'
      time: '08:00',
      include_logs: true,
      include_backup: true
    }
  }
};
```


### System Status Data Model

```javascript
const statusModel = {
  system: {
    uptime: 3600000, // milliseconds
    firmware_version: '1.0.0',
    free_heap: 150000, // bytes
    min_free_heap: 120000,
    chip_model: 'ESP32-S3-WROOM-1',
    chip_revision: 3,
    flash_size: 8388608, // bytes
    flash_free: 4194304,
    psram_size: 2097152,
    psram_free: 1048576
  },
  network: {
    wifi: {
      status: 'connected', // 'connected', 'disconnected', 'connecting'
      ssid: 'MyNetwork',
      ip: '192.168.1.100',
      rssi: -45, // dBm
      mode: 'dhcp'
    },
    ntp: {
      synced: true,
      current_time: '2025-10-16 14:30:00',
      last_sync: 1697461800000
    }
  },
  sip: {
    status: 'registered', // 'registered', 'connecting', 'disconnected', 'error'
    state: 'IDLE', // 'IDLE', 'CALLING', 'RINGING', 'CONNECTED'
    server: 'sip.domain.com',
    last_call: 1697461800000
  },
  hardware: {
    door: {
      status: 'closed', // 'closed', 'open'
      last_activation: 1697461800000,
      duration: 3000 // ms
    },
    light: {
      status: 'off', // 'on', 'off'
      last_toggle: 1697461800000
    },
    buttons: {
      bell1_last_press: 1697461800000,
      bell2_last_press: 1697461800000
    }
  }
};
```


## API Endpoints

### Configuration Endpoints

| Method | Endpoint | Description | Request Body | Response |
|--------|----------|-------------|--------------|----------|
| GET | `/api/sip/config` | Get SIP configuration | - | `{ server, username, password, target1, target2 }` |
| POST | `/api/sip/config` | Save SIP configuration | `{ server, username, password, target1, target2 }` | `{ status: 'success' }` |
| GET | `/api/wifi/config` | Get WiFi configuration | - | `{ ssid, mode }` |
| POST | `/api/wifi/connect` | Connect to WiFi | `{ ssid, password }` | `{ status: 'success' }` |
| GET | `/api/network/ip` | Get IP configuration | - | `{ mode, ip, subnet, gateway, dns1, dns2 }` |
| POST | `/api/network/ip` | Set IP configuration | `{ mode, ip, subnet, gateway, dns1, dns2 }` | `{ status: 'success' }` |
| GET | `/api/ntp/config` | Get NTP configuration | - | `{ server, timezone }` |
| POST | `/api/ntp/config` | Save NTP configuration | `{ server, timezone }` | `{ status: 'success' }` |
| GET | `/api/dtmf/security` | Get DTMF security config | - | `{ pin_enabled, pin_code, timeout_ms, max_attempts }` |
| POST | `/api/dtmf/security` | Save DTMF security config | `{ pin_enabled, pin_code, timeout_ms, max_attempts }` | `{ status: 'success' }` |
| GET | `/api/email/config` | Get email configuration | - | `{ smtp, reports }` |
| POST | `/api/email/config` | Save email configuration | `{ smtp, reports }` | `{ status: 'success' }` |

### Status Endpoints

| Method | Endpoint | Description | Response |
|--------|----------|-------------|----------|
| GET | `/api/system/status` | Get system status | `{ uptime, free_heap, firmware_version, chip_model, ... }` |
| GET | `/api/wifi/status` | Get WiFi status | `{ status, ssid, ip, rssi }` |
| GET | `/api/sip/status` | Get SIP status | `{ status, state, server }` |
| GET | `/api/ntp/status` | Get NTP status | `{ synced, current_time }` |
| GET | `/api/hardware/status` | Get hardware status | `{ door, light, buttons }` |

### Action Endpoints

| Method | Endpoint | Description | Request Body | Response |
|--------|----------|-------------|--------------|----------|
| POST | `/api/sip/connect` | Connect to SIP server | - | `{ status: 'success' }` |
| POST | `/api/sip/disconnect` | Disconnect from SIP | - | `{ status: 'success' }` |
| POST | `/api/sip/test` | Test SIP configuration | - | `{ status: 'success', message }` |
| POST | `/api/sip/testcall` | Initiate test call | `{ target }` | `{ status: 'success' }` |
| POST | `/api/wifi/scan` | Scan WiFi networks | - | `{ networks: [{ ssid, rssi }] }` |
| POST | `/api/ntp/sync` | Force NTP sync | - | `{ status: 'success' }` |
| POST | `/api/system/restart` | Restart system | - | `{ status: 'success' }` |
| POST | `/api/system/factory-reset` | Factory reset | - | `{ status: 'success' }` |
| POST | `/api/hardware/test/bell` | Simulate bell press | `{ button: 1 or 2 }` | `{ status: 'success' }` |
| POST | `/api/hardware/test/door` | Trigger door opener | `{ duration: 3000 }` | `{ status: 'success' }` |
| POST | `/api/hardware/test/light` | Toggle light | - | `{ status: 'success', state: 'on' or 'off' }` |
| POST | `/api/email/test` | Send test email | - | `{ status: 'success' }` |
| POST | `/api/email/send-report` | Send report now | - | `{ status: 'success' }` |
| POST | `/api/ota/upload` | Upload firmware binary | multipart/form-data | `{ status: 'success', size }` |
| GET | `/api/ota/status` | Get OTA update status | - | `{ state, progress, message }` |
| POST | `/api/ota/apply` | Apply uploaded firmware | - | `{ status: 'success' }` |
| GET | `/api/ota/version` | Get firmware version info | - | `{ version, build_date, chip_model }` |

### Backup/Restore Endpoints

| Method | Endpoint | Description | Request Body | Response |
|--------|----------|-------------|--------------|----------|
| GET | `/api/config/backup` | Download config backup | - | JSON file download |
| POST | `/api/config/restore` | Restore configuration | `{ config: {...}, include_passwords: true }` | `{ status: 'success' }` |

### Log Endpoints

| Method | Endpoint | Description | Query Params | Response |
|--------|----------|-------------|--------------|----------|
| GET | `/api/sip/log` | Get SIP logs | `?since=timestamp` | `{ entries: [{ timestamp, type, message }] }` |
| GET | `/api/dtmf/logs` | Get DTMF logs | `?since=timestamp` | `{ logs: [{ timestamp, type, command, success }] }` |


## Error Handling

### API Error Handling

```javascript
async function apiRequest(endpoint, options = {}) {
  const defaultOptions = {
    method: 'GET',
    headers: {
      'Content-Type': 'application/json'
    }
  };
  
  const config = { ...defaultOptions, ...options };
  
  try {
    const response = await fetch(endpoint, config);
    
    if (!response.ok) {
      const errorData = await response.json().catch(() => ({}));
      throw new Error(errorData.message || `HTTP ${response.status}: ${response.statusText}`);
    }
    
    return await response.json();
  } catch (error) {
    console.error(`API Error [${endpoint}]:`, error);
    
    // Show user-friendly error
    if (error.message.includes('Failed to fetch')) {
      showToast('Connection error. Please check your network.', 'error');
    } else {
      showToast(error.message, 'error');
    }
    
    throw error;
  }
}
```

### Form Submission Error Handling

```javascript
async function handleFormSubmit(event) {
  event.preventDefault();
  const form = event.target;
  
  // Validate form
  if (!validateForm(form)) {
    showToast('Please fix the errors in the form', 'error');
    return;
  }
  
  // Show loading state
  const submitBtn = form.querySelector('button[type="submit"]');
  submitBtn.disabled = true;
  submitBtn.querySelector('.btn-text').hidden = true;
  submitBtn.querySelector('.btn-spinner').hidden = false;
  
  try {
    const formData = new FormData(form);
    const data = Object.fromEntries(formData);
    const endpoint = form.dataset.api;
    
    await apiRequest(endpoint, {
      method: 'POST',
      body: JSON.stringify(data)
    });
    
    showToast('Configuration saved successfully!', 'success');
    
    // Update initial state
    formInitialState = data;
    trackFormChanges(form);
    
  } catch (error) {
    // Error already shown by apiRequest
  } finally {
    // Reset button state
    submitBtn.disabled = false;
    submitBtn.querySelector('.btn-text').hidden = false;
    submitBtn.querySelector('.btn-spinner').hidden = true;
  }
}
```

### Network Timeout Handling

```javascript
function fetchWithTimeout(url, options = {}, timeout = 10000) {
  return Promise.race([
    fetch(url, options),
    new Promise((_, reject) =>
      setTimeout(() => reject(new Error('Request timeout')), timeout)
    )
  ]);
}
```


## Testing Strategy

### Unit Testing Approach

Since we're building a single-file SPA for ESP32-S3, traditional unit testing frameworks are not practical. Instead, we'll use:

1. **Manual Testing Checklist**: Comprehensive test scenarios for each feature
2. **Browser Console Testing**: Test functions directly in browser console
3. **API Endpoint Testing**: Use curl or Postman to test backend endpoints
4. **Cross-Browser Testing**: Test on Chrome, Firefox, Safari, Edge
5. **Mobile Device Testing**: Test on actual mobile devices (iOS, Android)

### Test Scenarios

#### Navigation Testing
- [ ] Click each sidebar menu item and verify correct section displays
- [ ] Verify active state highlighting works
- [ ] Test keyboard navigation (Tab, Enter, Arrow keys)
- [ ] Test mobile hamburger menu open/close
- [ ] Verify section state persists in sessionStorage

#### Status Panel Testing
- [ ] Verify status updates every 10 seconds
- [ ] Test collapse/expand functionality
- [ ] Verify color coding for different states
- [ ] Test status panel state persistence in localStorage

#### Theme Testing
- [ ] Toggle between light and dark themes
- [ ] Verify theme persists in localStorage
- [ ] Test system theme detection on first load
- [ ] Verify all components respect theme colors

#### Form Testing
- [ ] Test form validation (required fields, patterns)
- [ ] Verify inline error messages display
- [ ] Test unsaved changes indicator
- [ ] Test save button enable/disable logic
- [ ] Verify form submission and success/error handling
- [ ] Test form reset functionality

#### Search Testing
- [ ] Test global search with various queries
- [ ] Verify search results display correctly
- [ ] Test navigation to search results
- [ ] Verify result highlighting
- [ ] Test keyboard shortcuts (Ctrl+K, /)

#### Toast Notification Testing
- [ ] Test success, error, warning, info toasts
- [ ] Verify toast queue system
- [ ] Test manual close button
- [ ] Verify auto-dismiss timing

#### Responsive Design Testing
- [ ] Test on desktop (1920x1080, 1366x768)
- [ ] Test on tablet (768x1024)
- [ ] Test on mobile (375x667, 414x896)
- [ ] Verify sidebar collapses to hamburger on mobile
- [ ] Test touch gestures on mobile

#### Performance Testing
- [ ] Measure page load time
- [ ] Test with slow 3G network simulation
- [ ] Verify smooth transitions and animations
- [ ] Test memory usage over time


## Mobile Responsive Design

### Breakpoints

```css
/* Mobile: < 768px */
@media (max-width: 767px) {
  .sidebar {
    position: fixed;
    left: -100%;
    transition: left 0.3s ease;
    z-index: 1000;
  }
  
  .sidebar.open {
    left: 0;
  }
  
  .menu-toggle {
    display: block;
  }
  
  .status-grid {
    grid-template-columns: 1fr 1fr;
  }
  
  .main-content {
    margin-left: 0;
  }
}

/* Tablet: 768px - 1024px */
@media (min-width: 768px) and (max-width: 1024px) {
  .sidebar {
    width: 200px;
  }
  
  .status-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

/* Desktop: > 1024px */
@media (min-width: 1025px) {
  .sidebar {
    width: 240px;
  }
  
  .status-grid {
    grid-template-columns: repeat(4, 1fr);
  }
}
```

### Touch Gestures

```javascript
// Swipe to close sidebar on mobile
let touchStartX = 0;
let touchEndX = 0;

function handleTouchStart(e) {
  touchStartX = e.changedTouches[0].screenX;
}

function handleTouchEnd(e) {
  touchEndX = e.changedTouches[0].screenX;
  handleSwipe();
}

function handleSwipe() {
  const swipeDistance = touchEndX - touchStartX;
  
  // Swipe left to close sidebar (> 50px)
  if (swipeDistance < -50 && sidebar.classList.contains('open')) {
    closeSidebar();
  }
  
  // Swipe right to open sidebar (> 50px)
  if (swipeDistance > 50 && !sidebar.classList.contains('open')) {
    openSidebar();
  }
}

document.addEventListener('touchstart', handleTouchStart);
document.addEventListener('touchend', handleTouchEnd);
```

### Mobile Optimizations

1. **Viewport Configuration**:
```html
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
```

2. **Touch-Friendly Buttons**:
```css
.btn, .nav-item, .status-card {
  min-height: 44px;
  min-width: 44px;
}
```

3. **Input Type Optimization**:
```html
<input type="tel" inputmode="numeric"> <!-- For PIN codes -->
<input type="email"> <!-- For email addresses -->
<input type="url"> <!-- For SIP URIs -->
```


## Performance Optimization

### File Size Optimization

1. **Minification Strategy**:
   - CSS: Remove comments, whitespace, combine selectors
   - JavaScript: Minify variable names, remove console.logs
   - HTML: Remove unnecessary whitespace
   - Target: < 100KB uncompressed, < 30KB gzipped

2. **Gzip Compression**:
```c
// ESP32 web server configuration
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.uri_match_fn = httpd_uri_match_wildcard;
config.enable_so_linger = true;
config.linger_timeout = 5;

// Enable gzip compression for HTML
esp_http_server_register_uri_handler(server, &index_html_handler);
```

3. **Resource Inlining**:
   - Embed all CSS in `<style>` tags
   - Embed all JavaScript in `<script>` tags
   - Use data URIs for small icons
   - No external dependencies

### Memory Management

1. **DOM Manipulation**:
```javascript
// Use DocumentFragment for batch DOM updates
function updateLogEntries(entries) {
  const fragment = document.createDocumentFragment();
  
  entries.forEach(entry => {
    const div = document.createElement('div');
    div.className = 'log-entry';
    div.textContent = entry.message;
    fragment.appendChild(div);
  });
  
  logContainer.appendChild(fragment);
}
```

2. **Event Delegation**:
```javascript
// Instead of adding listeners to each item
document.querySelector('.nav-menu').addEventListener('click', (e) => {
  const navItem = e.target.closest('.nav-item');
  if (navItem) {
    navigateToSection(navItem.dataset.section);
  }
});
```

3. **Debouncing**:
```javascript
function debounce(func, wait) {
  let timeout;
  return function executedFunction(...args) {
    clearTimeout(timeout);
    timeout = setTimeout(() => func.apply(this, args), wait);
  };
}

// Use for search input
searchInput.addEventListener('input', debounce(performSearch, 300));
```

### Caching Strategy

1. **Browser Caching**:
```c
// Set cache headers for static content
httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600");
```

2. **LocalStorage Caching**:
```javascript
// Cache configuration data
function cacheConfig(key, data) {
  localStorage.setItem(`config_${key}`, JSON.stringify(data));
  localStorage.setItem(`config_${key}_timestamp`, Date.now());
}

function getCachedConfig(key, maxAge = 60000) {
  const timestamp = localStorage.getItem(`config_${key}_timestamp`);
  if (Date.now() - timestamp < maxAge) {
    return JSON.parse(localStorage.getItem(`config_${key}`));
  }
  return null;
}
```


## Accessibility Features

### WCAG AA Compliance

1. **Color Contrast**:
   - Light theme: 4.5:1 minimum for text
   - Dark theme: 4.5:1 minimum for text
   - Status indicators: 3:1 minimum for UI components

2. **Keyboard Navigation**:
```javascript
// Tab order management
document.addEventListener('keydown', (e) => {
  // Ctrl+K or / for search
  if ((e.ctrlKey && e.key === 'k') || e.key === '/') {
    e.preventDefault();
    document.getElementById('global-search').focus();
  }
  
  // Escape to close modals/search
  if (e.key === 'Escape') {
    closeAllModals();
    document.getElementById('search-results').hidden = true;
  }
  
  // Arrow keys in search results
  if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
    navigateSearchResults(e.key);
  }
});
```

3. **ARIA Labels**:
```html
<button class="menu-toggle" 
        aria-label="Toggle navigation menu"
        aria-expanded="false"
        aria-controls="sidebar">
  ☰
</button>

<nav class="sidebar" 
     id="sidebar" 
     role="navigation" 
     aria-label="Main navigation">
  <!-- Menu items -->
</nav>

<div class="status-card" 
     role="status" 
     aria-live="polite"
     aria-label="WiFi connection status">
  <!-- Status content -->
</div>
```

4. **Focus Management**:
```javascript
function navigateToSection(sectionId) {
  // ... navigation logic ...
  
  // Set focus to section heading
  const heading = document.querySelector(`#section-${sectionId} h2`);
  if (heading) {
    heading.setAttribute('tabindex', '-1');
    heading.focus();
  }
}
```

5. **Screen Reader Support**:
```html
<span class="sr-only">Loading configuration...</span>

<style>
.sr-only {
  position: absolute;
  width: 1px;
  height: 1px;
  padding: 0;
  margin: -1px;
  overflow: hidden;
  clip: rect(0, 0, 0, 0);
  white-space: nowrap;
  border-width: 0;
}
</style>
```


## Section-Specific Designs

### Dashboard Section

**Layout**:
```
┌─────────────────────────────────────────────────────────┐
│ Dashboard                                               │
├─────────────────────────────────────────────────────────┤
│ System Information                                      │
│ ┌──────────┬──────────┬──────────┬──────────┐          │
│ │ Uptime   │ Free Heap│ IP Addr  │ Firmware │          │
│ │ 2h 15m   │ 150 KB   │ 192...   │ v1.0     │          │
│ └──────────┴──────────┴──────────┴──────────┘          │
│                                                         │
│ Quick Actions                                           │
│ [Connect SIP] [Disconnect] [Restart] [Factory Reset]   │
│                                                         │
│ Recent Activity (Last 10 entries)                       │
│ ┌─────────────────────────────────────────────────┐    │
│ │ [14:30] Bell 1 pressed                          │    │
│ │ [14:25] SIP call completed                      │    │
│ │ [14:20] Door opened                             │    │
│ └─────────────────────────────────────────────────┘    │
│                                                         │
│ Configuration Backup                                    │
│ [Download Backup] [Restore from File]                  │
└─────────────────────────────────────────────────────────┘
```

### SIP Settings Section

**Layout**:
```
┌─────────────────────────────────────────────────────────┐
│ SIP Settings                                            │
├─────────────────────────────────────────────────────────┤
│ Server Configuration                                    │
│ SIP Server: [sip.domain.com                    ]        │
│ Username:   [doorstation                       ]        │
│ Password:   [••••••••••                        ]        │
│                                                         │
│ Call Targets                                            │
│ Target 1:   [sip:apt1@domain.com              ] [Test]  │
│ Target 2:   [sip:apt2@domain.com              ] [Test]  │
│                                                         │
│ Connection Management                                   │
│ [Save Configuration] [Test Config] [Connect] [Disconnect]│
└─────────────────────────────────────────────────────────┘
```

### Network Settings Section

**Layout**:
```
┌─────────────────────────────────────────────────────────┐
│ Network Settings                                        │
├─────────────────────────────────────────────────────────┤
│ WiFi Configuration                                      │
│ [Scan Networks]                                         │
│ ┌─────────────────────────────────────────────────┐    │
│ │ MyNetwork        (-45 dBm)                      │    │
│ │ GuestNetwork     (-67 dBm)                      │    │
│ └─────────────────────────────────────────────────┘    │
│ SSID:     [MyNetwork                           ]        │
│ Password: [••••••••••                          ]        │
│ [Connect to WiFi]                                       │
│                                                         │
│ IP Configuration                                        │
│ Mode: (•) DHCP  ( ) Static IP                          │
│ IP Address: [192.168.1.100                     ]        │
│ Subnet:     [255.255.255.0                     ]        │
│ Gateway:    [192.168.1.1                       ]        │
│ DNS 1:      [8.8.8.8                           ]        │
│ DNS 2:      [8.8.4.4                           ]        │
│ [Save IP Configuration]                                 │
│                                                         │
│ NTP Time Synchronization                                │
│ Server:   [pool.ntp.org                        ]        │
│ Timezone: [Europe/Berlin                       ]        │
│ [Save NTP Config] [Force Sync]                          │
└─────────────────────────────────────────────────────────┘
```

### Hardware Settings Section

**Layout**:
```
┌─────────────────────────────────────────────────────────┐
│ Hardware Settings                                       │
├─────────────────────────────────────────────────────────┤
│ Hardware Information                                    │
│ Chip Model:    ESP32-S3-WROOM-1                         │
│ Chip Revision: 3                                        │
│ Flash Size:    8 MB (4 MB free)                         │
│ PSRAM:         2 MB (1 MB free)                         │
│ Free Heap:     150 KB (min: 120 KB)                     │
│                                                         │
│ GPIO Pin Assignments                                    │
│ ┌──────────────┬──────┬────────┐                       │
│ │ Function     │ GPIO │ State  │                       │
│ ├──────────────┼──────┼────────┤                       │
│ │ Bell 1       │  21  │ Ready  │                       │
│ │ Bell 2       │   4  │ Ready  │                       │
│ │ Door Relay   │   5  │ Off    │                       │
│ │ Light Relay  │   6  │ Off    │                       │
│ │ BOOT Button  │   0  │ Ready  │                       │
│ └──────────────┴──────┴────────┘                       │
│                                                         │
│ DTMF Control Codes                                      │
│ *1 - Open Door (3 seconds)                              │
│ *2 - Toggle Light                                       │
│ #  - End Call                                           │
└─────────────────────────────────────────────────────────┘
```


### Security Settings Section

**Layout**:
```
┌─────────────────────────────────────────────────────────┐
│ Security Settings                                       │
├─────────────────────────────────────────────────────────┤
│ DTMF PIN Protection                                     │
│ Current Status: ✅ Enabled                              │
│ Current PIN:    ****                                    │
│                                                         │
│ [✓] Enable PIN Protection                              │
│ PIN Code (1-8 digits): [1234                   ]        │
│ Command Timeout:       [10000                  ] ms     │
│ Max Failed Attempts:   [3                      ]        │
│                                                         │
│ ℹ️ When enabled, door opener requires: *[PIN]#         │
│    Example: *1234# (instead of legacy *1#)             │
│                                                         │
│ [Save Security Configuration]                           │
└─────────────────────────────────────────────────────────┘
```

### System Logs Section

**Layout**:
```
┌─────────────────────────────────────────────────────────┐
│ System Logs                                             │
├─────────────────────────────────────────────────────────┤
│ SIP Connection Log                                      │
│ [🔍 Search] [Filter: All ▼] [Refresh] [Clear] [Export] │
│ [✓] Auto-refresh (500ms)                                │
│ Showing 15 of 234 entries                               │
│ ┌─────────────────────────────────────────────────┐    │
│ │ [14:30:15] [INFO] SIP registration successful   │    │
│ │ [14:30:10] [SENT] REGISTER sip:server.com      │    │
│ │ [14:30:05] [RECV] 200 OK                       │    │
│ │ [14:29:58] [ERROR] Connection timeout          │    │
│ └─────────────────────────────────────────────────┘    │
│ [Jump to Bottom]                                        │
│                                                         │
│ DTMF Security Log                                       │
│ [🔍 Search] [Filter: All ▼] [Refresh] [Clear] [Export] │
│ ┌─────────────────────────────────────────────────┐    │
│ │ [14:25:30] ✅ *1234# → Door opened              │    │
│ │ [14:20:15] ❌ *9999# → Invalid PIN              │    │
│ │ [14:15:00] ⚙️ Configuration updated             │    │
│ └─────────────────────────────────────────────────┘    │
│ [Jump to Bottom]                                        │
└─────────────────────────────────────────────────────────┘
```

### Hardware Testing Section

**Layout**:
```
┌─────────────────────────────────────────────────────────┐
│ Hardware Testing                                        │
├─────────────────────────────────────────────────────────┤
│ ⚠️ Warning: These actions will physically activate      │
│    hardware components. Use with caution!               │
│                                                         │
│ Doorbell Button Simulation                              │
│ [Simulate Bell 1 Press] [Simulate Bell 2 Press]        │
│                                                         │
│ Relay Testing                                           │
│ Door Opener:                                            │
│   Duration: [3] seconds (1-10)                          │
│   [Trigger Door Opener]                                 │
│   Status: ○ Inactive                                    │
│                                                         │
│ Light Control:                                          │
│   [Toggle Light]                                        │
│   Status: ○ Off                                         │
│                                                         │
│ Test Results                                            │
│ ┌─────────────────────────────────────────────────┐    │
│ │ [14:30] Bell 1 simulated - Call initiated       │    │
│ │ [14:25] Door opener triggered - 3s duration     │    │
│ │ [14:20] Light toggled - Now ON                  │    │
│ └─────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
```


### Email Reports Section

**Layout**:
```
┌─────────────────────────────────────────────────────────┐
│ Email Reports                                           │
├─────────────────────────────────────────────────────────┤
│ SMTP Server Configuration                               │
│ Server:   [smtp.gmail.com                      ]        │
│ Port:     [587                                 ]        │
│ Username: [user@gmail.com                      ]        │
│ Password: [••••••••••                          ]        │
│ Sender:   [doorstation@domain.com              ]        │
│ [Test Email Connection]                                 │
│                                                         │
│ Report Configuration                                    │
│ [✓] Enable Automated Reports                           │
│ Recipient: [admin@domain.com                   ]        │
│                                                         │
│ Schedule:                                               │
│ (•) Daily    ( ) Weekly    ( ) Monthly                 │
│ Time: [08:00]                                           │
│                                                         │
│ Report Content:                                         │
│ [✓] System status summary                              │
│ [✓] Activity logs (SIP calls, DTMF events)             │
│ [✓] Configuration backup (JSON attachment)             │
│                                                         │
│ Last Report: 2025-10-16 08:00 - ✅ Success             │
│                                                         │
│ [Save Configuration] [Send Report Now]                  │
└─────────────────────────────────────────────────────────┘
```

### Documentation Section

**Layout**:
```
┌─────────────────────────────────────────────────────────┐
│ Documentation                                           │
├─────────────────────────────────────────────────────────┤
│ [🔍 Search documentation...]                            │
│                                                         │
│ Table of Contents                                       │
│ ▼ Quick Start Guide                                    │
│   • Initial Setup                                       │
│   • WiFi Configuration                                  │
│   • SIP Registration                                    │
│ ▼ Hardware Reference                                   │
│   • GPIO Pinout                                         │
│   • Wiring Diagrams                                     │
│   • Specifications                                      │
│ ▼ SIP Configuration                                    │
│   • Server Setup                                        │
│   • Authentication                                      │
│   • Troubleshooting                                     │
│ ▼ DTMF Commands                                        │
│ ▼ Network Configuration                                │
│ ▼ API Reference                                        │
│ ▼ Troubleshooting                                      │
│ ▼ FAQ                                                  │
│                                                         │
│ ─────────────────────────────────────────────────      │
│                                                         │
│ Quick Start Guide                                       │
│                                                         │
│ 1. Initial Setup                                        │
│    Connect your ESP32 to power and wait for the        │
│    device to boot...                                    │
│                                                         │
│    [Copy Example]                                       │
│                                                         │
│ Firmware Version: v1.0.0                                │
│ Build Date: 2025-10-16                                  │
│                                                         │
│ [Report Issue] [View on GitHub]                         │
└─────────────────────────────────────────────────────────┘
```


## Implementation Considerations

### ESP32-S3 Constraints

1. **Memory Limitations**:
   - Target HTML file size: < 100KB uncompressed
   - Enable gzip compression on ESP32 web server
   - Use efficient DOM manipulation techniques
   - Limit log entry retention (max 500 entries)

2. **Processing Power**:
   - Minimize JavaScript execution time
   - Use debouncing for frequent events
   - Avoid complex animations
   - Limit concurrent API requests

3. **Network Bandwidth**:
   - Single file reduces HTTP requests
   - Implement efficient polling (10s intervals)
   - Use incremental log fetching (since timestamp)
   - Compress API responses

### Browser Compatibility

**Target Browsers**:
- Chrome/Edge 90+
- Firefox 88+
- Safari 14+
- Mobile browsers (iOS Safari, Chrome Mobile)

**Polyfills Not Required**:
- ES6+ features (arrow functions, template literals, async/await)
- Fetch API
- CSS Grid and Flexbox
- CSS Custom Properties

### Security Considerations

1. **Password Handling**:
   - Never log passwords
   - Use password input type
   - Consider adding "show password" toggle
   - Warn about backup file security

2. **XSS Prevention**:
```javascript
function escapeHtml(text) {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

// Use when inserting user-generated content
logEntry.innerHTML = `<span>${escapeHtml(message)}</span>`;
```

3. **CSRF Protection**:
   - Not required for same-origin requests
   - Consider adding token for future authentication

### Future Extensibility

**Adding New Sections**:
1. Add menu item to sidebar HTML
2. Add content section to main area
3. Add to search index
4. Add API endpoints
5. Update navigation logic (automatic via data attributes)

**Example**:
```html
<!-- In sidebar -->
<li class="nav-item" data-section="mqtt">
  <span class="nav-icon">📡</span>
  <span class="nav-label">MQTT</span>
</li>

<!-- In main content -->
<section class="content-section" id="section-mqtt" hidden>
  <h2>MQTT Configuration</h2>
  <!-- MQTT settings form -->
</section>
```

No JavaScript changes required - navigation system handles it automatically!


## Design Decisions and Rationale

### Why Sidebar Navigation (Mockup B)?

1. **Modern UX**: Sidebar navigation is the standard in modern web applications (Gmail, Slack, Discord)
2. **Scalability**: Easy to add new menu items without cluttering the interface
3. **Mobile-Friendly**: Collapses to hamburger menu on mobile devices
4. **Visual Hierarchy**: Clear separation between navigation and content
5. **Always Accessible**: Navigation is always visible (or one tap away on mobile)

### Why Single-Page Application?

1. **ESP32 Constraints**: Single file minimizes memory usage and HTTP requests
2. **Performance**: No page reloads, instant navigation
3. **State Preservation**: Maintains status updates and form state
4. **Simpler Deployment**: One file to upload to ESP32
5. **Better UX**: Smooth transitions, no loading flashes

### Why Embedded CSS/JS?

1. **Reduced Requests**: No external file requests
2. **Offline Capable**: Works without internet connection
3. **Faster Load**: Single HTTP request
4. **Simpler Caching**: One file to cache
5. **ESP32 Friendly**: Easier to serve from flash memory

### Why No External Libraries?

1. **Size Constraints**: Libraries add significant overhead
2. **Performance**: Vanilla JS is faster for simple operations
3. **Maintenance**: No dependency updates required
4. **Compatibility**: No version conflicts
5. **Learning Curve**: Easier for contributors to understand

### Why CSS Custom Properties for Theming?

1. **Dynamic Switching**: Change theme without page reload
2. **Maintainability**: Single source of truth for colors
3. **Browser Support**: Widely supported (95%+ browsers)
4. **Performance**: No JavaScript required for theme application
5. **Extensibility**: Easy to add new themes

### Why LocalStorage for Preferences?

1. **Persistence**: Survives page reloads and browser restarts
2. **No Server Load**: Client-side storage
3. **Instant Access**: No API calls required
4. **Privacy**: Data stays on user's device
5. **Simple API**: Easy to use and understand


## Migration Strategy

### From Current to New Interface

**Phase 1: Preparation**
1. Document all existing API endpoints
2. Test all current functionality
3. Export current configuration as backup
4. Take screenshots of current interface for reference

**Phase 2: Development**
1. Create new index.html with sidebar structure
2. Implement theme system and navigation
3. Port Dashboard section (system info, quick actions)
4. Port SIP Settings section
5. Port Network Settings section
6. Port Hardware Settings section
7. Port Security Settings section
8. Port System Logs section
9. Add new sections (Testing, Email, Documentation)

**Phase 3: Testing**
1. Test all forms and validation
2. Test API integration
3. Test on multiple browsers
4. Test on mobile devices
5. Test theme switching
6. Test search functionality
7. Performance testing

**Phase 4: Deployment**
1. Backup current index.html
2. Upload new index.html to ESP32
3. Test in production environment
4. Monitor for issues
5. Gather user feedback

**Rollback Plan**:
- Keep backup of old index.html
- If critical issues found, revert to old version
- Fix issues in development
- Re-deploy when stable

### Backward Compatibility

**API Endpoints**: No changes required to existing endpoints
**Configuration**: Existing configuration remains valid
**Functionality**: All existing features preserved
**User Data**: No data migration needed


## Summary

This design document provides a comprehensive blueprint for refactoring the ESP32 SIP Door Station web interface into a modern, scalable, and maintainable single-page application with sidebar navigation.

### Key Features

✅ **Modern Sidebar Navigation**: App-like interface with collapsible menu
✅ **Persistent Status Panel**: Always-visible system status indicators
✅ **Light/Dark Theme**: System preference detection and manual toggle
✅ **Global Search**: Find settings across all sections
✅ **Mobile Responsive**: Hamburger menu, touch gestures, optimized layouts
✅ **Form Validation**: Inline errors, unsaved changes tracking
✅ **Toast Notifications**: User feedback for all actions
✅ **Log Search/Filter**: Real-time log filtering and export
✅ **Hardware Testing**: Safe component testing interface
✅ **Email Reports**: Automated system reports and backups
✅ **Documentation**: Built-in help and API reference
✅ **Backup/Restore**: Configuration export/import
✅ **Factory Reset**: System reset capability
✅ **Static IP**: Advanced network configuration
✅ **Accessibility**: WCAG AA compliant, keyboard navigation
✅ **Performance**: Optimized for ESP32-S3 constraints

### Architecture Highlights

- **Single HTML File**: < 100KB uncompressed, < 30KB gzipped
- **No External Dependencies**: Pure HTML/CSS/JavaScript
- **Client-Side Navigation**: No page reloads
- **Modular Code**: Easy to extend with new sections
- **API-Driven**: Clean separation of frontend and backend
- **Theme System**: CSS custom properties for easy theming
- **Event-Driven**: Efficient DOM updates and API polling

### Future-Ready

The architecture supports planned features without restructuring:
- Authentication & Certificates
- IP Access Control
- MQTT Home Automation
- Video Settings & Preview
- Button Action Mapping

This design balances modern UX expectations with ESP32-S3 hardware constraints, providing a professional, maintainable, and extensible web interface.


### OTA Update Section

**Layout**:
```
┌─────────────────────────────────────────────────────────┐
│ OTA Firmware Update                                     │
├─────────────────────────────────────────────────────────┤
│ Current Firmware                                        │
│ Version:    v1.0.0                                      │
│ Build Date: 2025-10-16 14:30:00                         │
│ Chip Model: ESP32-S3-WROOM-1                            │
│                                                         │
│ ⚠️ Warning: Do not power off the device during update! │
│                                                         │
│ Upload Firmware Binary                                  │
│ ┌─────────────────────────────────────────────────┐    │
│ │ [Choose File] sip_doorbell.bin                  │    │
│ │                                                 │    │
│ │ Drag and drop firmware file here                │    │
│ │ or click to browse                              │    │
│ └─────────────────────────────────────────────────┘    │
│                                                         │
│ File: sip_doorbell.bin (1.2 MB)                         │
│ Status: Ready to upload                                 │
│                                                         │
│ [Upload and Update Firmware]                            │
│                                                         │
│ Update Progress                                         │
│ ┌─────────────────────────────────────────────────┐    │
│ │ ████████████████░░░░░░░░░░░░░░░░░░░░ 45%       │    │
│ └─────────────────────────────────────────────────┘    │
│ Uploading... (540 KB / 1.2 MB)                          │
│ Estimated time: 15 seconds                              │
│                                                         │
│ Update Log                                              │
│ ┌─────────────────────────────────────────────────┐    │
│ │ [14:30] Firmware file validated                 │    │
│ │ [14:30] Starting upload...                      │    │
│ │ [14:30] Upload progress: 45%                    │    │
│ └─────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
```

**Update States**:
1. **Idle**: Ready to select file
2. **File Selected**: File validated, ready to upload
3. **Uploading**: Progress bar showing upload
4. **Verifying**: Checking firmware integrity
5. **Applying**: Writing to flash
6. **Rebooting**: Device restarting
7. **Complete**: Update successful
8. **Failed**: Error occurred, rollback if possible

**API Endpoints**:
```javascript
// OTA Update endpoints
POST /api/ota/upload     // Upload firmware binary (multipart/form-data)
GET  /api/ota/status     // Get update status and progress
POST /api/ota/apply      // Apply uploaded firmware
GET  /api/ota/version    // Get current firmware info
```

**Upload Logic**:
```javascript
async function uploadFirmware(file) {
  // Validate file
  if (!file.name.endsWith('.bin')) {
    showToast('Invalid file type. Please select a .bin file', 'error');
    return;
  }
  
  if (file.size > 5 * 1024 * 1024) { // 5MB limit
    showToast('File too large. Maximum size is 5MB', 'error');
    return;
  }
  
  // Confirm update
  if (!confirm('This will update the firmware and restart the device. Continue?')) {
    return;
  }
  
  const formData = new FormData();
  formData.append('firmware', file);
  
  try {
    const xhr = new XMLHttpRequest();
    
    // Progress tracking
    xhr.upload.addEventListener('progress', (e) => {
      if (e.lengthComputable) {
        const percent = (e.loaded / e.total) * 100;
        updateProgressBar(percent);
        updateUploadStatus(`Uploading... (${formatBytes(e.loaded)} / ${formatBytes(e.total)})`);
      }
    });
    
    // Upload complete
    xhr.addEventListener('load', () => {
      if (xhr.status === 200) {
        showToast('Firmware uploaded successfully. Applying update...', 'success');
        updateUploadStatus('Verifying firmware...');
        
        // Poll for update status
        pollUpdateStatus();
      } else {
        showToast('Upload failed: ' + xhr.statusText, 'error');
      }
    });
    
    // Upload error
    xhr.addEventListener('error', () => {
      showToast('Upload failed. Please try again.', 'error');
    });
    
    xhr.open('POST', '/api/ota/upload');
    xhr.send(formData);
    
  } catch (error) {
    showToast('Error uploading firmware: ' + error.message, 'error');
  }
}

async function pollUpdateStatus() {
  const interval = setInterval(async () => {
    try {
      const status = await apiRequest('/api/ota/status');
      
      updateUploadStatus(status.message);
      
      if (status.state === 'complete') {
        clearInterval(interval);
        showToast('Update complete! Device will restart...', 'success');
        
        // Wait for device to restart
        setTimeout(() => {
          window.location.reload();
        }, 10000);
      }
      
      if (status.state === 'failed') {
        clearInterval(interval);
        showToast('Update failed: ' + status.error, 'error');
      }
      
    } catch (error) {
      // Device might be restarting, ignore errors
    }
  }, 1000);
}
```

**Safety Features**:
- File validation (size, extension)
- Confirmation dialog
- Progress indication
- Error handling with rollback
- Warning about power loss
- Automatic page reload after update
