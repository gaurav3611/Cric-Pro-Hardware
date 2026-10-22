#!/usr/bin/env node
/*
  Turns the Markdown sources of the guide into the PDF files that are published.

      node tools/make_pdfs.mjs

  The README.md at the top of the kit stays as it is: it is the front page of the repository.
  Every other .md file becomes a .pdf of the same name beside it: the five guides in docs/
  and the page of each stage, stages/<stage>/README.pdf. All drawings are also collected in
  diagrams/diagrams.pdf, one per page, for printing.

  Of those other documents only the PDFs are kept in the repository. Their Markdown sources
  are ignored by git (see .gitignore), so this tool has nothing to do in a copy without them.

  It needs pandoc (Markdown to HTML) and Google Chrome (HTML to PDF). On a Mac: brew install pandoc.
  Set CHROME_PATH if Chrome is not in the usual place.

  Links are rewritten on the way: a link to another document points at its PDF in the
  repository, a link to a sketch or a drawing points at that file there, and a link to a
  heading on the same page stays a jump inside the PDF. tools/check.mjs fails when a PDF is
  older than its source.
*/
import { execFileSync, spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, normalize, relative, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const TOOLS = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(TOOLS, "..");
export const REPO = "https://github.com/gaurav3611/Cric-Pro-Hardware";
const CHROME = process.env.CHROME_PATH || "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
export const MANIFEST = join(TOOLS, "pdf-manifest.json");

/** Every Markdown source that is published as a PDF, as a path from the root of the kit. */
export function sources() {
  const stages = join(ROOT, "stages");
  return [...readdirSync(join(ROOT, "docs")).filter(f => f.endsWith(".md")).sort().map(f => "docs/" + f),
          ...readdirSync(stages).sort().map(s => `stages/${s}/README.md`)].filter(f => existsSync(join(ROOT, f)));
}
const drawings = () => readdirSync(join(ROOT, "diagrams")).filter(f => f.endsWith(".svg")).sort();
const sha = text => createHash("sha256").update(text).digest("hex");

/** What the PDFs were made from. tools/check.mjs compares this with the files as they are now. */
export function fingerprint() {
  const out = { tool: sha(readFileSync(fileURLToPath(import.meta.url))), drawings: sha(drawings().map(f => readFileSync(join(ROOT, "diagrams", f))).join("")), documents: {} };
  for (const source of sources()) out.documents[source] = sha(readFileSync(join(ROOT, source)));
  return out;
}

/** The line at the foot of every page: which document this is. */
const footer = title => (/sswingg/i.test(title) ? title : "SSwinGG bat sensor. " + title).replace(/"/g, "'");

const STYLE = title => `
  @page { size: A4; margin: 16mm 15mm 18mm 15mm;
    @bottom-left { content: "${footer(title)}"; font: 8pt Helvetica, Arial, sans-serif; color: #58675e; }
    @bottom-right { content: counter(page) " / " counter(pages); font: 8pt Helvetica, Arial, sans-serif; color: #58675e; } }
  * { box-sizing: border-box; }
  body { margin: 0; padding: 0 0.5mm; font: 10.5pt/1.5 "Helvetica Neue", Helvetica, Arial, sans-serif; color: #17221c; }
  h1 { font-size: 21pt; line-height: 1.2; margin: 0 0 10pt; }
  h2 { font-size: 14.5pt; margin: 20pt 0 6pt; padding-bottom: 3pt; border-bottom: 1px solid #c9d3c9; break-after: avoid; }
  h3 { font-size: 11.5pt; margin: 14pt 0 4pt; break-after: avoid; }
  p { margin: 0 0 7pt; } li { margin: 0 0 3pt; } ul, ol { margin: 0 0 7pt; padding-left: 18pt; } li > p { margin-bottom: 3pt; }
  a { color: #1565c0; text-decoration: none; }
  strong { font-weight: 700; }
  table { border-collapse: collapse; width: 100%; margin: 4pt 0 10pt; font-size: 9.5pt; line-height: 1.38; }
  th, td { border: 1px solid #c9d3c9; padding: 4pt 6pt; text-align: left; vertical-align: top; }
  th { background: #eef2ec; } tr { break-inside: avoid; } thead { display: table-header-group; }
  code { font: 9pt Menlo, Consolas, monospace; background: #eef2ec; padding: 0 2pt; border-radius: 2px; }
  pre { margin: 4pt 0 10pt; padding: 7pt 9pt; font-size: 7.3pt; line-height: 1.45; background: #f3f5f1; border: 1px solid #c9d3c9; border-radius: 4px; white-space: pre-wrap; word-break: break-all; break-inside: avoid; }
  pre code { font-size: 7.3pt; line-height: 1.42; background: none; padding: 0; }
  img { display: block; max-width: 100%; height: auto; margin: 6pt auto 10pt; border: 1px solid #dde4dc; border-radius: 4px; break-inside: avoid; }
  figure { margin: 0; } figcaption { display: none; }
  blockquote { margin: 0 0 8pt; padding: 6pt 10pt; border-left: 3px solid #1565c0; background: #f3f5f1; }
  blockquote p:last-child { margin-bottom: 0; }
`;

const escapeHtml = text => text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");

/** Where a relative link of a document should lead once the document is a PDF. */
function publishedLink(href, source) {
  if (/^(https?:|mailto:)/.test(href) || href.startsWith("#")) return href;
  const [path, heading] = href.split("#");
  const target = normalize(join(dirname(source), path)).replace(/\/$/, "");
  const onDisk = join(ROOT, target);
  // The front page is the one document that stays Markdown, so a link to one of its headings still works.
  if (target === "README.md") return `${REPO}/blob/main/README.md${heading ? "#" + heading : ""}`;
  if (target.endsWith(".md")) return `${REPO}/blob/main/${target.replace(/\.md$/, ".pdf")}`;
  if (existsSync(onDisk) && statSync(onDisk).isDirectory()) {
    return existsSync(join(onDisk, "README.md")) ? `${REPO}/blob/main/${target}/README.pdf` : `${REPO}/tree/main/${target}`;
  }
  return `${REPO}/blob/main/${target}`;
}

function toHtml(source) {
  const markdown = readFileSync(join(ROOT, source), "utf8");
  const title = (markdown.match(/^# (.+)$/m) || [, source])[1];
  let body = execFileSync("pandoc", ["-f", "gfm", "-t", "html5", "--wrap=none"], { input: markdown, encoding: "utf8" });
  body = body.replace(/href="([^"]*)"/g, (_, href) => `href="${escapeHtml(publishedLink(href.replace(/&amp;/g, "&"), source))}"`);
  body = body.replace(/src="([^"]*)"/g, (_, src) => /^https?:/.test(src) ? `src="${src}"` : `src="${pathToFileURL(join(ROOT, dirname(source), src)).href}"`);
  // In the published copy the documents are PDFs, so a visible "something.md" would name a file that is not there.
  // The front page keeps its name, README.md.
  body = body.replace(/>([^<]*)</g, (whole, text) => ">" + text.replace(/([\w./-]+)\.md\b/g, (name, stem) => stem === "README" ? name : stem + ".pdf") + "<");
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><title>${escapeHtml(title)}</title><style>${STYLE(title)}</style></head><body>${body}</body></html>`;
}

function drawingsHtml() {
  const pages = drawings().map(f => `<section><img src="${pathToFileURL(join(ROOT, "diagrams", f)).href}" alt=""></section>`).join("");
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><title>SSwinGG bat sensor: all drawings</title><style>
    @page { size: A4 landscape; margin: 8mm; }
    body { margin: 0; } section { height: 193mm; display: flex; align-items: center; justify-content: center; break-after: page; }
    section:last-child { break-after: auto; } img { max-width: 100%; max-height: 100%; }
  </style></head><body>${pages}</body></html>`;
}

function print(html, pdf, work) {
  const page = join(work, "page.html");
  writeFileSync(page, html);
  rmSync(pdf, { force: true });
  spawnSync(CHROME, ["--headless=new", "--disable-gpu", "--no-pdf-header-footer", `--print-to-pdf=${pdf}`, pathToFileURL(page).href], { stdio: "ignore", timeout: 120000 });
  if (!existsSync(pdf) || statSync(pdf).size < 2000) throw new Error("Chrome did not write " + relative(ROOT, pdf));
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const list = sources();
  if (list.length === 0) { console.log("No Markdown sources here. The published copy holds only the PDFs."); process.exit(0); }
  if (spawnSync("pandoc", ["--version"]).status !== 0) { console.error("pandoc was not found. On a Mac: brew install pandoc"); process.exit(1); }
  if (!existsSync(CHROME)) { console.error("Google Chrome was not found at " + CHROME + ". Set CHROME_PATH."); process.exit(1); }
  const work = mkdtempSync(join(tmpdir(), "sswingg-pdf-"));
  try {
    for (const source of list) {
      const pdf = join(ROOT, source.replace(/\.md$/, ".pdf"));
      print(toHtml(source), pdf, work);
      console.log(`${source.replace(/\.md$/, ".pdf").padEnd(40)} ${(statSync(pdf).size / 1024).toFixed(0)} kB`);
    }
    const all = join(ROOT, "diagrams", "diagrams.pdf");
    print(drawingsHtml(), all, work);
    console.log(`${"diagrams/diagrams.pdf".padEnd(40)} ${(statSync(all).size / 1024).toFixed(0)} kB`);
    writeFileSync(MANIFEST, JSON.stringify(fingerprint(), null, 2) + "\n");
    console.log(`made ${list.length + 1} PDFs`);
  } finally { rmSync(work, { recursive: true, force: true }); }
}
