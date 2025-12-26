# Theme Toggle (Light/Dark Mode) - Design Document

## Overview

This feature implements a light/dark theme toggle for the ESP32 SIP Door Station web interface, allowing users to switch between color schemes based on their preference or ambient lighting conditions. The theme preference is persisted in browser local storage and automatically applied on subsequent visits.

## Architecture

### High-Level Architecture

```
┌─────────────────┐
│  Web Interface  │
│   (Browser)     │
│  - CSS Variables│
│  - Theme Toggle │
│  - LocalStorage │
└─────────────────┘
```

This is a **client-side only** feature with no server-side components required.

### Component Interaction

```
User Click → Toggle Button → JavaScript Handler → Update CSS Variables → Save to LocalStorage
                                                          ↓
                                                   Apply Theme Classes
```

## Components and Interfaces

### 1. CSS Theme Variables

**Purpose**: Define color schemes using CSS custom properties for easy theme switching.

**Light Theme Variables**:
```css
:root {
  /* Background Colors */
  --bg-primary: #ffffff;
  --bg-secondary: #f5f5f5;
  --bg-tertiary: #e0e0e0;
  --bg-card: #ffffff;
  --bg-header: #f8f9fa;
  
  /* Text Colors */
  --text-primary: #212529;
  --text-secondary: #6c757d;
  --text-tertiary: #adb5bd;
  
  /* Border Colors */
  --border-color: #dee2e6;
  --border-light: #e9ecef;
  
  /* Status Colors */
  --status-success: #28a745;
  --status-warning: #ffc107;
  --status-error: #dc3545;
  --status-info: #17a2b8;
  
  /* Interactive Elements */
  --btn-primary-bg: #007bff;
  --btn-primary-text: #ffffff;
  --btn-primary-hover: #0056b3;
  --btn-secondary-bg: #6c757d;
  --btn-secondary-text: #ffffff;
  
  /* Input Elements */
  --input-bg: #ffffff;
  --input-border: #ced4da;
  --input-focus-border: #80bdff;
  --input-text: #495057;
  
  /* Sidebar */
  --sidebar-bg: #343a40;
  --sidebar-text: #ffffff;
  --sidebar-hover: #495057;
  --sidebar-active: #007bff;
  
  /* Shadows */
  --shadow-sm: 0 1px 2px rgba(0,0,0,0.05);
  --shadow-md: 0 4px 6px rgba(0,0,0,0.1);
  --shadow-lg: 0 10px 15px rgba(0,0,0,0.15);
}
```

**Dark Theme Variables**:
```css
[data-theme="dark"] {
  /* Background Colors */
  --bg-primary: #1a1a1a;
  --bg-secondary: #2d2d2d;
  --bg-tertiary: #3a3a3a;
  --bg-card: #242424;
  --bg-header: #1f1f1f;
  
  /* Text Colors */
  --text-primary: #e0e0e0;
  --text-secondary: #b0b0b0;
  --text-tertiary: #808080;
  
  /* Border Colors */
  --border-color: #404040;
  --border-light: #353535;
  
  /* Status Colors */
  --status-success: #4caf50;
  --status-warning: #ff9800;
  --status-error: #f44336;
  --status-info: #2196f3;
  
  /* Interactive Elements */
  --btn-primary-bg: #2196f3;
  --btn-primary-text: #ffffff;
  --btn-primary-hover: #1976d2;
  --btn-secondary-bg: #757575;
  --btn-secondary-text: #ffffff;
  
  /* Input Elements */
  --input-bg: #2d2d2d;
  --input-border: #404040;
  --input-focus-border: #2196f3;
  --input-text: #e0e0e0;
  
  /* Sidebar */
  --sidebar-bg: #1f1f1f;
  --sidebar-text: #e0e0e0;
  --sidebar-hover: #2d2d2d;
  --sidebar-active: #2196f3;
  
  /* Shadows */
  --shadow-sm: 0 1px 2px rgba(0,0,0,0.3);
  --shadow-md: 0 4px 6px rgba(0,0,0,0.4);
  --shadow-lg: 0 10px 15px rgba(0,0,0,0.5);
}
```

### 2. Theme Toggle Component

**Purpose**: Provide a user-friendly toggle button for switching themes.

**Toggle Button Designs**:

**Option 1: Icon Toggle**
```html
<button id="theme-toggle" class="theme-toggle" aria-label="Toggle theme">
  <span class="theme-icon light-icon">☀️</span>
  <span class="theme-icon dark-icon">🌙</span>
</button>
```

**Option 2: Switch Toggle**
```html
<div class="theme-toggle-container">
  <label class="theme-switch">
    <input type="checkbox" id="theme-toggle">
    <span class="slider"></span>
  </label>
  <span class="theme-label">Dark Mode</span>
</div>
```

**Option 3: Dropdown Menu**
```html
<select id="theme-toggle" class="theme-select">
  <option value="light">Light</option>
  <option value="dark">Dark</option>
  <option value="auto">Auto (System)</option>
</select>
```

