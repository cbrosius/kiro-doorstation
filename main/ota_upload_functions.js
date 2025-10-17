// ===== Task 10: OTA Update Functions =====

/**
 * Load OTA firmware information
 */
async function loadOTAInfo() {
  try {
    const response = await apiRequest('/api/ota/info', { silentError: true });

    if (response) {
      // Update current firmware version
      const versionElement = document.getElementById('ota-current-version');
      if (versionElement) {
        versionElement.textContent = response.version || 'Unknown';
      }

      // Update build date
      const buildDateElement = document.getElementById('ota-build-date');
      if (buildDateElement) {
        buildDateElement.textContent = response.build_date || 'Unknown';
      }

      // Update ESP-IDF version
      const idfVersionElement = document.getElementById('ota-idf-version');
      if (idfVersionElement) {
        idfVersionElement.textContent = response.idf_version || 'Unknown';
      }

      // Update rollback button state
      const rollbackBtn = document.getElementById('ota-rollback-btn');
      const rollbackInfo = document.getElementById('ota-rollback-info');
      if (rollbackBtn && rollbackInfo) {
        if (response.can_rollback) {
          rollbackBtn.disabled = false;
          rollbackInfo.style.display = 'none';
        } else {
          rollbackBtn.disabled = true;
          rollbackInfo.style.display = 'block';
        }
      }

      console.log('OTA info loaded successfully');
    }
  } catch (error) {
    console.error('Error loading OTA info:', error);
  }
}

/**
 * Setup OTA file input handlers
 */
function setupOTAFileHandlers() {
  const fileInput = document.getElementById('ota-file-input');
  const dropZone = document.getElementById('ota-drop-zone');
  const uploadBtn = document.getElementById('ota-upload-btn');

  if (!fileInput || !dropZone) return;

  // File input change handler
  fileInput.addEventListener('change', (e) => {
    if (e.target.files && e.target.files.length > 0) {
      handleOTAFileSelected(e.target.files[0]);
    }
  });

  // Drag and drop handlers
  dropZone.addEventListener('dragover', (e) => {
    e.preventDefault();
    dropZone.classList.add('dragging');
  });

  dropZone.addEventListener('dragleave', (e) => {
    e.preventDefault();
    dropZone.classList.remove('dragging');
  });

  dropZone.addEventListener('drop', (e) => {
    e.preventDefault();
    dropZone.classList.remove('dragging');

    if (e.dataTransfer.files && e.dataTransfer.files.length > 0) {
      handleOTAFileSelected(e.dataTransfer.files[0]);
    }
  });

  // Click to browse
  dropZone.addEventListener('click', (e) => {
    // Don't trigger if clicking the button inside
    if (e.target.tagName !== 'BUTTON' && e.target.tagName !== 'SPAN') {
      fileInput.click();
    }
  });
}

/**
 * Handle OTA file selection
 * @param {File} file - Selected firmware file
 */
function handleOTAFileSelected(file) {
  // Validate file extension
  if (!file.name.endsWith('.bin')) {
    showToast('Invalid file type. Please select a .bin file', 'error');
    return;
  }

  // Validate file size (max 5MB)
  const maxSize = 5 * 1024 * 1024; // 5MB
  if (file.size > maxSize) {
    showToast('File too large. Maximum size is 5MB', 'error');
    return;
  }

  // Store file reference
  window.otaSelectedFile = file;

  // Display file info
  const fileInfo = document.getElementById('ota-file-info');
  const fileName = document.getElementById('ota-file-name');
  const fileSize = document.getElementById('ota-file-size');
  const uploadBtn = document.getElementById('ota-upload-btn');

  if (fileInfo && fileName && fileSize) {
    fileName.textContent = file.name;
    fileSize.textContent = `Size: ${formatBytes(file.size)}`;
    fileInfo.style.display = 'block';
  }

  // Enable upload button
  if (uploadBtn) {
    uploadBtn.disabled = false;
  }

  console.log('OTA file selected:', file.name, formatBytes(file.size));
}

/**
 * Clear selected OTA file
 */
function clearOTAFile() {
  window.otaSelectedFile = null;

  const fileInput = document.getElementById('ota-file-input');
  const fileInfo = document.getElementById('ota-file-info');
  const uploadBtn = document.getElementById('ota-upload-btn');

  if (fileInput) {
    fileInput.value = '';
  }

  if (fileInfo) {
    fileInfo.style.display = 'none';
  }

  if (uploadBtn) {
    uploadBtn.disabled = true;
  }
}

/**
 * Upload firmware with progress tracking and automatic restart
 */
