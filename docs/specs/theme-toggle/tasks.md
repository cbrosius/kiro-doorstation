# Implementation Plan

- [ ] 1. Define CSS custom properties for light theme
  - Add `:root` CSS block in `main/index.html` embedded styles
  - Define background color variables (primary, secondary, tertiary, card, header)
  - Define text color variables (primary, secondary, tertiary)
  - Define border color variables
  - Define status color variables (success, warning, error, info)
  - Define button color variables (primary, secondary, hover states)
  - Define input element color variables
  - Define sidebar color variables
  - Define shadow variables
  - _Requirements: 11.6, 11.7_

- [ ] 2. Define CSS custom properties for dark theme
  - Add `[data-theme="dark"]` CSS block in `main/index.html` embedded styles
  - Define dark theme background colors
  - Define dark theme text colors
  - Define dark theme border colors
  - Define dark theme status colors
  - Define dark theme button colors
  - Define dark theme input colors
  - Define dark theme sidebar colors
  - Define dark theme shadows
  - _Requirements: 11.2, 11.6, 11.7_

- [ ] 3. Update existing CSS to use CSS variables
  - Replace hardcoded background colors with `var(--bg-*)` variables
  - Replace hardcoded text colors with `var(--text-*)` variables
  - Replace hardcoded border colors with `var(--border-*)` variables
  - Replace hardcoded button colors with `var(--btn-*)` variables
  - Replace hardcoded input colors with `var(--input-*)` variables
  - Replace hardcoded status indicator colors with `var(--status-*)` variables
  - Replace hardcoded sidebar colors with `var(--sidebar-*)` variables
  - Replace hardcoded shadows with `var(--shadow-*)` variables
  - _Requirements: 11.5, 11.7_

- [ ] 4. Create theme toggle button in web interface
  - Add theme toggle button to header area (or sidebar footer)
  - Create HTML structure with light and dark icons (☀️ and 🌙)
  - Add appropriate ARIA labels for accessibility
  - Style toggle button with CSS
  - Add hover and focus states
  - _Requirements: 11.1, 11.6_

- [ ] 5. Implement JavaScript ThemeManager object
  - Create `ThemeManager` object in `main/index.html` embedded JavaScript
  - Implement `init()` function to initialize theme on page load
  - Implement `getSystemTheme()` function using `prefers-color-scheme` media query
  - Implement `loadTheme()` function to read from localStorage
  - Implement `saveTheme()` function to write to localStorage
  - Implement `getCurrentTheme()` function to read current theme from DOM
  - Implement `applyTheme()` function to set `data-theme` attribute
  - Implement `toggleTheme()` function to switch between light and dark
  - Implement `updateToggleButton()` function to update button icon
  - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5_

- [ ] 6. Add system theme preference detection
  - Use `window.matchMedia('(prefers-color-scheme: dark)')` to detect system preference
  - Apply system theme on first load if no saved preference exists
  - Add event listener for system theme changes
  - Update theme automatically when system preference changes (if theme is set to "auto")
  - _Requirements: 11.4_

- [ ] 7. Implement theme persistence with localStorage
  - Save theme preference to localStorage when user toggles theme
  - Load saved theme preference on page load
  - Handle localStorage errors gracefully (fallback to system preference)
  - Validate loaded theme value (reset to default if invalid)
  - _Requirements: 11.3_

- [ ] 8. Add smooth theme transition effects
  - Add CSS transitions for background-color and color properties
  - Implement `.theme-transitioning` class to disable transitions during theme switch
  - Apply and remove `.theme-transitioning` class with 50ms delay
  - Ensure no visual flash during theme switch
  - _Requirements: 11.5_

- [ ] 9. Initialize theme on page load
  - Add `DOMContentLoaded` event listener
  - Call `ThemeManager.init()` on page load
  - Ensure theme is applied before page is visible (prevent flash)
  - Setup toggle button click event listener
  - _Requirements: 11.1, 11.3, 11.4_

- [ ] 10. Update toggle button icon based on current theme
  - Show sun icon (☀️) in light mode
  - Show moon icon (🌙) in dark mode
  - Update ARIA label based on current theme
  - Ensure icon changes immediately when theme switches
  - _Requirements: 11.1, 11.5_

- [ ]* 11. Verify color contrast ratios for accessibility
  - Check light theme text contrast (minimum 4.5:1 for normal text)
  - Check dark theme text contrast (minimum 4.5:1 for normal text)
  - Check button contrast ratios (minimum 3:1)
  - Check status indicator contrast ratios
  - Verify focus indicators are visible in both themes
  - Test with browser accessibility tools
  - _Requirements: 11.6_

- [ ]* 12. Test theme functionality across all pages
  - Test theme toggle on Dashboard page
  - Test theme toggle on SIP Settings page
  - Test theme toggle on Network Settings page
  - Test theme toggle on Hardware Settings page
  - Test theme toggle on Security Settings page
  - Test theme toggle on System Logs page
  - Verify all UI elements render correctly in both themes
  - Test theme persistence after page reload
  - Test system preference detection
  - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5, 11.7_
