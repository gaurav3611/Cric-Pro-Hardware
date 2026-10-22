#!/usr/bin/env node
/*
  Makes everything the board needs from its KiCad design, pcb/sswingg-bat.kicad_pro.

      node tools/make_pcb_files.mjs

  It needs KiCad 10. Set KICAD_CLI to the path of kicad-cli if it is not found by itself.

  1. Checks the design: ERC on the schematic, DRC on the board including that the board matches the
     schematic, and that the schematic is the circuit of diagrams/make_diagrams.py. Any error stops it.
  2. pcb/sswingg-bat-gerbers.zip   the Gerber and drill files to upload to a maker
  3. pcb/bom.csv                   the parts list, from the fields of the schematic
  4. pcb/sswingg-bat-3d.png        a picture of the board with its parts
  5. diagrams/pcb-top.svg, pcb-bottom.svg and pcb-schematic.svg, for the guides
  6. pcb/fab-manifest.json         a fingerprint of the design, so tools/check.mjs can tell when the files above are out of date
*/
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { copyFileSync, existsSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { homedir, tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const TOOLS = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(TOOLS, "..");
const PCB = join(ROOT, "pcb");
const NAME = "sswingg-bat";
const SCH = join(PCB, NAME + ".kicad_sch"), BOARD = join(PCB, NAME + ".kicad_pcb");
export const MANIFEST = join(PCB, "fab-manifest.json");

export function kicadCli() {
  const found = spawnSync("which", ["kicad-cli"], { encoding: "utf8" }).stdout?.trim();
  return [process.env.KICAD_CLI, found,
    "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli",
    join(homedir(), "Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"),
    "C:\\Program Files\\KiCad\\10.0\\bin\\kicad-cli.exe"].find(p => p && existsSync(p)) || null;
}

// The files a person edits in KiCad. The .kicad_prl file only holds window settings, so it is left out.
export function designFingerprint() {
  const files = [NAME + ".kicad_pro", NAME + ".kicad_sch", NAME + ".kicad_pcb", "sswingg.kicad_sym", "sym-lib-table", "fp-lib-table",
    ...readdirSync(join(PCB, "sswingg.pretty")).sort().map(f => "sswingg.pretty/" + f)];
  const h = createHash("sha256").update(readFileSync(fileURLToPath(import.meta.url)));
  for (const f of files) h.update(f).update(readFileSync(join(PCB, f)));
  return h.digest("hex");
}

function run(cli, args) {
  const r = spawnSync(cli, args, { encoding: "utf8" });
  return { ok: r.status === 0, out: (r.stdout || "") + (r.stderr || "") };
}

// How a pin of the schematic is named in NETS. J1 only holds the ends of the charger's leads, so it is skipped.
const PIN = {
  U1: f => "ESP." + f, U2: f => "IMU." + f, U3: f => "TP." + f, BT1: f => "BAT." + f,
  D1: f => ({ K: "D1.cathode", A: "D1.anode" })[f], SW1: f => ({ A: "SW.b", B: "SW.common", C: "SW.a" })[f],
  C2: (f, n) => (n === "1" ? "C2.+" : "C2.-"), R1: (f, n) => "R1." + n, R2: (f, n) => "R2." + n, C1: (f, n) => "C1." + n,
};

// Every problem of the design as one line each. Empty means: no ERC or DRC errors, and the same circuit as the drawings.
export function verifyDesign(cli) {
  const work = mkdtempSync(join(tmpdir(), "sswingg-pcb-"));
  const problems = [];
  try {
    // KiCad counts a board that differs from its schematic as a warning only. Here it counts as a problem, like an error.
    run(cli, ["sch", "erc", "--severity-all", "--format", "json", "-o", join(work, "erc.json"), SCH]);
    const erc = JSON.parse(readFileSync(join(work, "erc.json"), "utf8")).sheets.flatMap(s => s.violations).filter(v => v.severity === "error");
    run(cli, ["pcb", "drc", "--schematic-parity", "--severity-all", "--format", "json", "-o", join(work, "drc.json"), BOARD]);
    const drcReport = JSON.parse(readFileSync(join(work, "drc.json"), "utf8"));
    const drc = [...drcReport.violations.filter(v => v.severity === "error"), ...drcReport.unconnected_items, ...drcReport.schematic_parity];
    for (const [tool, list, where] of [["ERC", erc, "the schematic, Inspect > Electrical Rules Checker"], ["DRC", drc, "the board, Inspect > Design Rules Checker"]]) {
      if (list.length) problems.push(`${tool}: ${list.length} problem(s), see ${where}: ` + list.slice(0, 3).map(v => v.description).join("; "));
    }

    run(cli, ["sch", "export", "netlist", "--format", "kicadxml", "-o", join(work, "net.xml"), SCH]);
    const xml = readFileSync(join(work, "net.xml"), "utf8");
    const made = new Set();
    for (const [, name, body] of xml.matchAll(/<net code="\d+" name="([^"]+)"[^>]*>([\s\S]*?)<\/net>/g)) {
      if (name.startsWith("unconnected-")) continue;
      const pins = [...body.matchAll(/<node ref="([^"]+)" pin="([^"]+)"(?: pinfunction="([^"]*)")?/g)]
        .filter(([, ref]) => ref !== "J1")
        .map(([, ref, pin, fn = ""]) => (PIN[ref] ? PIN[ref](fn.replace(new RegExp(`_${pin}$`), ""), pin) : `${ref}.${pin}`));
      made.add(pins.sort().join(" "));
    }
    const nets = JSON.parse(spawnSync("python3", ["-c",
      "import json, sys; sys.path.insert(0, 'diagrams'); from make_diagrams import NETS; print(json.dumps(NETS))"],
      { cwd: ROOT, encoding: "utf8" }).stdout);
    const wanted = new Set(Object.values(nets).map(p => [...p].sort().join(" ")));
    for (const n of wanted) if (!made.has(n)) problems.push(`the schematic does not join ${n}`);
    for (const n of made) if (!wanted.has(n)) problems.push(`the schematic joins ${n}, which diagrams/make_diagrams.py does not`);
  } finally {
    rmSync(work, { recursive: true, force: true });
  }
  return problems;
}

function zip(dir, files, out) {
  const py = "import sys, zipfile\nwith zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED) as z:\n" +
    "    for f in sys.argv[3:]:\n        z.writestr(zipfile.ZipInfo(f, (1980, 1, 1, 0, 0, 0)), open(sys.argv[2] + '/' + f, 'rb').read())\n";
  const r = spawnSync("python3", ["-c", py, out, dir, ...files], { encoding: "utf8" });
  if (r.status !== 0) throw new Error(r.stderr);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const cli = kicadCli();
  if (!cli) {
    console.error("kicad-cli not found. Install KiCad 10, or set KICAD_CLI to the path of kicad-cli.");
    process.exit(1);
  }
  const problems = verifyDesign(cli);
  if (problems.length) {
    console.error("Not made, the design has problems:\n  " + problems.join("\n  "));
    process.exit(1);
  }
  console.log("design ok: no ERC or DRC errors, the board matches the schematic, the schematic is the circuit of the drawings");

  const work = mkdtempSync(join(tmpdir(), "sswingg-fab-"));
  const must = (args, what) => {
    const r = run(cli, args);
    if (!r.ok) throw new Error(`${what} failed:\n${r.out}`);
  };
  must(["pcb", "export", "gerbers", "--layers", "F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,Edge.Cuts",
    "--subtract-soldermask", "-o", work + "/", BOARD], "Gerber export");
  must(["pcb", "export", "drill", "--excellon-separate-th", "-o", work + "/", BOARD], "drill export");
  const fab = readdirSync(work).sort();
  zip(work, fab, join(PCB, NAME + "-gerbers.zip"));
  console.log(`pcb/${NAME}-gerbers.zip: ${fab.join(", ")}`);
  rmSync(work, { recursive: true, force: true });

  must(["sch", "export", "bom", "--fields", "Reference,Value,${QUANTITY},Search,Notes,Footprint",
    "--labels", "Ref,Value,Qty,Name to search for,Notes,Footprint", "--group-by", "Value,Footprint,Search,Notes",
    "-o", join(PCB, "bom.csv"), SCH], "BOM export");
  must(["pcb", "render", "--side", "top", "--quality", "high", "--width", "1100", "--height", "1400", "--zoom", "0.8",
    "-o", join(PCB, NAME + "-3d.png"), BOARD], "3D render");
  for (const [file, layers, mirror] of [["pcb-top.svg", "F.Cu,F.Silkscreen,Edge.Cuts", false], ["pcb-bottom.svg", "B.Cu,B.Silkscreen,Edge.Cuts", true]]) {
    must(["pcb", "export", "svg", "--mode-single", "--layers", layers, "--page-size-mode", "2", "--exclude-drawing-sheet",
      ...(mirror ? ["--mirror"] : []), "-o", join(ROOT, "diagrams", file), BOARD], "SVG export");
  }
  const sheet = mkdtempSync(join(tmpdir(), "sswingg-sch-"));
  must(["sch", "export", "svg", "-o", sheet, SCH], "schematic export");
  copyFileSync(join(sheet, NAME + ".svg"), join(ROOT, "diagrams", "pcb-schematic.svg"));
  rmSync(sheet, { recursive: true, force: true });

  writeFileSync(MANIFEST, JSON.stringify({ design: designFingerprint() }, null, 2) + "\n");
  console.log("made pcb/bom.csv, pcb/" + NAME + "-3d.png, diagrams/pcb-top.svg, pcb-bottom.svg, pcb-schematic.svg and pcb/fab-manifest.json");
}
