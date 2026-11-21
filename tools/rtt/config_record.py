"""Version 1 configuration responses from the MCU."""

import hashlib
import math
import struct

CONFIG_TYPE = 10
CONFIG_REQUEST = struct.Struct("<cI")
CONFIG_HEADER = struct.Struct("<4B4H12B")
CONFIG_COEFFICIENTS = struct.Struct("<15f")
CONFIG_CONTROL = struct.Struct("<IIHH6f")
CONFIG_RATE_AXES = struct.Struct("<21f")
CONFIG_PAYLOAD_BYTES = 204
CONFIG_REPLY_BYTES = 4 + CONFIG_PAYLOAD_BYTES

MODES = {1: "flight", 2: "esc_calibration", 3: "flash_export", 4: "imu_calibration"}
IMU_CODES = (
    "accel_odr_code",
    "gyro_odr_code",
    "accel_range_code",
    "gyro_range_code",
    "accel_filter_code",
    "gyro_filter_code",
    "accel_power_code",
    "gyro_power_code",
    "accel_filter_perf_code",
    "gyro_filter_perf_code",
    "gyro_noise_perf_code",
    "advanced_power_save_code",
)
RATE_FIELDS = (
    "kp_s_per_rad",
    "ki_per_rad",
    "kd_s2_per_rad",
    "integral_limit",
    "output_limit",
    "d_filter_tau_s",
    "max_rate_radps",
)


def decode_config_reply(payload):
    """Return the echoed request ID and configuration with named SI fields."""
    if len(payload) != CONFIG_REPLY_BYTES:
        raise ValueError("invalid CONFIG length")
    request = int.from_bytes(payload[:4], "little")
    config = memoryview(payload)[4:]
    flags, mode, chip, axis_map, accel_odr, gyro_odr, accel_range, gyro_range, *codes = (
        CONFIG_HEADER.unpack_from(config)
    )
    if mode not in MODES or flags & 0xF0 or (flags & 6 and not flags & 1):
        raise ValueError("invalid CONFIG mode or flags")
    cal = CONFIG_COEFFICIENTS.unpack_from(config, 24)
    rate_us, timeout_us, mahony_hz, attitude_hz, *control = CONFIG_CONTROL.unpack_from(config, 84)
    axes = CONFIG_RATE_AXES.unpack_from(config, 120)
    if not all(math.isfinite(value) for value in (*cal, *control, *axes)):
        raise ValueError("nonfinite CONFIG coefficients")

    imu = None
    if flags & 1:
        if not all((chip, axis_map, accel_odr, gyro_odr, accel_range, gyro_range)):
            raise ValueError("invalid CONFIG IMU settings")
        imu = {
            "model": {0xD1: "BMI160", 0x24: "BMI270"}.get(chip, "unknown"),
            "chip_id": chip,
            "axis_map": axis_map,
            "accel_odr_hz": accel_odr,
            "gyro_odr_hz": gyro_odr,
            "accel_range_g": accel_range,
            "gyro_range_dps": gyro_range,
            "settings_source": "driver_configuration",
            "driver_codes": {
                name: None if code == 255 else code for name, code in zip(IMU_CODES, codes)
            },
            "calibration": {
                "source": "flash" if flags & 2 else "default",
                "frame": "FRD",
                "gyro_bias_radps": list(cal[:3]),
                "accel_enabled": bool(flags & 4),
                "accel_bias_mps2": list(cal[3:6]),
                "accel_matrix": [list(cal[i : i + 3]) for i in (6, 9, 12)],
            },
        }
        if codes[4] == 2:
            imu["accel_filter"] = "normal_avg4"
        if codes[5] == 2:
            imu["gyro_filter"] = "normal"

    rate = None
    if flags & 8:
        if not rate_us:
            raise ValueError("invalid CONFIG rate period")
        rate = {"period_us": rate_us, "frame": "FRD", "axes": {}}
        for index, axis in enumerate(("roll", "pitch", "yaw")):
            rate["axes"][axis] = dict(zip(RATE_FIELDS, axes[index * 7 : (index + 1) * 7]))
    return request, {
        "schema_version": 1,
        "config_sha256": hashlib.sha256(config).hexdigest(),
        "mode": MODES[mode],
        "imu": imu,
        "rate": rate,
        "imu_timeout_us": timeout_us or None,
        "mahony": (
            {
                "nominal_hz": mahony_hz,
                "kp_per_s": control[0],
                "ki_per_s2": control[1],
                "gravity_min_mps2": control[4],
                "gravity_max_mps2": control[5],
            }
            if mahony_hz
            else None
        ),
        "attitude": (
            {
                "nominal_hz": attitude_hz,
                "kp_per_s": control[2],
                "max_tilt_rad": control[3],
            }
            if attitude_hz
            else None
        ),
    }
