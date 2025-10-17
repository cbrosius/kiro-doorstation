# Logout Button Implementation

## Summary
Added a logout button to the web interface header that allows users to log out of the ESP32 SIP Door Station.

## Changes Made

### 1. HTML Structure (main/index.html)
Added a logout button in the header-actions section:
```html
<button class="logout-btn" id="logout-btn" aria-label="Logout">
  <span>🚪</span>
  <span>Logout</span>
</button>
```

### 2. CSS Styling (main/index.html)
Added styling for the logout button:
- Red background color (danger color) to indicate logout action
- Hover effects with slight elevation
- Responsive design: on mobile devices, only the icon is shown to save space
- Minimum touch target size of 44x44px for accessibility

```css
.logout-btn {
  display: inline-flex;
  align-items: center;
  gap: var(--spacing-xs);
  padding: var(--spacing-sm) var(--spacing-md);
  background-color: var(--color-danger);
  color: var(--color-text-inverse);
  border: none;
  border-radius: var(--border-radius-sm);
  font-size: var(--font-size-sm);
  font-weight: var(--font-weight-medium);
  cursor: pointer;
  transition: background-color var(--transition-fast), transform var(--transition-fast);
  min-height: 36px;
}
```

### 3. JavaScript Functionality (main/index.html)
Added `handleLogout()` function that:
- Sends a POST request to `/api/auth/logout`
- Clears the session on the server
- Redirects to the login page on success
- Shows error toast notification on failure

```javascript
async function handleLogout() {
  try {
    const response = await fetch('/api/auth/logout', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      }
    });
    
    if (response.ok) {
      window.location.href = '/login.html';
    } else {
      console.error('Logout failed:', response.statusText);
      showToast('Logout failed. Please try again.', 'error');
    }
  } catch (error) {
    console.error('Logout error:', error);
    showToast('Logout error. Please try again.', 'error');
  }
}
```

### 4. Event Listener Registration
Added event listener in the initialization code:
```javascript
const logoutBtn = document.getElementById('logout-btn');
if (logoutBtn) {
  logoutBtn.addEventListener('click', handleLogout);
}
```

## Backend API
The logout functionality uses the existing `/api/auth/logout` endpoint in `main/web_server.c`:
- Endpoint: `POST /api/auth/logout`
- Invalidates the user's session
- Clears the session cookie
- Returns success response

## User Experience
1. **Desktop**: Button shows door icon (🚪) and "Logout" text
2. **Mobile**: Button shows only the door icon to save space
3. **Hover**: Button darkens and slightly elevates
4. **Click**: Logs out user and redirects to login page
5. **Error Handling**: Shows toast notification if logout fails

## Accessibility
- Proper ARIA label for screen readers
- Minimum 44x44px touch target size
- High contrast color (red) to indicate important action
- Keyboard accessible (can be focused and activated with Enter/Space)

## Testing Recommendations
1. Test logout on desktop browsers
2. Test logout on mobile devices
3. Verify session is properly invalidated
4. Verify redirect to login page works
5. Test error handling by simulating network failure
6. Verify button is visible in both light and dark themes
