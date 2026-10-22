/*
  The logic of the SSwinGG recorder app, with no screen code in it, so that tools/check.mjs
  can test it on a computer. index.html does the Bluetooth and the screen.

  The packet is the kit's: 14 bytes, seven signed 16-bit numbers, low byte first:
  ax, ay, az, gx, gy, gz, temperature. See stages/08_sswingg_bat/swing_packet.h.
*/

export const SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
export const SENSOR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
export const NAME_PREFIX = "SwingLab";
const COUNTS_PER_G = 4096;      // the sensor's 8 g range
const COUNTS_PER_DPS = 16.4;    // the sensor's 2000 degrees per second range

export const SHOT_TYPES = ["pull_shot", "drive_shot", "cut_shot", "defensive_shot", "other", "unknown"];

export const CSV_COLUMNS = ["sample_index", "timestamp_iso", "timestamp_ms", "elapsed_ms",
  "ax_g", "ay_g", "az_g", "gx_dps", "gy_dps", "gz_dps", "accel_total_g", "speed_kmh",
  "ax_raw", "ay_raw", "az_raw", "gx_raw", "gy_raw", "gz_raw", "temp_raw"];

/** One packet to one reading, or null when it is too short to be one. */
export function decodePacket(view) {
  if (!view || view.byteLength < 14) return null;
  const raw = [0, 1, 2, 3, 4, 5, 6].map(i => view.getInt16(i * 2, true));
  const r = {
    ax_raw: raw[0], ay_raw: raw[1], az_raw: raw[2], gx_raw: raw[3], gy_raw: raw[4], gz_raw: raw[5], temp_raw: raw[6],
    ax_g: raw[0] / COUNTS_PER_G, ay_g: raw[1] / COUNTS_PER_G, az_g: raw[2] / COUNTS_PER_G,
    gx_dps: raw[3] / COUNTS_PER_DPS, gy_dps: raw[4] / COUNTS_PER_DPS, gz_dps: raw[5] / COUNTS_PER_DPS,
  };
  r.accel_total_g = Math.hypot(r.ax_g, r.ay_g, r.az_g);
  r.speed_kmh = speedKmh(r);
  return r;
}

/** Bat speed, the dashboard's formula: rotation at a point 0.85 m from the wrists, dead zone 5 deg/s, at most 180. */
export function speedKmh(r) {
  let rotation = Math.hypot(r.gx_dps, r.gy_dps, r.gz_dps);
  if (rotation < 5) rotation = 0;
  return Math.min(rotation * (Math.PI / 180) * 0.85 * 3.6, 180);
}

/*
  A first guess at the type of shot. It is NOT a trained or tested classifier: no labelled
  recordings exist yet. It uses the same side rule as the dashboard, rotation around z:
    slow swing, peak under 20 km/h            defensive_shot
    fast, turning to the leg side             pull_shot
    fast, turning to the off side             cut_shot
    fast, neither                             drive_shot
  Choose the type by hand when recording training data. The files named that way are what a
  real classifier would later be trained on.
*/
export function classify(samples) {
  if (samples.length < 10) return { label: "unknown", reason: `only ${samples.length} readings` };
  let peak = 0, peakAt = 0;
  samples.forEach((s, i) => { if (s.speed_kmh > peak) { peak = s.speed_kmh; peakAt = i; } });
  if (peak < 20) return { label: "defensive_shot", reason: `peak bat speed ${peak.toFixed(1)} km/h, under 20` };
  const t = samples[peakAt].elapsed_ms;
  const near = samples.filter(s => Math.abs(s.elapsed_ms - t) <= 100);    // 0.2 s around the peak
  const gz = near.reduce((sum, s) => sum + s.gz_dps, 0) / near.length;
  const why = `peak ${peak.toFixed(1)} km/h, rotation around z ${gz >= 0 ? "+" : ""}${gz.toFixed(0)} deg/s at the peak`;
  if (gz > 20) return { label: "pull_shot", reason: why + ", leg side" };
  if (gz < -20) return { label: "cut_shot", reason: why + ", off side" };
  return { label: "drive_shot", reason: why + ", straight" };
}

/** The recording as CSV: one header row, then one row per reading, every column of CSV_COLUMNS. */
export function toCsv(samples) {
  const cell = v => typeof v === "number" ? (Number.isInteger(v) ? String(v) : v.toFixed(4)) : v == null ? "" : `"${String(v).replace(/"/g, '""')}"`;
  return [CSV_COLUMNS.join(","), ...samples.map(s => CSV_COLUMNS.map(c => c === "timestamp_iso" ? s[c] : cell(s[c])).join(","))].join("\r\n") + "\r\n";
}

/** pull_shot_001.csv, pull_shot_002.csv, ... A simulated recording starts with sim_ so it is never mixed up with real data. */
export function fileName(label, number, simulated = false) {
  const safe = String(label).toLowerCase().replace(/[^a-z0-9_]+/g, "_");
  return `${simulated ? "sim_" : ""}${safe}_${String(number).padStart(3, "0")}.csv`;
}
