/**
 * OTA Firmware Update - Safety Warnings and User Guidance (Task 9)
 * 
 * This module implements comprehensive safety warnings and user guidance for the OTA update process:
 * - Warning about device restart during update
 * - Warning about not powering off device during update
 * - Estimated update duration based on file size
 * - Confirmation dialogs for destructive actions (update, rollback)
 * - Success message with countdown before automatic restart
 */

/**
 * Calculate estimated update duration based on file size
 * @param {number} fileSizeBytes - Size of firmware file in bytes
 * @returns {string} Formatted duration estimate
 */
function calculateUpdateDuration(fileSizeBytes) {
    // Typical upload speed over WiFi: 50-100 KB/s
    // We'll use conservative estimate of 50 KB/s
    const uploadSpeedKBps = 50;
    const fileSizeKB = fileSizeBytes / 1024;
    
    // Upload time + validation time (5s) + flash write time (10s) + restart time (5s)
    const uploadTimeSeconds = Math.ceil(fileSizeKB / uploadSpeedKBps);
    const totalTimeSeconds = uploadTimeSeconds + 20; // Add 20 seconds for validation, flashing, restart
    
    const minutes = Math.floor(totalTimeSeconds / 60);
    const seconds = totalTimeSeconds % 60;
    
    if (minutes > 0) {
        return `${minutes} minute${minutes !== 1 ? 's' : ''} ${seconds} second${seconds !== 1 ? 's' : ''}`;
    } else {
        return `${seconds} second${seconds !== 1 ? 's' : ''}`;
    }
}

/**
 * Format file size to human-readable string
 * @param {number} bytes - File size in bytes
 * @returns {string} Formatted file size
 */
function formatFileSize(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return Math.round((bytes / Math.pow(k, i)) * 100) / 100 + ' ' + sizes[i];
}

/**
 * Show comprehensive confirmation dialog for firmware update with safety warnings
 * @param {File} file - The firmware file to upload
 * @returns {Promise<boolean>} True if user confirms, false otherwise
 */
async function confirmFirmwareUpdate(file) {
    const fileSize = formatFileSize(file.size);
    const estimatedDuration = calculateUpdateDuration(file.size);
    
    const message = `
        <div style="text-align: left;">
            <p style="margin-bottom: var(--spacing-md);"><strong>Firmware File:</strong> ${file.name}</p>
            <p style="margin-bottom: var(--spacing-md);"><strong>File Size:</strong> ${fileSize}</p>
            <p style="margin-bottom: var(--spacing-lg);"><strong>Estimated Duration:</strong> ${estimatedDuration}</p>
            
            <div style="padding: var(--spacing-md); background-color: rgba(239, 83, 80, 0.1); border-left: 4px solid var(--color-danger); border-radius: var(--border-radius-sm); margin-bottom: var(--spacing-md);">
                <p style="margin: 0; margin-bottom: var(--spacing-sm);"><strong>⚠️ CRITICAL SAFETY WARNINGS:</strong></p>
                <ul style="margin: 0; padding-left: var(--spacing-lg); font-size: var(--font-size-sm);">
                    <li style="margin-bottom: var(--spacing-xs);">The device will <strong>restart automatically</strong> after the update</li>
                    <li style="margin-bottom: var(--spacing-xs);"><strong>DO NOT power off</strong> the device during the update process</li>
                    <li style="margin-bottom: var(--spacing-xs);"><strong>DO NOT disconnect</strong> the device from power or network</li>
                    <li style="margin-bottom: var(--spacing-xs);">The web interface will be <strong>temporarily unavailable</strong> during update</li>
                    <li>Loss of power during update may <strong>brick the device</strong></li>
                </ul>
            </div>
            
            <div style="padding: var(--spacing-md); background-color: rgba(212, 160, 23, 0.1); border-left: 4px solid var(--color-warning); border-radius: var(--border-radius-sm);">
                <p style="margin: 0; margin-bottom: var(--spacing-sm);"><strong>📋 Before You Continue:</strong></p>
                <ul style="margin: 0; padding-left: var(--spacing-lg); font-size: var(--font-size-sm);">
                    <li style="margin-bottom: var(--spacing-xs);">Ensure the device has a <strong>stable power supply</strong></li>
                    <li style="margin-bottom: var(--spacing-xs);">Verify you have a <strong>stable WiFi connection</strong></li>
                    <li style="margin-bottom: var(--spacing-xs);">Confirm this is the <strong>correct firmware file</strong> for your device</li>
                    <li>Have a backup plan in case the update fails</li>
                </ul>
            </div>
            
            <p style="margin-top: var(--spacing-lg); font-weight: var(--font-weight-medium);">Do you want to proceed with the firmware update?</p>
        </div>
    `;
    
    return await showConfirmDialog(
        '⚠️ Firmware Update Confirmation',
        message,
        'Yes, Update Firmware',
        'Cancel',
        'danger'
    );
}

