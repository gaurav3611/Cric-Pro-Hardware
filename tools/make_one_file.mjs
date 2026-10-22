#!/usr/bin/env node
/*
  Writes sswingg_bat_one_file/sswingg_bat_one_file.ino: the stage 8 firmware and its four
  helper files merged into one sketch, for people who want a single file to open or paste.

      node tools/make_one_file.mjs

  Run it after changing anything in stages/08_sswingg_bat. tools/check.mjs fails when the
  one-file sketch no longer matches.
*/
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const STAGE = join(ROOT, "stages", "08_sswingg_bat");
export const ONE_FILE = join(ROOT, "sswingg_bat_one_file", "sswingg_bat_one_file.ino");

/** A file without its #pragma once and #include lines, which the one file brings in once at the top. */
const body = name => readFileSync(join(STAGE, name), "utf8").split("\n").filter(l => !/^\s*#(pragma once|include)/.test(l)).join("\n").trim() + "\n";
const section = (n, title) => `// =====================================================================================\n// ${n}. ${title}\n// =====================================================================================\n`;

export function oneFile() {
  const main = readFileSync(join(STAGE, "08_sswingg_bat.ino"), "utf8");
  const headEnd = main.indexOf("*/") + 2;
  const before = "  Settings are in config.h. The sensor steps are in bmi160.h, the 14-byte packet is in\n  swing_packet.h and the battery arithmetic is in battery.h.";
  if (!main.includes(before)) throw new Error("The opening comment of 08_sswingg_bat.ino changed. Update tools/make_one_file.mjs.");
  const head = main.slice(0, headEnd).replace(before, "  This is the whole firmware in one file. The kit keeps the same code in five files:\n  08_sswingg_bat.ino, config.h, bmi160.h, swing_packet.h and battery.h.\n  Settings are in the first section below.");
  const rest = main.slice(headEnd).split("\n").filter(l => !/^\s*#include/.test(l)).join("\n").trim() + "\n";

  // The Arduino IDE declares every function before the first function in the file, so the types
  // those declarations use, BatteryState and SWING_PACKET_SIZE, are moved to the top.
  const enumText = body("battery.h").match(/enum BatteryState \{[\s\S]*?\};\n/)[0];
  const size = "const size_t SWING_PACKET_SIZE = 14;\n";
  return [
    head,
    "#include <Arduino.h>\n#include <Wire.h>\n#include <BLEDevice.h>\n#include <BLEServer.h>\n#include <BLEUtils.h>\n#include <BLE2902.h>\n#include <esp_sleep.h>\n#include <stdint.h>\n#include <stddef.h>\n",
    section(1, "Settings (config.h in the kit)") + body("config.h"),
    "// Types used by the functions further down. They come first, because the Arduino IDE\n// declares every function before the first one in the file.\n" + size + enumText,
    section(2, "Battery arithmetic (battery.h in the kit)") + body("battery.h").replace(enumText, ""),
    section(3, "The 14-byte Bluetooth packet (swing_packet.h in the kit)") + body("swing_packet.h").replace(size, ""),
    section(4, "The BMI160 sensor driver (bmi160.h in the kit)") + body("bmi160.h"),
    section(5, "The firmware (08_sswingg_bat.ino in the kit)") + rest,
  ].join("\n");
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  mkdirSync(dirname(ONE_FILE), { recursive: true });
  writeFileSync(ONE_FILE, oneFile());
  console.log("wrote " + ONE_FILE);
}