### 3. JavaScript Theme Manager

**Purpose**: Handle theme switching, persistence, and system preference detection.

**Public API**:
```javascript
// Theme manager object
const ThemeManager = {
  // Initialize theme on page load
  init: function() { },
  
  // Get current theme
  getTheme: function() { },
  
  // Set theme (light, dark, auto)
  setTheme: function(theme) { },
  
  // Toggle between light and dark
  toggleTheme: function() { },
  
  // Detect system preference
  getSystemTheme: function() { },
  
  // Apply theme to document
  applyTheme: function(theme) { }
};
```

## Data Models

### Theme State
```javascript
{
  current: "light" | "dark" | "auto",
  system: "light" | "dark",
  effective: "light" | "dark"  // Actual applied theme
}
```

### LocalStorage Schema
```javascript
{
  "theme": "light" | "dark" | "auto"
}
```

## Error Handling

### Fallback Behavior
- If localStorage is unavailable: Use system preference
- If system preference unavailable: Default to light theme
- If invalid theme value in storage: Reset to light theme

### Browser Compatibility
- CSS Variables: Supported in all modern browsers (IE11+ with polyfill)
- LocalStorage: Supported in all browsers
- prefers-color-scheme: Supported in modern browsers (graceful degradation)

## Testing Strategy

### Manual Tests
1. **Theme Switching**
   - Click toggle button
   - Verify theme changes immediately
   - Verify all UI elements update correctly

2. **Persistence**
   - Set theme to dark
   - Reload page
   - Verify dark theme is still active

3. **System Preference**
   - Set theme to "auto"
   - Change system theme
   - Verify web interface follows system theme

4. **Visual Consistency**
   - Check all pages in light theme
   - Check all pages in dark theme
   - Verify no color contrast issues
   - Verify all text is readable

### Automated Tests
1. **JavaScript Tests**
   - Test theme detection
   - Test theme switching
   - Test localStorage persistence
   - Test system preference detection

2. **CSS Tests**
   - Verify all CSS variables are defined
   - Check color contrast ratios (WCAG AA)
   - Validate no hardcoded colors remain

## Accessibility Considerations

### WCAG Compliance
- **Color Contrast**: Minimum 4.5:1 for normal text, 3:1 for large text
- **Focus Indicators**: Visible focus states in both themes
- **Keyboard Navigation**: Toggle accessible via keyboard
- **Screen Readers**: Proper ARIA labels for toggle button

### Contrast Ratios
**Light Theme**:
- Text on background: 16:1 (AAA)
- Links on background: 7:1 (AAA)
- Buttons: 4.5:1 (AA)

**Dark Theme**:
- Text on background: 14:1 (AAA)
- Links on background: 6:1 (AAA)
- Buttons: 4.5:1 (AA)

## Performance Considerations

### CSS Performance
- Use CSS variables for instant theme switching (no page reload)
- Minimize CSS specificity for faster rendering
- Use hardware-accelerated transitions

### JavaScript Performance
- Theme detection: <10ms
- Theme switching: <50ms
- LocalStorage access: <5ms

### Memory Usage
- CSS variables: Negligible
- JavaScript theme manager: <1KB
- LocalStorage: <100 bytes

## Implementation Notes

### System Theme Detection
```javascript
function getSystemTheme() {
  if (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) {
    return 'dark';
  }
  return 'light';
}

// Listen for system theme changes
window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (e) => {
  if (ThemeManager.getTheme() === 'auto') {
    ThemeManager.applyTheme(e.matches ? 'dark' : 'light');
  }
});
```

### Theme Application
```javascript
function applyTheme(theme) {
  // Remove existing theme
  document.documentElement.removeAttribute('data-theme');
  
  // Apply new theme
  if (theme === 'dark') {
    document.documentElement.setAttribute('data-theme', 'dark');
  }
  
  // Update toggle button state
  updateToggleButton(theme);
}
```

### LocalStorage Persistence
```javascript
function saveTheme(theme) {
  try {
    localStorage.setItem('theme', theme);
  } catch (e) {
    console.warn('Failed to save theme preference:', e);
  }
}

function loadTheme() {
  try {
    return localStorage.getItem('theme') || 'auto';
  } catch (e) {
    console.warn('Failed to load theme preference:', e);
    return 'auto';
  }
}
```

### Smooth Transitions
```css
/* Disable transitions during theme switch to prevent flash */
.theme-transitioning * {
  transition: none !important;
}

/* Enable transitions after theme switch */
body {
  transition: background-color 0.3s ease, color 0.3s ease;
}

button, input, select {
  transition: background-color 0.2s ease, border-color 0.2s ease;
}
```

## Web Interface Design

### Theme Toggle Placement

**Option 1: Header Right**
```html
<header class="main-header">
  <h1>ESP32 SIP Door Station</h1>
  <div class="header-controls">
    <button id="theme-toggle" class="theme-toggle">
      <span class="icon">🌙</span>
    </button>
  </div>
</header>
```