/**
 * Show confirmation dialog for firmware rollback with safety warnings
 * @returns {Promise<boolean>} True if user confirms, false otherwise
 */
async function confirmFirmwareRollback() {
    const message = `
        <div style="text-align: left;">
            <p style="margin-bottom: var(--spacing-lg);">This will revert the device to the previous firmware version.</p>
            
            <div style="padding: var(--spacing-md); background-color: rgba(239, 83, 80, 0.1); border-left: 4px solid var(--color-danger); border-radius: var(--border-radius-sm); margin-bottom: var(--spacing-md);">
                <p style="margin: 0; margin-bottom: var(--spacing-sm);"><strong>⚠️ SAFETY WARNINGS:</strong></p>
                <ul style="margin: 0; padding-left: var(--spacing-lg); font-size: var(--font-size-sm);">
                    <li style="margin-bottom: var(--spacing-xs);">The device will <strong>restart immediately</strong> after rollback</li>
                    <li style="margin-bottom: var(--spacing-xs);"><strong>DO NOT power off</strong> the device during restart</li>
                    <li style="margin-bottom: var(--spacing-xs);">All settings will be preserved</li>
                    <li>The web interface will be temporarily unavailable</li>
                </ul>
            </div>
            
            <p style="margin-top: var(--spacing-md); font-weight: var(--font-weight-medium);">Do you want to rollback to the previous firmware?</p>
        </div>
    `;
    
    return await showConfirmDialog(
        '⚠️ Firmware Rollback Confirmation',
        message,
        'Yes, Rollback Firmware',
        'Cancel',
        'warning'
    );
}

/**
 * Display success message with countdown before automatic restart
 * @param {number} countdownSeconds - Number of seconds before restart
 * @returns {Promise<void>}
 */
async function showUpdateSuccessWithCountdown(countdownSeconds = 10) {
    return new Promise((resolve) => {
        const overlay = document.createElement('div');
        overlay.className = 'dialog-overlay dialog-success';
        
        const dialog = document.createElement('div');
        dialog.className = 'dialog';
        dialog.style.maxWidth = '600px';
        
        let remainingSeconds = countdownSeconds;
        
        const updateContent = () => {
            dialog.innerHTML = `
                <div class="dialog-header">
                    <span class="dialog-icon" style="color: var(--color-success);">✅</span>
                    <h3 class="dialog-title">Firmware Update Successful!</h3>
                </div>
                <div class="dialog-body">
                    <p style="margin-bottom: var(--spacing-lg);">
                        The firmware has been successfully uploaded and validated. The device will restart automatically to apply the new firmware.
                    </p>
                    
                    <div style="padding: var(--spacing-lg); background-color: var(--color-bg); border-radius: var(--border-radius); text-align: center; margin-bottom: var(--spacing-lg);">
                        <div style="font-size: var(--font-size-sm); color: var(--color-text-secondary); margin-bottom: var(--spacing-sm);">
                            Device restarting in:
                        </div>
                        <div style="font-size: 48px; font-weight: var(--font-weight-bold); color: var(--color-primary); font-family: monospace;">
                            ${remainingSeconds}
                        </div>
                        <div style="font-size: var(--font-size-sm); color: var(--color-text-secondary); margin-top: var(--spacing-sm);">
                            seconds
                        </div>
                    </div>
                    
                    <div style="padding: var(--spacing-md); background-color: rgba(41, 182, 246, 0.1); border-left: 4px solid var(--color-info); border-radius: var(--border-radius-sm);">
                        <p style="margin: 0; margin-bottom: var(--spacing-sm);"><strong>ℹ️ What happens next:</strong></p>
                        <ul style="margin: 0; padding-left: var(--spacing-lg); font-size: var(--font-size-sm);">
                            <li style="margin-bottom: var(--spacing-xs);">Device will restart with new firmware</li>
                            <li style="margin-bottom: var(--spacing-xs);">This page will attempt to reconnect automatically</li>
                            <li style="margin-bottom: var(--spacing-xs);">If reconnection fails, manually refresh the page</li>
                            <li>The restart process takes approximately 10-15 seconds</li>
                        </ul>
                    </div>
                    
                    <p style="margin-top: var(--spacing-lg); font-size: var(--font-size-sm); color: var(--color-text-secondary); text-align: center;">
                        <strong>DO NOT power off the device during restart</strong>
                    </p>
                </div>
                <div class="dialog-footer">
                    <button class="btn btn-secondary" onclick="window.location.reload()">
                        Reconnect Now
                    </button>
                </div>
            `;
        };
        
        updateContent();
        overlay.appendChild(dialog);
        document.body.appendChild(overlay);
        
        // Animate in
        setTimeout(() => overlay.classList.add('show'), 10);
        
        // Countdown timer
        const countdownInterval = setInterval(() => {
            remainingSeconds--;
            updateContent();
            
            if (remainingSeconds <= 0) {
                clearInterval(countdownInterval);
                
                // Show reconnecting message
                dialog.innerHTML = `
                    <div class="dialog-header">
                        <span class="dialog-icon" style="color: var(--color-info);">🔄</span>
                        <h3 class="dialog-title">Device Restarting...</h3>
                    </div>
                    <div class="dialog-body">
                        <div style="text-align: center; padding: var(--spacing-xl);">
                            <div class="btn-spinner" style="font-size: 48px; margin-bottom: var(--spacing-lg);">⏳</div>
                            <p style="font-size: var(--font-size-lg); margin-bottom: var(--spacing-md);">
                                Please wait while the device restarts...
                            </p>
                            <p style="font-size: var(--font-size-sm); color: var(--color-text-secondary);">
                                This page will reload automatically when the device is ready.
                            </p>
                        </div>
                    </div>
                `;
                
                // Attempt to reload after device restart time
                setTimeout(() => {
                    window.location.reload();
                }, 15000); // Wait 15 seconds for device to restart
                
                resolve();
            }
        }, 1000);
    });
}