async function uploadFirmware() {
  const file = window.otaSelectedFile;

  if (!file) {
    showToast('Please select a firmware file first', 'error');
    return;
  }

  // Confirm upload
  const confirmed = await showConfirmDialog(
    'Upload Firmware',
    `<strong>⚠️ Important Warnings:</strong><br><br>` +
    `• Do not disconnect power during the update<br>` +
    `• The device will restart automatically after upload<br>` +
    `• You will lose connection temporarily<br>` +
    `• The update process may take 1-2 minutes<br><br>` +
    `<strong>File:</strong> ${file.name} (${formatBytes(file.size)})<br><br>` +
    `Do you want to proceed with the firmware update?`,
    'Upload and Update',
    'Cancel',
    'warning'
  );

  if (!confirmed) {
    return;
  }

  const uploadBtn = document.getElementById('ota-upload-btn');
  const progressContainer = document.getElementById('ota-progress-container');
  const progressBar = document.getElementById('ota-progress-bar');
  const progressPercent = document.getElementById('ota-progress-percent');
  const uploadSpeed = document.getElementById('ota-upload-speed');
  const timeRemaining = document.getElementById('ota-time-remaining');
  const uploadStatus = document.getElementById('ota-upload-status');

  // Disable upload button
  if (uploadBtn) {
    setLoadingState(uploadBtn, true);
    uploadBtn.disabled = true;
  }

  // Show progress container
  if (progressContainer) {
    progressContainer.style.display = 'block';
  }

  // Reset progress
  if (progressBar) progressBar.style.width = '0%';
  if (progressPercent) progressPercent.textContent = '0%';
  if (uploadSpeed) uploadSpeed.textContent = 'Speed: --';
  if (timeRemaining) timeRemaining.textContent = 'Time remaining: --';
  if (uploadStatus) uploadStatus.textContent = 'Preparing upload...';

  try {
    // Create FormData
    const formData = new FormData();
    formData.append('firmware', file);

    // Track upload progress
    const startTime = Date.now();
    let lastLoaded = 0;
    let lastTime = startTime;

    // Create XMLHttpRequest for progress tracking
    const xhr = new XMLHttpRequest();

    // Upload progress handler
    xhr.upload.addEventListener('progress', (e) => {
      if (e.lengthComputable) {
        const percent = Math.round((e.loaded / e.total) * 100);
        const currentTime = Date.now();
        const timeDiff = (currentTime - lastTime) / 1000; // seconds
        const bytesDiff = e.loaded - lastLoaded;

        // Update progress bar
        if (progressBar) progressBar.style.width = percent + '%';
        if (progressPercent) progressPercent.textContent = percent + '%';

        // Calculate speed (bytes per second)
        if (timeDiff > 0.5) { // Update speed every 0.5 seconds
          const speed = bytesDiff / timeDiff;
          if (uploadSpeed) {
            uploadSpeed.textContent = `Speed: ${formatBytes(speed)}/s`;
          }

          // Calculate time remaining
          const remaining = e.total - e.loaded;
          const timeLeft = remaining / speed;
          if (timeRemaining && timeLeft > 0) {
            timeRemaining.textContent = `Time remaining: ${formatTime(timeLeft)}`;
          }

          lastLoaded = e.loaded;
          lastTime = currentTime;
        }

        // Update status message
        if (uploadStatus) {
          if (percent < 100) {
            uploadStatus.textContent = `Uploading firmware... ${percent}%`;
          } else {
            uploadStatus.textContent = 'Upload complete. Validating firmware...';
          }
        }
      }
    });

    // Load handler (upload complete, waiting for response)
    xhr.addEventListener('load', () => {
      if (xhr.status === 200) {
        try {
          const response = JSON.parse(xhr.responseText);

          if (response.success) {
            // Success! Show countdown and restart message
            if (uploadStatus) {
              uploadStatus.innerHTML = '<strong style="color: var(--color-success);">✅ Firmware update successful!</strong>';
            }

            showToast('Firmware uploaded successfully! Device will restart in 5 seconds...', 'success');

            // Show restart countdown
            showRestartCountdown();

          } else {
            // Upload failed
            const errorMsg = response.error || 'Upload failed';
            if (uploadStatus) {
              uploadStatus.innerHTML = `<strong style="color: var(--color-danger);">❌ ${errorMsg}</strong>`;
            }
            showToast('Firmware upload failed: ' + errorMsg, 'error');

            // Re-enable upload button
            if (uploadBtn) {
              setLoadingState(uploadBtn, false);
              uploadBtn.disabled = false;
            }
          }
        } catch (error) {
          console.error('Error parsing response:', error);
          if (uploadStatus) {
            uploadStatus.innerHTML = '<strong style="color: var(--color-danger);">❌ Invalid server response</strong>';
          }
          showToast('Error: Invalid server response', 'error');

          // Re-enable upload button
          if (uploadBtn) {
            setLoadingState(uploadBtn, false);
            uploadBtn.disabled = false;
          }
        }
      } else {
        // HTTP error
        if (uploadStatus) {
          uploadStatus.innerHTML = `<strong style="color: var(--color-danger);">❌ Upload failed (HTTP ${xhr.status})</strong>`;
        }
        showToast(`Upload failed: HTTP ${xhr.status}`, 'error');

        // Re-enable upload button
        if (uploadBtn) {
          setLoadingState(uploadBtn, false);
          uploadBtn.disabled = false;
        }
      }
    });

    // Error handler
    xhr.addEventListener('error', () => {
      if (uploadStatus) {
        uploadStatus.innerHTML = '<strong style="color: var(--color-danger);">❌ Network error during upload</strong>';
      }
      showToast('Network error during upload. Please try again.', 'error');

      // Re-enable upload button
      if (uploadBtn) {
        setLoadingState(uploadBtn, false);
        uploadBtn.disabled = false;
      }
    });

    // Abort handler
    xhr.addEventListener('abort', () => {
      if (uploadStatus) {
        uploadStatus.innerHTML = '<strong style="color: var(--color-warning);">⚠️ Upload cancelled</strong>';
      }
      showToast('Upload cancelled', 'warning');

      // Re-enable upload button
      if (uploadBtn) {
        setLoadingState(uploadBtn, false);
        uploadBtn.disabled = false;
      }
    });

    // Send request
    xhr.open('POST', '/api/ota/upload');
    xhr.send(formData);

  } catch (error) {
    console.error('Error uploading firmware:', error);
    if (uploadStatus) {
      uploadStatus.innerHTML = `<strong style="color: var(--color-danger);">❌ ${error.message}</strong>`;
    }
    showToast('Error uploading firmware: ' + error.message, 'error');

    // Re-enable upload button
    if (uploadBtn) {
      setLoadingState(uploadBtn, false);
      uploadBtn.disabled = false;
    }
  }
}