**Option 2: Sidebar Bottom**
```html
<aside class="sidebar">
  <nav><!-- menu items --></nav>
  <div class="sidebar-footer">
    <button id="theme-toggle" class="theme-toggle">
      <span class="icon">🌙</span>
      <span class="label">Dark Mode</span>
    </button>
  </div>
</aside>
```

### Complete JavaScript Implementation

```javascript
const ThemeManager = {
  init: function() {
    // Load saved theme or detect system preference
    const savedTheme = this.loadTheme();
    const effectiveTheme = savedTheme === 'auto' ? this.getSystemTheme() : savedTheme;
    this.applyTheme(effectiveTheme);
    
    // Setup toggle button
    const toggle = document.getElementById('theme-toggle');
    if (toggle) {
      toggle.addEventListener('click', () => this.toggleTheme());
    }
    
    // Listen for system theme changes
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (e) => {
      if (this.loadTheme() === 'auto') {
        this.applyTheme(e.matches ? 'dark' : 'light');
      }
    });
  },
  
  getSystemTheme: function() {
    if (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) {
      return 'dark';
    }
    return 'light';
  },
  
  loadTheme: function() {
    try {
      return localStorage.getItem('theme') || 'auto';
    } catch (e) {
      return 'auto';
    }
  },
  
  saveTheme: function(theme) {
    try {
      localStorage.setItem('theme', theme);
    } catch (e) {
      console.warn('Failed to save theme:', e);
    }
  },
  
  getCurrentTheme: function() {
    return document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light';
  },
  
  applyTheme: function(theme) {
    // Disable transitions temporarily
    document.documentElement.classList.add('theme-transitioning');
    
    // Apply theme
    if (theme === 'dark') {
      document.documentElement.setAttribute('data-theme', 'dark');
    } else {
      document.documentElement.removeAttribute('data-theme');
    }
    
    // Update toggle button
    this.updateToggleButton(theme);
    
    // Re-enable transitions
    setTimeout(() => {
      document.documentElement.classList.remove('theme-transitioning');
    }, 50);
  },
  
  toggleTheme: function() {
    const current = this.getCurrentTheme();
    const newTheme = current === 'light' ? 'dark' : 'light';
    this.applyTheme(newTheme);
    this.saveTheme(newTheme);
  },
  
  updateToggleButton: function(theme) {
    const toggle = document.getElementById('theme-toggle');
    if (!toggle) return;
    
    const lightIcon = toggle.querySelector('.light-icon');
    const darkIcon = toggle.querySelector('.dark-icon');
    
    if (theme === 'dark') {
      if (lightIcon) lightIcon.style.display = 'none';
      if (darkIcon) darkIcon.style.display = 'inline';
      toggle.setAttribute('aria-label', 'Switch to light mode');
    } else {
      if (lightIcon) lightIcon.style.display = 'inline';
      if (darkIcon) darkIcon.style.display = 'none';
      toggle.setAttribute('aria-label', 'Switch to dark mode');
    }
  }
};

// Initialize theme on page load
document.addEventListener('DOMContentLoaded', () => {
  ThemeManager.init();
});
```

### CSS Styling for Toggle Button

```css
.theme-toggle {
  background: var(--btn-secondary-bg);
  border: 1px solid var(--border-color);
  border-radius: 8px;
  padding: 8px 12px;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 8px;
  transition: background-color 0.2s ease;
}

.theme-toggle:hover {
  background: var(--btn-secondary-hover);
}

.theme-toggle:focus {
  outline: 2px solid var(--btn-primary-bg);
  outline-offset: 2px;
}

.theme-icon {
  font-size: 20px;
  line-height: 1;
}

.dark-icon {
  display: none;
}

[data-theme="dark"] .light-icon {
  display: none;
}

[data-theme="dark"] .dark-icon {
  display: inline;
}

/* Disable transitions during theme switch */
.theme-transitioning,
.theme-transitioning * {
  transition: none !important;
}
```

## Dependencies

### Browser APIs
- `localStorage`: Theme persistence
- `matchMedia`: System preference detection
- CSS Custom Properties: Theme variables

### No Server-Side Dependencies
This feature is entirely client-side and requires no ESP32 code changes.

## Rollout Plan

### Phase 1: Basic Light/Dark Toggle (MVP)
- Define CSS variables for both themes
- Implement theme toggle button
- Add localStorage persistence
- Update all existing UI elements to use CSS variables

### Phase 2: System Preference Support
- Add "auto" theme option
- Detect system preference
- Listen for system theme changes
- Add theme selector dropdown

### Phase 3: Enhanced Customization
- Add custom color picker
- Allow user-defined themes
- Add theme preview
- Export/import theme settings

## References

- [CSS Custom Properties](https://developer.mozilla.org/en-US/docs/Web/CSS/--*)
- [prefers-color-scheme](https://developer.mozilla.org/en-US/docs/Web/CSS/@media/prefers-color-scheme)
- [Web Storage API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Storage_API)
- [WCAG Color Contrast](https://www.w3.org/WAI/WCAG21/Understanding/contrast-minimum.html)