/**
 * Display update progress with safety reminders
 * @param {number} percent - Progress percentage (0-100)
 * @param {string} status - Current status message
 */
function updateProgressDisplay(percent, status) {
    const progressBar = document.getElementById('ota-progress-fill');
    const progressText = document.getElementById('ota-progress-text');
    const statusText = document.getElementById('ota-status-text');
    const safetyReminder = document.getElementById('ota-safety-reminder');
    
    if (progressBar) {
        progressBar.style.width = `${percent}%`;
    }
    
    if (progressText) {
        progressText.textContent = `${percent}%`;
    }
    
    if (statusText) {
        statusText.textContent = status;
    }
    
    // Show safety reminder during critical phases
    if (safetyReminder) {
        if (percent > 0 && percent < 100) {
            safetyReminder.style.display = 'block';
        } else {
            safetyReminder.style.display = 'none';
        }
    }
}

/**
 * Show error message with recovery guidance
 * @param {string} errorMessage - The error message to display
 */
function showUpdateError(errorMessage) {
    const message = `
        <div style="text-align: left;">
            <p style="margin-bottom: var(--spacing-lg);">
                The firmware update failed with the following error:
            </p>
            
            <div style="padding: var(--spacing-md); background-color: rgba(239, 83, 80, 0.1); border-left: 4px solid var(--color-danger); border-radius: var(--border-radius-sm); margin-bottom: var(--spacing-lg);">
                <p style="margin: 0; font-family: monospace; font-size: var(--font-size-sm); color: var(--color-danger);">
                    ${errorMessage}
                </p>
            </div>
            
            <div style="padding: var(--spacing-md); background-color: rgba(41, 182, 246, 0.1); border-left: 4px solid var(--color-info); border-radius: var(--border-radius-sm);">
                <p style="margin: 0; margin-bottom: var(--spacing-sm);"><strong>🔧 Troubleshooting Steps:</strong></p>
                <ul style="margin: 0; padding-left: var(--spacing-lg); font-size: var(--font-size-sm);">
                    <li style="margin-bottom: var(--spacing-xs);">Verify the firmware file is correct for this device</li>
                    <li style="margin-bottom: var(--spacing-xs);">Check that the file is not corrupted</li>
                    <li style="margin-bottom: var(--spacing-xs);">Ensure you have a stable network connection</li>
                    <li style="margin-bottom: var(--spacing-xs);">Try uploading the firmware again</li>
                    <li>If the problem persists, contact support</li>
                </ul>
            </div>
            
            <p style="margin-top: var(--spacing-lg); font-size: var(--font-size-sm); color: var(--color-success);">
                ✅ <strong>Good news:</strong> Your current firmware is still intact and the device is functioning normally.
            </p>
        </div>
    `;
    
    showConfirmDialog(
        '❌ Firmware Update Failed',
        message,
        'OK',
        null,
        'danger'
    );
}

// Export functions for use in main application
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        calculateUpdateDuration,
        formatFileSize,
        confirmFirmwareUpdate,
        confirmFirmwareRollback,
        showUpdateSuccessWithCountdown,
        updateProgressDisplay,
        showUpdateError
    };
}
