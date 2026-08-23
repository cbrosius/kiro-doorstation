# Web UI Bug Fixes & Request Queue Implementation

## Context

Browser console output from `https://192.168.62.246` revealed three bug categories in the ESP32 SIP Door Station web interface:

1. **Endpoint mismatch** — Frontend calls `/api/ota/version`, backend registers `/api/ota/info` → `HTTP 404`
2. **Server overload** — `initApp()` fires ~13 concurrent HTTPS requests at startup, overwhelming the ESP32 → timeouts, `ERR_CONNECTION_RESET`, `ERR_NO_BUFFER_SPACE`
3. **Response field mismatches** — Frontend expects JSON field names/structures that don't match backend responses → silent data failures

## Decisions

- **Request queue**: Always queue (route every `apiRequest()` through `requestQueue.add()`)
- **Field mismatches**: Extend backend to return missing fields + fix frontend to use them

---

## Task 1: Implement Request Queue in `apiRequest()`

**File**: `components/web/index.html` (line ~4522)

Wrap the entire `apiRequest` body in `requestQueue.add()`:

```javascript
async function apiRequest(endpoint, options = {}, timeout = 10000) {
  return requestQueue.add(async () => {
    // ... existing apiRequest body unchanged ...
  });
}
```

**Why**: The `requestQueue` (lines 3860-3896) already exists but is unused. It serializes requests with 100ms delays. Routing all `apiRequest` calls through it prevents the 13-concurrent-request storm at startup. After startup, the queue is typically empty, so polling/user actions wait <100ms.

**Note**: Direct `fetch()` calls (OTA status polling at line 9465, documentation load at line 703, connectivity check at line 10924) are NOT routed through the queue. These are low-frequency and not part of the startup storm. They can be addressed in a follow-up if needed.

---

## Task 2: Fix OTA Endpoint Mismatch (Bug 1)

**File**: `components/web/index.html`

| Line | Current | Fix |
|------|---------|-----|
| 9216 | `apiRequest('/api/ota/version')` | `apiRequest('/api/ota/info')` |
| 9229 | `response.chip_model` | `response.idf_version` |

The backend `get_ota_info_handler` (api_ota.c:19-40) returns `version`, `build_date`, `idf_version`, `partition`, `app_size`, `can_rollback`. There is no `chip_model` field.

---

## Task 3: Fix Response Field Mismatches (Bug 3)

### 3a. SIP log array key

**File**: `components/web/index.html` line 5810

| Current | Fix |
|---------|-----|
| `response.entries` | `response.logs` |

Backend `get_sip_log_handler` (api_sip.c:188) returns `{ "logs": [...] }`.

Also update the empty-check on line 5810: `if (!response.logs || response.logs.length === 0)`.

### 3b. Network IP config — extend backend + fix frontend

**Backend** (`components/web/api_network.c`):

Current `get_network_ip_handler` returns only `ip_address`, `subnet_mask`, `gateway`. Extend to return:

```c
cJSON_AddStringToObject(root, "mode", info.dhcp ? "dhcp" : "static");
cJSON_AddStringToObject(root, "ip", info.ip_address);
cJSON_AddStringToObject(root, "subnet", info.netmask);
cJSON_AddStringToObject(root, "gateway", info.gateway);
cJSON_AddStringToObject(root, "dns1", info.dns1);
cJSON_AddStringToObject(root, "dns2", info.dns2);
```

This requires `wifi_connection_info_t` to carry DNS info. Check `wifi_manager.h` — if the struct doesn't have `dns1`/`dns2`, add them to the struct and populate from the DHCP/station info in `wifi_get_connection_info()`.

**Frontend** (`components/web/index.html`):

`loadIPConfiguration()` (line 6750) expects `response.ip`, `response.subnet`, `response.mode`, `response.dns1`, `response.dns2`. After the backend fix, these will match. No frontend change needed for field names.

### 3c. Email config — extend backend + fix frontend

**Backend** (`components/web/api_email.c`):

Current `get_email_config_handler` returns flat fields. The frontend expects a nested structure:

```json
{
  "smtp": { "server": "", "port": 587, "username": "", "password": "", "sender": "" },
  "reports": { "enabled": true, "recipient": "", "schedule": "daily", "time": "08:00", "day": "1", "day_of_month": "1", "include_status": true, "include_logs": true, "include_backup": true },
  "last_report": { "timestamp": 0, "success": false, "error": null }
}
```

**Backend changes** (`api_email.c`):

