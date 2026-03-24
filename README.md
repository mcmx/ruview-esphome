# RuView CSI — ESPHome External Component

WiFi CSI (Channel State Information) presence sensor for Home Assistant via ESPHome.

Detects human presence, breathing rate, heart rate, motion energy, and falls using WiFi signal perturbation — no cameras, no wearables.

## Requirements

- **Hardware:** ESP32-S3 (8 MB flash recommended; 4 MB works)
- **Framework:** ESP-IDF (not Arduino — CSI requires the ESP-IDF WiFi API)
- **ESPHome:** 2024.2.0 or later
- **sdkconfig:** `CONFIG_ESP_WIFI_CSI_ENABLED=y`

ESP32 (original) and ESP32-C3 are **not supported** — single-core chips cannot run the CSI DSP pipeline.

## Quick Start

1. Copy `components/ruview_csi/` into your ESPHome `components/` directory (or use a `type: local` external component path).

2. Create a YAML config — see `examples/edge_vitals_node.yaml` for a full setup, or start minimal with `examples/basic_udp_node.yaml`.

3. Make sure your `esp32:` block uses `esp-idf` and enables CSI:

```yaml
esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_ESP_WIFI_CSI_ENABLED: "y"
```

4. Compile and flash:

```bash
esphome compile your_node.yaml
esphome upload your_node.yaml
```

## Architecture

Two-layer design:

- **`ruview_core`** — Plain C/ESP-IDF library. CSI capture, ADR-018 serialization, UDP streaming, edge DSP, NVS config. No `app_main()`, no Wi-Fi ownership.
- **`ruview_csi`** — ESPHome component wrapper. YAML schema, codegen, lifecycle, entity publishing. Starts `ruview_core` only after ESPHome has connected Wi-Fi.

```
ESPHome (owns Wi-Fi, OTA, API, logger)
  |
  +-- ruview_csi (Component class, Python config)
        |
        +-- ruview_core (C library)
              |-- csi_collector    (CSI callback + ring buffer)
              |-- adr018_serializer (wire format)
              |-- udp_sender       (UDP transport)
              |-- edge_dsp         (presence, vitals, fall detection)
              |-- runtime_config   (NVS persistence)
```

## Configuration Reference

### Hub block (`ruview_csi:`)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `id` | ID | auto | Component instance ID |
| `node_id` | int (0-255) | `1` | Node identifier in ADR-018 frames |
| `target_ip` | string | *required if UDP on* | Aggregator IP address |
| `target_port` | int | `5005` | Aggregator UDP port |
| `wifi_channel` | int (0-14) | `0` | WiFi channel (0 = auto from connection) |
| `udp_raw_enabled` | bool | `true` | Send raw ADR-018 CSI frames over UDP |
| `udp_vitals_enabled` | bool | `true` | Send vitals packets over UDP |
| `edge_enabled` | bool | `true` | Enable edge DSP processing |
| `edge_tier` | int (0-2) | `1` | DSP tier: 0=raw, 1=basic, 2=full |
| `vital_interval_ms` | int (100-60000) | `1000` | Vitals packet send interval (ms) |
| `top_k_count` | int (1-8) | `3` | Number of top subcarriers for DSP |
| `presence_threshold` | float (0-10) | `0.0` | Presence threshold (0 = auto-calibrate) |
| `fall_threshold` | float (0-100) | `15.0` | Fall detection threshold |
| `load_runtime_from_nvs` | bool | `true` | Load saved config from NVS at boot |
| `persist_runtime_to_nvs` | bool | `false` | Persist runtime changes to NVS |
| `nvs_namespace` | string (1-15 chars) | `csi_cfg` | NVS namespace name |

### Sensors (`sensor:` platform)

| Key | Unit | Class | Description |
|-----|------|-------|-------------|
| `motion_energy` | — | measurement | Current motion energy level |
| `breathing_bpm` | BPM | measurement | Estimated breathing rate |
| `heart_rate_bpm` | BPM | measurement | Estimated heart rate |
| `rssi` | dBm | signal_strength | WiFi RSSI |
| `csi_frames_received` | — | total_increasing | Total CSI frames received |
| `adr018_frames_sent` | — | total_increasing | Total ADR-018 frames sent |
| `vitals_packets_sent` | — | total_increasing | Total vitals packets sent |

### Binary Sensors (`binary_sensor:` platform)

| Key | Device Class | Description |
|-----|-------------|-------------|
| `presence` | occupancy | Human presence detected |
| `fall_detected` | safety | Fall event triggered |

### Text Sensor (`text_sensor:` platform)

| Key | Description |
|-----|-------------|
| `state` | Component state: `idle`, `waiting_for_wifi`, `starting`, `running`, `error` |

### Number Entities (`number:` platform)

| Key | Range | Step | Description |
|-----|-------|------|-------------|
| `presence_threshold` | 0.0 - 10.0 | 0.01 | Live presence threshold tuning |
| `fall_threshold` | 0.0 - 100.0 | 0.1 | Live fall threshold tuning |

## Config Merge Order

1. Compile-time defaults
2. NVS values (if `load_runtime_from_nvs: true`)
3. YAML values (always override NVS)
4. Runtime Number entity changes
5. Persist to NVS (only if `persist_runtime_to_nvs: true`)

YAML is always authoritative at boot. NVS provides optional persistence of runtime-tuned values.

## UDP Wire Formats

### ADR-018 Raw CSI (magic `0xC5110001`)

20-byte header + raw I/Q payload. Header fields: magic, node_id, n_antennas, n_subcarriers, freq_mhz, sequence, rssi, noise_floor.

### Vitals Packet (magic `0xC5110002`)

32 bytes packed: magic, node_id, flags (presence/fall/motion bits), breathing_rate (BPM x 100), heartrate (BPM x 10000), rssi, n_persons, motion_energy, presence_score, timestamp_ms.

### Delta Compressed (magic `0xC5110003`)

XOR + RLE encoding for bandwidth reduction.

## Limitations

- Requires ESP-IDF framework (not Arduino)
- ESP32 (original) and ESP32-C3 not supported (single-core)
- CSI accuracy depends on environment, multipath, and placement
- Auto-calibration takes ~60 seconds (1200 frames at 20 Hz)
- Heart rate estimation is experimental and environment-dependent
- No camera, radar, or mmWave fusion in this component

## License

See `LICENSE` file.