/**
 * Show restart countdown and automatic page reload
 */
function showRestartCountdown() {
  // Create countdown overlay
  const overlay = document.createElement('div');
  overlay.style.cssText = `
    position: fixed;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    background-color: rgba(0, 0, 0, 0.8);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 9999;
  `;

  const countdownBox = document.createElement('div');
  countdownBox.style.cssText = `
    background-color: var(--color-surface);
    border-radius: var(--border-radius-lg);
    padding: var(--spacing-xl);
    text-align: center;
    max-width: 500px;
    box-shadow: var(--shadow-xl);
  `;

  countdownBox.innerHTML = `
    <div style="font-size: 64px; margin-bottom: var(--spacing-md);">🔄</div>
    <h2 style="font-size: var(--font-size-2xl); font-weight: var(--font-weight-bold); color: var(--color-text); margin-bottom: var(--spacing-md);">
      Firmware Update Successful!
    </h2>
    <p style="font-size: var(--font-size-base); color: var(--color-text-secondary); margin-bottom: var(--spacing-lg);">
      Device is restarting with new firmware...
    </p>
    <div style="font-size: 48px; font-weight: var(--font-weight-bold); color: var(--color-primary); margin-bottom: var(--spacing-md);">
      <span id="restart-countdown">5</span>s
    </div>
    <p style="font-size: var(--font-size-sm); color: var(--color-text-secondary);">
      Page will reload automatically when device is ready
    </p>
  `;

  overlay.appendChild(countdownBox);
  document.body.appendChild(overlay);

  // Countdown from 5 to 0
  let countdown = 5;
  const countdownElement = document.getElementById('restart-countdown');

  const countdownInterval = setInterval(() => {
    countdown--;
    if (countdownElement) {
      countdownElement.textContent = countdown;
    }

    if (countdown <= 0) {
      clearInterval(countdownInterval);

      // Change message to "waiting for device"
      countdownBox.innerHTML = `
        <div style="font-size: 64px; margin-bottom: var(--spacing-md); animation: spin 2s linear infinite;">⏳</div>
        <h2 style="font-size: var(--font-size-2xl); font-weight: var(--font-weight-bold); color: var(--color-text); margin-bottom: var(--spacing-md);">
          Waiting for Device...
        </h2>
        <p style="font-size: var(--font-size-base); color: var(--color-text-secondary); margin-bottom: var(--spacing-lg);">
          Device is restarting. This may take up to 30 seconds.
        </p>
        <p style="font-size: var(--font-size-sm); color: var(--color-text-secondary);">
          Attempting to reconnect...
        </p>
      `;

      // Start polling for device availability
      startDeviceReconnectPolling();
    }
  }, 1000);
}

