# Design Spec: Zero-Wait WiFi (Parallel Race) - v2.0

## Goal
Achieve sub-500ms WiFi connection times by pre-loading BSSID, Channel, and Static IP state, while maintaining 100% reliability via an aggressive DNS-based "Sanity Check" and a clean DHCP fallback.

## Architecture

### 1. Atomic Network Snapshot (Struct)
Instead of multiple NVS keys, we use a single binary blob for atomicity and flash longevity.
```cpp
struct WiFiSnapshot {
  char ssid[33];
  uint8_t bssid[6];
  uint8_t channel;
  uint32_t ip;
  uint32_t gateway;
  uint32_t subnet;
  uint32_t dns;
  uint32_t crc; // For data integrity
};
```

### 2. The Launcher Sequence
1. **Load Snapshot**: Load `WiFiSnapshot` from NVS and verify CRC.
2. **Pre-Injection**: If target SSID matches snapshot:
   - Call `WiFi.config(snapshot.ip, snapshot.gateway, snapshot.subnet, snapshot.dns)`.
   - Call `WiFi.begin(ssid, pass, snapshot.channel, snapshot.bssid)`.
3. **The Race**:
   - **Case A: Snapshot Valid**: Link established, DNS Probe passes in ~100ms. Total time: <500ms.
   - **Case B: Snapshot Stale**: Link established, but DNS Probe fails or IP Conflict detected.
4. **Clean DHCP Fallback**:
   - If Probe fails: `WiFi.disconnect()`, `WiFi.config(INADDR_NONE, ...)` and restart DHCP.

### 3. Verification & Commitment
The snapshot is only committed to NVS after a successful HTTPS telemetry upload, ensuring only "known-good" network segments are cached.

## Success Criteria
- **Sub-500ms** association and reachability in cached environments.
- **Aggressive Recovery**: Stale segments detected and DHCP started within **750ms**.
