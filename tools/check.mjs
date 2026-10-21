#!/usr/bin/env node
/*
  Checks this kit without any hardware.

      node tools/check.mjs            the fast checks, well under a minute
      node tools/check.mjs --board    also compiles every sketch for the ESP32-C3 (needs the Arduino IDE
                                      or arduino-cli with the esp32 boards installed; takes several minutes)

  What it does
    1. Files in step     the copies of bmi160.h, swing_packet.h and battery.h are identical, every sketch
                         uses the same pins, and the Bluetooth ids match the web app
    2. Packet and battery arithmetic
                         swing_packet.h and battery.h are built for this computer and compared with
                         answers worked out separately here, and with the web app's own decoder
                         README.md and the 16 PDF documents are there, and no PDF is older than what it
                         was made from.
    3. Every sketch in the simulator
                         tools/sim runs each sketch against a pretend sensor, battery and dashboard.
                         What the sketch prints, sends and does with its light is checked.
    4. The phone app     its packet reading, shot guess, CSV and file names, on a computer
    5. With --board      every sketch compiles for the real chip

  It needs Node 22 or newer and a C++ compiler (c++, clang++ or g++).
  It cannot check soldering, real radio range or a real battery. Only the board can.
*/
import { execFileSync, spawnSync } from "node:child_process";
import { MANIFEST, fingerprint, sources } from "./make_pdfs.mjs";
import { ONE_FILE, oneFile } from "./make_one_file.mjs";
import { existsSync, mkdtempSync, readFileSync, readdirSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const TOOLS = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(TOOLS, "..");
const STAGES = join(ROOT, "stages");
const WORK = mkdtempSync(join(tmpdir(), "swinglab-check-"));
const withBoard = process.argv.includes("--board");

// The one list of pins. Every sketch, the drawings and the README must agree with it.
const PINS = { SDA_PIN: 3, SCL_PIN: 4, LED_PIN: 8, BUTTON_PIN: 9, BATTERY_PIN: 0 };
const SENSOR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";

let failed = 0, passed = 0;
function check(name, ok, detail = "") {
  if (ok) passed++; else failed++;
  console.log((ok ? "ok    " : "FAIL  ") + name + (ok || !detail ? "" : "\n        -> " + detail));
}
const equal = (name, actual, expected) => check(name, JSON.stringify(actual) === JSON.stringify(expected), `expected ${JSON.stringify(expected)} but was ${JSON.stringify(actual)}`);
const read = file => readFileSync(file, "utf8");
const stageDirs = readdirSync(STAGES).filter(d => existsSync(join(STAGES, d, d + ".ino"))).sort();
// README.md is the front page and is always there. Every other document is published as a PDF.
// The Markdown sources of those are kept out of the repository, so a downloaded copy has none,
// and the checks that read them are skipped there.
const hasSources = existsSync(join(ROOT, "docs", "how-it-works.md"));

// ------------------------------------------------------------------------------------------------
// The web app's own code, when its project is next to this folder. It is the other end of the wire.
// ------------------------------------------------------------------------------------------------
async function loadWebApp() {
  const candidates = [process.env.WEB_APP_DIR, join(ROOT, "..", "angular-cricket-frontend"), join(ROOT, "..", "Cric-Pro-FE")].filter(Boolean);
  const dir = candidates.find(d => existsSync(join(d, "src", "app", "sensor", "sensor-packet.ts")));
  const { stripTypeScriptTypes } = await import("node:module");
  if (!dir || !stripTypeScriptTypes) return null;
  const load = async file => {
    const warn = process.emitWarning; process.emitWarning = () => {};   // the type stripper announces that it is experimental
    try {
      const js = stripTypeScriptTypes(read(file).replace(/^import .*$/gm, ""));
      return await import("data:text/javascript;base64," + Buffer.from(js).toString("base64"));
    } finally { process.emitWarning = warn; }
  };
  const packetFile = join(dir, "src", "app", "sensor", "sensor-packet.ts");
  return { dir, packetSource: read(packetFile), packet: await load(packetFile), analysis: await load(join(dir, "src", "app", "analysis", "swing-analysis.ts")) };
}

// This file's own decoder and speed formula, written from the description of the packet, not copied from the firmware.
function decode(hex) {
  const view = new DataView(Uint8Array.from(hex.match(/../g).map(h => parseInt(h, 16))).buffer);
  const n = i => view.getInt16(i * 2, true);
  return { ax: n(0) / 4096, ay: n(1) / 4096, az: n(2) / 4096, gx: n(3) / 16.4, gy: n(4) / 16.4, gz: n(5) / 16.4, counts: [0, 1, 2, 3, 4, 5, 6].map(n), view };
}
function speedKmh(s) {
  let rotation = Math.hypot(s.gx, s.gy, s.gz);
  if (rotation < 5) rotation = 0;
  return Math.min(rotation * (Math.PI / 180) * 0.85 * 3.6, 180);
}

// ------------------------------------------------------------------------------------------------
// Building and running
// ------------------------------------------------------------------------------------------------
const compiler = ["c++", "clang++", "g++"].find(c => spawnSync(c, ["--version"]).status === 0);
function build(output, args) {
  const result = spawnSync(compiler, ["-std=c++17", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-function", ...args, "-o", output], { encoding: "utf8" });
  return { ok: result.status === 0, messages: (result.stderr || "").trim() };
}
const builtSims = new Map();
/** Runs one sketch in the simulator. Returns what it printed, sent, read and did with its light. */
function simulate(stage, scenario, { flags = [], env = {} } = {}) {
  const key = stage + " " + flags.join(" ");
  if (!builtSims.has(key)) {
    const binary = join(WORK, "sim-" + builtSims.size);
    const dir = join(STAGES, stage);
    const made = build(binary, ["-I", join(TOOLS, "sim", "include"), "-I", dir, `-DSKETCH="${join(dir, stage + ".ino")}"`, ...flags, join(TOOLS, "sim", "sim_main.cpp")]);
    check(`${stage}${flags.length ? " " + flags.join(" ") : ""} builds for the simulator without warnings`, made.ok && !made.messages, made.messages.split("\n").slice(0, 6).join("\n           "));
    builtSims.set(key, made.ok ? binary : null);
  }
  const binary = builtSims.get(key);
  if (!binary) return { out: "", packets: [], reads: new Map(), light: [] };
  const logs = { SIM_PACKET_LOG: join(WORK, "packets.log"), SIM_SENSOR_LOG: join(WORK, "sensor.log"), SIM_LIGHT_LOG: join(WORK, "light.log") };
  for (const file of Object.values(logs)) rmSync(file, { force: true });
  const out = execFileSync(binary, [scenario], { encoding: "utf8", env: { ...process.env, ...logs, ...env }, maxBuffer: 64 * 1024 * 1024 });
  const lines = file => existsSync(file) ? read(file).split("\n").filter(Boolean).map(l => l.split(" ")) : [];
  return {
    out,
    packets: lines(logs.SIM_PACKET_LOG).map(([at, uuid, hex]) => ({ at: +at, uuid, hex })),
    reads: new Map(lines(logs.SIM_SENSOR_LOG).map(([at, ...counts]) => [+at, counts.map(Number)])),   // gx gy gz ax ay az
    light: lines(logs.SIM_LIGHT_LOG).map(([at, on]) => ({ at: +at, on: on === "1" })),
  };
}
/** How many times the light came on between two moments. */
const flashes = (light, from, to) => light.filter(l => l.on && l.at >= from && l.at < to).length;

// ================================================================================================
const web = await loadWebApp();
console.log(web ? `Comparing with the web app in ${web.dir}` : "The web app project was not found next to this folder, so the comparison with its decoder is skipped.");
if (!compiler) { console.log("FAIL  no C++ compiler found (tried c++, clang++, g++)"); process.exit(1); }

// ------------------------------------------------------------------------------------------------
console.log("\n1. Files in step");
// ------------------------------------------------------------------------------------------------
for (const [header, stages] of [["bmi160.h", ["05_ble_stream", "08_swinglab_bat", "09_wifi_to_api"]], ["swing_packet.h", ["05_ble_stream", "08_swinglab_bat"]], ["battery.h", ["07_battery_level", "08_swinglab_bat"]]]) {
  const texts = stages.map(s => read(join(STAGES, s, header)));
  check(`${header} is the same file in ${stages.join(", ")}`, texts.every(t => t === texts[0]), "copy the changed one over the others");
}
{
  const wrong = [];
  for (const stage of stageDirs) {
    for (const file of readdirSync(join(STAGES, stage)).filter(f => /\.(ino|h)$/.test(f))) {
      const text = read(join(STAGES, stage, file));
      for (const [name, pin] of Object.entries(PINS)) {
        for (const m of text.matchAll(new RegExp(`(?:#define\\s+${name}\\s+|const int ${name}\\s*=\\s*)(-?\\d+)`, "g"))) {
          if (+m[1] !== pin) wrong.push(`${stage}/${file}: ${name} is ${m[1]}, the kit uses ${pin}`);
        }
      }
    }
  }
  check("every sketch uses the same pins", wrong.length === 0, wrong.join("; ") + ". Change them all, then PINS at the top of tools/check.mjs, the drawings and the README.");

  const readme = read(join(ROOT, "README.md"));
  const rows = [`\`GPIO${PINS.SDA_PIN}\` | \`SDA\``, `\`GPIO${PINS.SCL_PIN}\` | \`SCL\``, `\`GPIO${PINS.BATTERY_PIN}\` | middle of R1 and R2`];
  check("the connection table in README.md names the same pins", rows.every(r => readme.includes(r)), "missing: " + rows.filter(r => !readme.includes(r)).join(" ; "));
  const drawings = existsSync(join(ROOT, "diagrams", "make_diagrams.py")) ? read(join(ROOT, "diagrams", "make_diagrams.py")) : "";
  const wanted = `PINS = {"SDA": ${PINS.SDA_PIN}, "SCL": ${PINS.SCL_PIN}, "LED": ${PINS.LED_PIN}, "BUTTON": ${PINS.BUTTON_PIN}, "BATTERY": ${PINS.BATTERY_PIN}}`;
  check("the drawings are made with the same pins", drawings.includes(wanted), "diagrams/make_diagrams.py should contain: " + wanted);
}
{
  const config = read(join(STAGES, "08_swinglab_bat", "config.h")), stage5 = read(join(STAGES, "05_ble_stream", "05_ble_stream.ino"));
  const pick = (text, name) => (text.match(new RegExp(`#define ${name} "([^"]+)"`)) || [])[1];
  equal("stage 5 and stage 8 use the same Bluetooth ids and name", ["SERVICE_UUID", "SENSOR_CHARACTERISTIC_UUID", "DEVICE_NAME"].map(n => pick(stage5, n)), ["SERVICE_UUID", "SENSOR_CHARACTERISTIC_UUID", "DEVICE_NAME"].map(n => pick(config, n)));
  equal("the sensor value has the id this check looks for", pick(config, "SENSOR_CHARACTERISTIC_UUID"), SENSOR_UUID);
  if (web) {
    equal("the Bluetooth ids are the ones the web app asks for", [pick(config, "SERVICE_UUID"), pick(config, "SENSOR_CHARACTERISTIC_UUID")], [web.packet.BLE_SERVICE_UUID, web.packet.BLE_SENSOR_UUID]);
    check("the device name starts with the prefix the web app filters on", pick(config, "DEVICE_NAME").startsWith(web.packet.BLE_NAME_PREFIX), `${pick(config, "DEVICE_NAME")} does not start with ${web.packet.BLE_NAME_PREFIX}`);
    const driver = read(join(STAGES, "08_swinglab_bat", "bmi160.h"));
    const scale = (text, re) => parseFloat((text.match(re) || [])[1]);
    equal("counts per g and per degree per second match the web app", [scale(driver, /BMI160_ACC_LSB_PER_G = ([\d.]+)/), scale(driver, /BMI160_GYR_LSB_PER_DPS = ([\d.]+)/)],
      [scale(web.packetSource, /ACCEL_COUNTS_PER_G = ([\d.]+)/), scale(web.packetSource, /GYRO_COUNTS_PER_DPS = ([\d.]+)/)]);
  }
  // The soldering guide must list every wire and every part position of the layout the drawing script checked.
  const drawings = read(join(ROOT, "diagrams", "make_diagrams.py"));
  const links = [...drawings.matchAll(/^    \("([A-Z]\d+)", "([A-Z]\d+)", (?:RED|BLACK|ORANGE|BLUE|GREEN), /gm)].map(m => `| ${m[1]} | ${m[2]} |`);
  const guide = hasSources ? read(join(ROOT, "docs", "build-on-zero-pcb.md")) : "";
  if (hasSources) check(`the soldering guide lists all ${links.length} wires of the zero PCB layout`, links.length === 17 && links.every(l => guide.includes(l)), "missing: " + links.filter(l => !guide.includes(l)).join("  "));
  const holes = Object.fromEntries([...drawings.matchAll(/"((?:SW|D1|R1|R2|C1|TP)\.[^"]+)": "([A-Z]\d+)"/g)].map(m => [m[1], m[2]]));
  const parts = [["D1", "D1.cathode", "D1.anode"], ["SW1", "SW.b", "SW.common", "SW.a"], ["R1", "R1.1", "R1.2"], ["R2", "R2.1", "R2.2"], ["C1", "C1.1", "C1.2"], ["OUT+", "TP.OUT+"], ["OUT-", "TP.OUT-"]];
  const lost = parts.filter(([name, ...pins]) => !guide.split("\n").some(line => line.includes(name) && pins.every(p => line.includes(holes[p]))));
  if (hasSources) check("the soldering guide puts every part in the holes of the layout", Object.keys(holes).length === 13 && lost.length === 0, "check the rows for: " + lost.map(l => l[0]).join(", "));

  check("the one-file sketch is the stage 8 firmware as it is now", existsSync(ONE_FILE) && read(ONE_FILE) === oneFile(), "run: node tools/make_one_file.mjs");

  const wifi = read(join(STAGES, "09_wifi_to_api", "09_wifi_to_api.ino"));
  check("the WiFi sketch holds no real network name or password", wifi.includes('"YOUR_WIFI_NAME"') && wifi.includes('"YOUR_WIFI_PASSWORD"'), "put the placeholders back before sharing the file");
}

{
  // The published documents: five guides, one page per stage, and all drawings in one file. README.md is the front page.
  const made = JSON.parse(read(MANIFEST));
  const pdfs = [...Object.keys(made.documents).map(f => f.replace(/\.md$/, ".pdf")), "diagrams/diagrams.pdf"];
  const bad = pdfs.filter(f => !existsSync(join(ROOT, f)) || readFileSync(join(ROOT, f)).subarray(0, 5).toString() !== "%PDF-" || readFileSync(join(ROOT, f)).length < 20000);
  check(`all ${pdfs.length} PDF documents are there: six guides, a page for each of the ${stageDirs.length} stages, and the drawings`,
    pdfs.length === 16 && stageDirs.every(s => pdfs.includes(`stages/${s}/README.pdf`)) && bad.length === 0, "missing or not a PDF: " + bad.join(", "));
  const strays = ["README.pdf"].filter(f => existsSync(join(ROOT, f)));
  check("the front page exists once, as README.md", existsSync(join(ROOT, "README.md")) && strays.length === 0, "remove: " + strays.join(", "));
  if (!hasSources) {
    console.log("      The Markdown sources of the documents are not in this copy, so the checks that read them are skipped.");
  } else {
    // A PDF must not be older than what it was made from: its source, the drawings, or the tool that makes it.
    const now = fingerprint();
    const stale = [...new Set([...Object.keys(now.documents), ...Object.keys(made.documents)])].filter(f => now.documents[f] !== made.documents[f]);
    check("every PDF was made from the Markdown source, the drawings and the tool as they are now",
      stale.length === 0 && now.drawings === made.drawings && now.tool === made.tool && sources().length === Object.keys(made.documents).length,
      (stale.length ? "changed since: " + stale.join(", ") + ". " : now.drawings !== made.drawings ? "the drawings changed. " : "tools/make_pdfs.mjs changed. ") + "Run: node tools/make_pdfs.mjs");
  }
}
{
  // Every link from one document to another file, or to a heading, has to lead somewhere.
  // README.md is checked in every copy. The other documents are checked where their sources are.
  const documents = ["README.md", ...readdirSync(join(ROOT, "docs")).map(f => "docs/" + f), ...stageDirs.map(s => `stages/${s}/README.md`)].filter(f => f.endsWith(".md") && existsSync(join(ROOT, f)));
  const anchorsOf = text => new Set([...text.matchAll(/^#{1,6}\s+(.+)$/gm)].map(m => m[1].trim().toLowerCase().replace(/[^\w\- ]/g, "").replace(/ /g, "-")));
  const broken = [];
  for (const doc of documents) {
    const text = read(join(ROOT, doc));
    for (const m of text.matchAll(/\]\(([^)\s]+)\)/g)) {
      if (/^https?:/.test(m[1])) continue;
      const [path, anchor] = m[1].split("#");
      const target = path ? resolve(dirname(join(ROOT, doc)), path) : join(ROOT, doc);
      if (!existsSync(target)) { broken.push(`${doc}: ${m[1]}`); continue; }
      if (anchor && target.endsWith(".md") && !anchorsOf(read(target)).has(anchor)) broken.push(`${doc}: no heading for #${anchor} in ${path || doc}`);
    }
  }
  check(`${documents.length === 1 ? "README.md links" : "the " + documents.length + " documents link"} only to files and headings that exist`, broken.length === 0, broken.join("\n           "));
  // In the published copy README.md must not point at a Markdown file that is not published.
  // Only the guides and the stage pages are kept out (see .gitignore). Any other Markdown file is published.
  const toSources = [...read(join(ROOT, "README.md")).matchAll(/\]\(([^)\s#]+\.md)[^)]*\)/g)].map(m => m[1]).filter(f => /^(docs\/[^/]+|stages\/[^/]+\/README)\.md$/.test(f));
  check("README.md links to the PDFs, not to Markdown files that are left out of the repository", toSources.length === 0, toSources.join(", "));
  if (hasSources) check("every stage has its own page in the sources", stageDirs.length === 9 && stageDirs.every(s => existsSync(join(STAGES, s, "README.md"))));
}

// ------------------------------------------------------------------------------------------------
console.log("\n2. Packet and battery arithmetic");
// ------------------------------------------------------------------------------------------------
{
  const binary = join(WORK, "vectors");
  const made = build(binary, ["-I", join(STAGES, "08_swinglab_bat"), join(TOOLS, "tests", "vectors.cpp")]);
  check("swing_packet.h and battery.h build for this computer without warnings", made.ok && !made.messages, made.messages);
  const lines = made.ok ? execFileSync(binary, { encoding: "utf8" }).split("\n").filter(Boolean).map(l => l.trim().split(/\s+/)) : [];

  const clamp = v => Math.max(-32768, Math.min(32767, v));
  let packets = 0, wrongBytes = [], wrongDecode = [];
  for (const [kind, ...f] of lines.filter(l => l[0] === "packet")) {
    const n = f.slice(0, 16).map(Number), hex = f[16];
    const accel = n.slice(0, 3), gyro = n.slice(3, 6), bias = n.slice(6, 9), source = n.slice(9, 12), sign = n.slice(12, 15), temperature = n[15];
    // Worked out here from the description: pick the source axis, remove the still value, apply the sign, stay inside 16 bits.
    const values = [...[0, 1, 2].map(i => clamp(accel[source[i]] * sign[i])), ...[0, 1, 2].map(i => clamp((gyro[source[i]] - bias[source[i]]) * sign[i])), temperature];
    const view = new DataView(new ArrayBuffer(14));
    values.forEach((v, i) => view.setInt16(i * 2, v, true));
    const expected = [...new Uint8Array(view.buffer)].map(b => b.toString(16).padStart(2, "0")).join("");
    if (hex !== expected) wrongBytes.push(`${f.slice(0, 16).join(" ")} gave ${hex}, expected ${expected}`);
    if (web) {
      const s = web.packet.decodeSensorPacket(decode(hex).view);
      const want = { ax: values[0] / 4096, ay: values[1] / 4096, az: values[2] / 4096, gx: values[3] / 16.4, gy: values[4] / 16.4, gz: values[5] / 16.4 };
      if (JSON.stringify(s) !== JSON.stringify(want)) wrongDecode.push(hex);
    }
    packets++;
  }
  check(`${packets} packets, with turned axes, flipped signs and values at the limits, have exactly the expected bytes`, packets === 20 && wrongBytes.length === 0, wrongBytes[0]);
  if (web) check("the web app's decoder reads every one of them back to the numbers that went in", wrongDecode.length === 0, wrongDecode[0]);

  const battery = new Map(lines.filter(l => l[0] === "battery").map(([, mv, percent, state]) => [+mv, { percent: +percent, state: +state }]));
  const at = mv => battery.get(mv).percent;
  equal("battery percent at the points of the table", [4400, 4200, 4100, 4000, 3900, 3850, 3800, 3700, 3600, 3500, 3400, 3000, 0].map(at), [100, 100, 90, 80, 60, 50, 40, 20, 10, 5, 0, 0, 0]);
  const ordered = [...battery.keys()].filter(mv => mv >= 3000).sort((a, b) => a - b);
  check("battery percent never falls as the voltage rises, and stays between 0 and 100", ordered.every((mv, i) => at(mv) >= 0 && at(mv) <= 100 && (i === 0 || at(mv) >= at(ordered[i - 1]))));
  equal("battery states: not seen, empty, low, ok", [0, 999, 3440, 3450, 3590, 3600, 4200].map(mv => battery.get(mv).state), [0, 0, 1, 2, 2, 3, 3]);
  equal("pin voltage to battery voltage, with and without calibration", lines.filter(l => l[0] === "pin").map(l => +l[2]), [4200, 3774]);
}

// ------------------------------------------------------------------------------------------------
console.log("\n3. Every sketch in the simulator");
// ------------------------------------------------------------------------------------------------
{
  const r = simulate("01_blink", "button@1200:1100,end@2600");
  check("stage 1 blinks, prints, and sees the BOOT button", /tick\s+1\s+light ON\s+BOOT button not pressed/.test(r.out) && /BOOT button PRESSED/.test(r.out) && flashes(r.light, 0, 2600) === 3, r.out.slice(-200));
}
{
  const good = simulate("02_i2c_scan", "end@2500"), none = simulate("02_i2c_scan", "nosensor,end@2500");
  check("stage 2 finds the sensor at 0x68 and passes", good.out.includes("0x68 answered   chip id 0xD1") && good.out.includes("RESULT: PASS"));
  check("stage 2 fails with advice when no sensor answers", none.out.includes("RESULT: FAIL. Nobody answered") && !none.out.includes("PASS"));
}
{
  const r = simulate("03_imu_read", "type@5200:p,type@5600:p,type@5800:b,end@6200");
  equal("stage 3 passes all four still checks", (r.out.match(/ PASS$/gm) || []).length, 4);
  check("stage 3 wakes the sensor and reports both parts awake", r.out.includes("register 0x03 (power status) = 0x14"));
  // The printed bytes, turned into numbers here, must be the counts the sketch printed beside them.
  const rows = [...r.out.matchAll(/bytes ([0-9A-F]{2}) ([0-9A-F]{2})\s+=\s+(-?\d+) counts/g)].map(m => [new DataView(Uint8Array.from([parseInt(m[1], 16), parseInt(m[2], 16)]).buffer).getInt16(0, true), +m[3]]);
  check("stage 3 shows the byte order correctly, for " + rows.length + " printed values", rows.length === 12 && rows.every(([a, b]) => a === b), JSON.stringify(rows));
  check("stage 3 switches to Serial Plotter format and back", /^ax:[-\d.]+,ay:[-\d.]+,az:[-\d.]+,gx:/m.test(r.out) && r.out.trimEnd().split("\n").pop().startsWith("acceleration"));
  const fail = simulate("03_imu_read", "nosensor,end@4000");
  check("stage 3 sends you back to stage 2 when the sensor is missing", fail.out.includes("Go back to stage 2") && !fail.out.includes("PASS"));
}
{
  const swings = [{ at: 3000, len: 420, peak: 900, side: 1 }, { at: 5200, len: 380, peak: 1500, side: -1 }, { at: 7200, len: 300, peak: 2300, side: 0 }];
  const r = simulate("04_swing_speed", swings.map(s => `swing@${s.at}:${s.len}:${s.peak}:${s.side}:4`).join(",") + ",end@8600");
  const found = [...r.out.matchAll(/Swing (\d+):\s+peak speed\s+([\d.]+) km\/h.*mostly around (\w)\s+ended turning (.+)$/gm)].map(m => ({ speed: +m[2], axis: m[3], side: m[4] }));
  const limit = 32767 / 16.4;
  const expected = swings.map(s => {
    const sample = { ax: 0, ay: 0, az: 1, gx: Math.max(-limit, -0.25 * s.peak), gy: Math.min(limit, s.peak), gz: s.side * (0.1 * s.peak + 3) };
    return web ? web.analysis.instantSpeedKmh(sample) : speedKmh(sample);
  });
  equal("stage 4 finds three swings, mostly around y, with their sides", found.map(f => f.axis + " " + f.side), ["y leg side", "y off side", "y straight"]);
  check("stage 4 peak speeds agree with " + (web ? "the web app's formula" : "the formula") + " to within 1%", found.length === 3 && found.every((f, i) => Math.abs(f.speed - expected[i]) / expected[i] < 0.01), JSON.stringify({ found: found.map(f => f.speed), expected: expected.map(e => +e.toFixed(1)) }));
  check("stage 4 says when the gyroscope hit its limit", (r.out.match(/hit its limit/g) || []).length === 1);
  check("stage 4 reads zero speed while still", /now\s+0\.0 km\/h\s+1\.00 g/.test(r.out));
}

/** The checks stage 5 and stage 8 share: what goes out over Bluetooth is what the sensor measured. */
function checkStream(label, r, { connected, quiet = [], swing, stillOffsets = 1 }) {
  const sensor = r.packets.filter(p => p.uuid === SENSOR_UUID);
  check(`${label}: every packet is 14 bytes`, sensor.length > 0 && sensor.every(p => p.hex.length === 28), "packets: " + sensor.length);
  check(`${label}: nothing is sent unless a dashboard is connected`, sensor.every(p => connected.some(([from, to]) => p.at >= from && p.at <= to + 25)), "a packet was sent outside " + JSON.stringify(connected));

  // Whole seconds inside a connection, away from moments when the sketch is busy with something else.
  const rates = [];
  for (const [from, to] of connected) {
    for (let t = Math.ceil(from / 1000) * 1000; t + 1000 <= to; t += 1000) {
      if (!quiet.some(([a, b]) => t < b && t + 1000 > a)) rates.push(sensor.filter(p => p.at >= t && p.at < t + 1000).length);
    }
  }
  check(`${label}: 50 packets in each of ${rates.length} full seconds`, rates.length > 0 && rates.every(n => n >= 49 && n <= 51), "per second: " + rates.join(" "));

  const offsets = new Set();
  let mismatched = 0, decodeDiffers = 0, restMoving = 0, peak = 0;
  for (const p of sensor) {
    const raw = r.reads.get(p.at);                    // gx gy gz ax ay az, as the pretend sensor gave them
    const d = decode(p.hex);
    if (!raw || d.counts[0] !== raw[3] || d.counts[1] !== raw[4] || d.counts[2] !== raw[5]) { mismatched++; continue; }
    offsets.add([0, 1, 2].map(i => raw[i] - d.counts[3 + i]).join(","));
    const sample = web ? web.packet.decodeSensorPacket(d.view) : d;
    if (web && ["ax", "ay", "az", "gx", "gy", "gz"].some(k => sample[k] !== d[k])) decodeDiffers++;
    const speed = web ? web.analysis.instantSpeedKmh(sample) : speedKmh(d);
    const inSwing = swing && p.at >= swing.at - 20 && p.at <= swing.at + swing.len + 20;
    if (inSwing) peak = Math.max(peak, speed); else if (speed !== 0) restMoving++;
  }
  check(`${label}: acceleration in every packet is exactly what the sensor measured at that instant`, mismatched === 0, mismatched + " packets differ");
  check(`${label}: rotation is the measurement minus a fixed still value (${[...offsets].join(" | ")} counts)`, offsets.size >= 1 && offsets.size <= stillOffsets &&
    [...offsets].every(o => { const [x, y, z] = o.split(",").map(Number); return Math.abs(x - 5) <= 2 && Math.abs(y + 8) <= 2 && Math.abs(z - 2) <= 2; }), "still values seen: " + [...offsets].join(" | "));
  if (web) check(`${label}: the web app's decoder gives the same numbers as this check's own`, decodeDiffers === 0);
  check(`${label}: a still bat shows a speed of exactly 0 in the web app's arithmetic`, restMoving === 0, restMoving + " packets showed a speed while still");
  if (swing) {
    const sample = { ax: 0, ay: 0, az: 1, gx: -0.25 * swing.peak, gy: swing.peak, gz: swing.side * (0.1 * swing.peak + 3) };
    const want = web ? web.analysis.instantSpeedKmh(sample) : speedKmh(sample);
    check(`${label}: the swing peaks at ${peak.toFixed(1)} km/h, within 1% of the ${want.toFixed(1)} expected`, Math.abs(peak - want) / want < 0.01);
  }
}
{
  const r = simulate("05_ble_stream", "connect@3000,swing@4200:400:1100:1,disconnect@6500,end@8000");
  checkStream("stage 5", r, { connected: [[3000, 6500]], swing: { at: 4200, len: 400, peak: 1100, side: 1 } });
  check("stage 5 advertises again after the dashboard leaves", r.out.includes("Disconnected. Advertising again."));
  check("stage 5 light: slow blink while waiting, steady while connected", flashes(r.light, 1100, 3000) === 2 && flashes(r.light, 3200, 6400) === 0, JSON.stringify(r.light.slice(0, 12)));
}
{
  const flash = join(WORK, "flash.txt");
  const first = simulate("06_battery_power", "end@61000", { env: { SIM_FLASH_FILE: flash } });
  check("stage 6 first start: one start, no brownout, a heartbeat every 2 seconds", /Starts so far\s+1\b/.test(first.out) && /Brownout resets\s+0\b/.test(first.out) && flashes(first.light, 10000, 20000) === 5, first.out.slice(0, 300));
  const second = simulate("06_battery_power", "type@3000:c,end@6000", { env: { SIM_FLASH_FILE: flash, SIM_RESET_REASON: "brownout" } });
  check("stage 6 remembers across a restart: second start, one brownout, previous run of one minute", /Starts so far\s+2\b/.test(second.out) && /Brownout resets\s+1\b/.test(second.out) && second.out.includes("Previous run     0 h 01 min 00 s"), second.out.slice(0, 400));
  check("stage 6 warns of a brownout with ten fast flashes, visible without a cable", flashes(second.light, 0, 1500) === 10, "flashes: " + flashes(second.light, 0, 1500));
  const third = simulate("06_battery_power", "end@1000", { env: { SIM_FLASH_FILE: flash } });
  check("stage 6 clears its counters when c is sent", second.out.includes("Counters cleared.") && /Starts so far\s+1\b/.test(third.out) && /Brownout resets\s+0\b/.test(third.out));
}
{
  const r = simulate("07_battery_level", "battery@0:0,battery@2500:4120,battery@4500:3560,battery@6500:3410,end@8600");
  check("stage 7 reports no battery, ok, low and empty at the right voltages", /no battery seen/.test(r.out) && /battery 4\.12 V\s+9[12]%\s+ok/.test(r.out) && /battery 3\.56 V\s+[78]%\s+low/.test(r.out) && /battery 3\.41 V\s+0%\s+EMPTY/.test(r.out), r.out.slice(-400));
}
{
  // The bat firmware, used the way it will be: moved while it calibrates, a swing, a loose wire, BOOT held, a reconnect.
  const r = simulate("08_swinglab_bat", "wobble@200:900,connect@4000,swing@5200:400:1100:1:3,unplug@7000:700,button@9000:1300,disconnect@12000,connect@13000,end@15000");
  checkStream("stage 8", r, { connected: [[4000, 12000], [13000, 15000]], quiet: [[7000, 8500], [9000, 11500]], swing: { at: 5200, len: 400, peak: 1100, side: 1 }, stillOffsets: 3 });
  check("stage 8 measures the still position again when the bat moved during it", r.out.includes("The bat moved. Trying again..."));
  check("stage 8 starts the sensor again after a wire came loose", r.out.includes("Too many failed reads. Starting the sensor again.") && (r.out.match(/BMI160 found at 0x68/g) || []).length === 2);
  check("stage 8 measures the still position again when BOOT is held for a second", (r.out.match(/^Still position:/gm) || []).length === 2);
  check("stage 8 accepts a second connection after the first one ended", r.packets.some(p => p.at > 13000));
  check("stage 8 does not mention a battery when the resistors are not fitted", !/battery/i.test(r.out));
  check("stage 8 light: slow blink while waiting, steady while connected, fast while measuring, blinking again after a disconnect",
    flashes(r.light, 3000, 4000) === 1 && flashes(r.light, 5000, 6900) === 0 && flashes(r.light, 10000, 11100) === 10 && r.light.some(l => !l.on && l.at > 12000 && l.at < 13000),
    JSON.stringify(r.light.filter(l => l.at > 2400).slice(0, 8)));
}
{
  const flags = ["-DHAS_BATTERY_SENSE=1"];
  const r = simulate("08_swinglab_bat", "battery@0:3950,connect@3000,battery@8000:3550,battery@20000:3400,end@60000", { flags });
  const levels = r.packets.filter(p => p.uuid === "2a19").map(p => parseInt(p.hex, 16));
  check("stage 8 with battery parts: reports the level at start", r.out.includes("Battery: 3.95 V, about 70%") || r.out.includes("Battery: 3.95 V, about 69%"), r.out.slice(0, 200));
  check(`stage 8 with battery parts: sends a falling level over Bluetooth (${levels.slice(0, 6).join(", ")}, ... ${levels.at(-1)})`, levels.length >= 5 && levels.every((v, i) => v <= 100 && (i === 0 || v < levels[i - 1])) && levels.at(-1) <= 2);
  const lowAt = (r.out.match(/battery 3\.5\d V \d+% LOW/) || { index: -1 }).index;
  check("stage 8 with battery parts: says LOW under 3.60 V and blinks three times every 2 seconds", lowAt > 0 && flashes(r.light, 16000, 18000) === 3 && flashes(r.light, 18000, 20000) === 3, "flashes: " + flashes(r.light, 16000, 18000));
  check("stage 8 with battery parts: an empty cell puts the bat to sleep, after five flashes", r.out.includes("Battery empty at") && r.out.includes("deep sleep") && !r.packets.some(p => p.at > 30000));
  const dead = simulate("08_swinglab_bat", "battery@0:3300,end@5000", { flags });
  check("stage 8 with battery parts: switched on with an empty cell, it sleeps before the radio starts", dead.out.includes("deep sleep") && !dead.out.includes("Bluetooth is on"));
  const usb = simulate("08_swinglab_bat", "battery@0:0,connect@3000,end@6000", { flags });
  check("stage 8 with battery parts: on USB with the switch off it runs normally and says the battery is not seen", usb.out.includes("Battery: not seen") && usb.packets.filter(p => p.uuid === SENSOR_UUID).length > 100 && !usb.out.includes("sleep"));
}

// ------------------------------------------------------------------------------------------------
console.log("\n4. The phone app's logic");
// ------------------------------------------------------------------------------------------------
{
  const app = await import(join(ROOT, "app", "recorder.js"));
  const view = hex => new DataView(Uint8Array.from(hex.match(/../g).map(h => parseInt(h, 16))).buffer);
  const r = app.decodePacket(view("35057203121040fd060b4c010000"));     // the example packet of diagrams/packet.svg
  equal("the app reads the example packet to the numbers the drawing shows", [r.ax_raw, r.gx_raw, r.gy_raw, r.gz_raw, r.ax_g.toFixed(3), r.gy_dps.toFixed(1), r.speed_kmh.toFixed(1)],
    [1333, -704, 2822, 332, "0.325", "172.1", "9.5"]);
  if (web) {
    const s = web.packet.decodeSensorPacket(view("35057203121040fd060b4c010000"));
    equal("the app and the web app read a packet the same way", [r.ax_g, r.ay_g, r.az_g, r.gx_dps, r.gy_dps, r.gz_dps, r.speed_kmh], [s.ax, s.ay, s.az, s.gx, s.gy, s.gz, web.analysis.instantSpeedKmh(s)]);
    equal("the app's Bluetooth ids are the ones the web app uses", [app.SERVICE_UUID, app.SENSOR_UUID, app.NAME_PREFIX], [web.packet.BLE_SERVICE_UUID, web.packet.BLE_SENSOR_UUID, web.packet.BLE_NAME_PREFIX]);
  }
  check("the app turns away a packet that is too short", app.decodePacket(new DataView(new ArrayBuffer(10))) === null);

  // A recording of n readings, 20 ms apart, with one swing in the middle: peak rotation around y, and a turn around z.
  const recording = (peak, gz, n = 100) => Array.from({ length: n }, (_, i) => {
    const k = i > 40 && i < 60 ? Math.sin((i - 40) / 20 * Math.PI) : 0;
    const v = new DataView(new ArrayBuffer(14));
    [0.02, 0.01, 1, -0.25 * peak * k, peak * k, gz * k].forEach((x, j) => v.setInt16(j * 2, Math.round(x * (j < 3 ? 4096 : 16.4)), true));
    return { sample_index: i, timestamp_iso: new Date(1e12 + i * 20).toISOString(), timestamp_ms: 1e12 + i * 20, elapsed_ms: i * 20, ...app.decodePacket(v) };
  });
  equal("the first guess at the shot type follows its rules: slow, leg side, off side, straight, too short",
    [recording(200, 0), recording(1100, 170), recording(1100, -170), recording(1100, 0), recording(1100, 170, 5)].map(x => app.classify(x).label),
    ["defensive_shot", "pull_shot", "cut_shot", "drive_shot", "unknown"]);

  const rows = app.toCsv(recording(1100, 170)).trim().split("\r\n").map(line => line.split(","));
  check("a recording's CSV has a header and one complete row per reading", rows[0].join(",") === app.CSV_COLUMNS.join(",") && rows.length === 101 && rows.every(row => row.length === app.CSV_COLUMNS.length) &&
    rows[1][1] === "2001-09-09T01:46:40.000Z" && rows[1][12] === "82", JSON.stringify(rows.slice(0, 2)));
  equal("file names: numbered per type, cleaned up, and marked when simulated", [app.fileName("pull_shot", 1), app.fileName("Pull Shot", 12), app.fileName("cut_shot", 3, true)],
    ["pull_shot_001.csv", "pull_shot_012.csv", "sim_cut_shot_003.csv"]);
}

// ------------------------------------------------------------------------------------------------
if (withBoard) {
  console.log("\n5. Every sketch compiles for the ESP32-C3 Super Mini");
  const cli = [process.env.ARDUINO_CLI, "arduino-cli", "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"].filter(Boolean).find(c => spawnSync(c, ["version"]).status === 0);
  if (!cli) {
    check("arduino-cli is available", false, "install the Arduino IDE, or set ARDUINO_CLI to the path of arduino-cli");
  } else {
    const compile = (stage, extra = []) => {
      const folder = stage === "swinglab_bat_one_file" ? join(ROOT, stage) : join(STAGES, stage);
      const result = spawnSync(cli, ["compile", "--fqbn", "esp32:esp32:nologo_esp32c3_super_mini", "--warnings", "all", "--build-path", join(WORK, "board-" + stage + extra.length), ...extra, folder], { encoding: "utf8" });
      const text = (result.stdout || "") + (result.stderr || "");
      const size = (text.match(/Sketch uses (\d+) bytes \((\d+)%\)/) || [])[2];
      const warnings = text.split("\n").filter(l => /warning:/.test(l));
      check(`${stage}${extra.length ? " with battery parts" : ""} compiles without warnings${size ? ", using " + size + "% of the program space" : ""}`, result.status === 0 && warnings.length === 0, (warnings[0] || text.split("\n").filter(l => /error/i.test(l))[0] || "").trim());
    };
    for (const stage of [...stageDirs, "swinglab_bat_one_file"]) compile(stage);
    compile("08_swinglab_bat", ["--build-property", "compiler.cpp.extra_flags=-DHAS_BATTERY_SENSE=1"]);
  }
} else {
  console.log("\n5. Compiling for the real chip was skipped. Run again with --board to include it.");
}

rmSync(WORK, { recursive: true, force: true });
console.log(`\n${failed === 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED"}: ${passed} passed, ${failed} failed`);
process.exitCode = failed === 0 ? 0 : 1;