/**
 * Poll device for availability and reload page when ready
 */
function startDeviceReconnectPolling() {
  let attempts = 0;
  const maxAttempts = 60; // 60 attempts = 30 seconds (polling every 500ms)

  const pollInterval = setInterval(async () => {
    attempts++;

    try {
      // Try to fetch system state
      const response = await fetch('/api/system/state', {
        method: 'GET',
        cache: 'no-cache',
        signal: AbortSignal.timeout(2000) // 2 second timeout
      });

      if (response.ok) {
        // Device is back online!
        clearInterval(pollInterval);

        // Show success message briefly
        const overlay = document.querySelector('div[style*="z-index: 9999"]');
        if (overlay) {
          const countdownBox = overlay.querySelector('div');
          if (countdownBox) {
            countdownBox.innerHTML = `
              <div style="font-size: 64px; margin-bottom: var(--spacing-md);">✅</div>
              <h2 style="font-size: var(--font-size-2xl); font-weight: var(--font-weight-bold); color: var(--color-success); margin-bottom: var(--spacing-md);">
                Device Online!
              </h2>
              <p style="font-size: var(--font-size-base); color: var(--color-text-secondary);">
                Reloading page...
              </p>
            `;
          }
        }

        // Reload page after brief delay
        setTimeout(() => {
          window.location.reload();
        }, 1000);
      }
    } catch (error) {
      // Device not ready yet, continue polling
      console.log(`Reconnect attempt ${attempts}/${maxAttempts}...`);
    }

    // Give up after max attempts
    if (attempts >= maxAttempts) {
      clearInterval(pollInterval);

      const overlay = document.querySelector('div[style*="z-index: 9999"]');
      if (overlay) {
        const countdownBox = overlay.querySelector('div');
        if (countdownBox) {
          countdownBox.innerHTML = `
            <div style="font-size: 64px; margin-bottom: var(--spacing-md);">⚠️</div>
            <h2 style="font-size: var(--font-size-2xl); font-weight: var(--font-weight-bold); color: var(--color-warning); margin-bottom: var(--spacing-md);">
              Connection Timeout
            </h2>
            <p style="font-size: var(--font-size-base); color: var(--color-text-secondary); margin-bottom: var(--spacing-lg);">
              Device is taking longer than expected to restart.
            </p>
            <button class="btn btn-primary" onclick="window.location.reload()">
              <span>🔄</span>
              <span>Reload Page Now</span>
            </button>
          `;
        }
      }
    }
  }, 500); // Poll every 500ms
}

/**
 * Handle OTA rollback
 */
async function handleOTARollback() {
  const confirmed = await showConfirmDialog(
    'Rollback Firmware',
    'This will rollback to the previous firmware version and restart the device. You will lose connection temporarily. Continue?',
    'Rollback and Restart',
    'Cancel',
    'warning'
  );

  if (!confirmed) {
    return;
  }

  const button = document.getElementById('ota-rollback-btn');
  if (button) setLoadingState(button, true);

  try {
    const response = await apiRequest('/api/ota/rollback', {
      method: 'POST'
    });

    if (response && response.success) {
      showToast('Rollback initiated. Device will restart...', 'success');

      // Show restart countdown
      showRestartCountdown();
    } else {
      showToast(response?.message || 'Rollback failed', 'error');
      if (button) setLoadingState(button, false);
    }
  } catch (error) {
    console.error('Error during rollback:', error);
    showToast('Error during rollback: ' + error.message, 'error');
    if (button) setLoadingState(button, false);
  }
}

/**
 * Format bytes to human-readable string
 * @param {number} bytes - Number of bytes
 * @returns {string} Formatted string
 */
function formatBytes(bytes) {
  if (bytes === 0) return '0 Bytes';

  const k = 1024;
  const sizes = ['Bytes', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));

  return Math.round(bytes / Math.pow(k, i) * 100) / 100 + ' ' + sizes[i];
}

/**
 * Format time in seconds to human-readable string
 * @param {number} seconds - Number of seconds
 * @returns {string} Formatted string
 */
function formatTime(seconds) {
  if (seconds < 60) {
    return Math.round(seconds) + 's';
  } else {
    const minutes = Math.floor(seconds / 60);
    const secs = Math.round(seconds % 60);
    return `${minutes}m ${secs}s`;
  }
}

/**
 * Initialize OTA section
 */
function initOTASection() {
  loadOTAInfo();
  setupOTAFileHandlers();
  console.log('OTA section initialized');
}