1. Add NVS storage for report config fields (namespace `email_config`):
   - `report_schedule` (string: daily/weekly/monthly)
   - `report_time` (string: HH:MM)
   - `report_day` (int: 0-6)
   - `report_day_of_month` (int: 1-31)
   - `include_status`, `include_logs`, `include_backup` (bool)
   - `last_report_timestamp` (u64)
   - `last_report_success` (bool)

2. Update `email_load_config()` to read these new fields.

3. Update `get_email_config_handler()` to return the nested JSON structure expected by the frontend.

4. Add a new POST handler or extend the existing one to save report config fields. Alternatively, create a new endpoint `/api/email/reports` for report-specific config.

**Frontend** (`components/web/index.html`):

`loadEmailConfiguration()` (line 8892) already expects the nested `response.smtp.*` and `response.reports.*` structure. After the backend fix, this will work. Verify the field mapping:

| Frontend expects | Backend should return |
|------------------|----------------------|
| `response.smtp.server` | `smtp_server` |
| `response.smtp.port` | `smtp_port` |
| `response.smtp.username` | `smtp_username` |
| `response.smtp.password` | (not stored in response for security — leave empty or add if needed) |
| `response.smtp.sender` | `smtp_sender` (use `smtp_username` or add a separate field) |
| `response.reports.enabled` | `enabled` |
| `response.reports.recipient` | `recipient_email` |
| `response.reports.schedule` | `report_schedule` |
| `response.reports.time` | `report_time` |
| `response.reports.day` | `report_day` |
| `response.reports.day_of_month` | `report_day_of_month` |
| `response.reports.include_status` | `include_status` |
| `response.reports.include_logs` | `include_logs` |
| `response.reports.include_backup` | `include_backup` |

### 3d. WiFi config — add `configured` field to backend

**Backend** (`components/web/api_wifi.c`):

Add to `get_wifi_config_handler`:

```c
cJSON_AddBoolToObject(root, "configured", config.ssid[0] != '\0');
```

**Frontend** (`components/web/index.html` line 6664): No change needed — `loadIPConfiguration` already checks `response.configured`.

---

## Task 4: Verify Auto-Refresh Intervals Don't Re-Overload

After implementing the always-on request queue, verify that the multiple `setInterval` timers (lines 4260, 5903, 6965, 6500, 5763, 5903) don't create a sustained high request rate. At most 6 intervals fire periodically, but they target only 5 distinct endpoints (`/api/hardware/state`, `/api/wifi/state`, `/api/sip/state`, `/api/system/state`, `/api/ntp/state`, `/api/sip/log`, `/api/hardware/events`). With the queue, even if multiple intervals fire simultaneously, requests are serialized.

The hardware log auto-refresh (line 11240) defaults to 5-second intervals via `toggleHWLogAutoRefresh`. This is fine with the queue.

---

## Validation Plan

1. **Build & flash** the firmware with backend changes (Tasks 3b, 3c, 3d)
2. **Open browser DevTools** → Network tab
3. **Load `https://192.168.62.246`** and verify:
   - No 404 errors (Task 2)
   - No `ERR_CONNECTION_RESET` / `ERR_NO_BUFFER_SPACE` / timeout errors (Task 1)
   - Requests are serialized (start times staggered by ~100ms)
   - All sections populate with data:
     - Dashboard: system info, NTP status, recent activity (Task 3a)
     - OTA section: firmware version, build date, IDF version (Task 2)
     - Network: IP config populates with mode/DNS (Task 3b)
     - Email: SMTP and report config populate (Task 3c)
     - WiFi: password placeholder shows saved state (Task 3d)
4. **Navigate to each section** and verify no console errors
5. **Test SIP connect/disconnect** buttons work
6. **Test WiFi scan** functionality

---

## Affected Files

| File | Changes |
|------|---------|
| `components/web/index.html` | Tasks 1, 2, 3a, 3b (frontend), 3c (frontend verify), 3d (none) |
| `components/web/api_network.c` | Task 3b (backend) |
| `components/web/api_email.c` | Task 3c (backend) |
| `components/web/api_wifi.c` | Task 3d (backend) |
| `components/wifi_manager.h` | Task 3b (add DNS fields to struct if missing) |

---

## Open Questions

- **Email report scheduling backend**: The email report scheduling UI exists in the frontend but the backend has no scheduler implementation. This plan only extends the API to store/return the config values — the actual scheduling logic is out of scope.
- **NVS schema change for email**: Adding new fields to the `email_config` NVS namespace is backward-compatible (new fields simply read as defaults if missing). No migration needed.
- **`wifi_connection_info_t` DNS fields**: Need to verify if this struct already has DNS fields. If not, they must be added and populated from `esp_netif_dns_info_t` in `wifi_get_connection_info()`.
