import fs from "node:fs";
import { createHash } from "node:crypto";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { execFileSync } from "node:child_process";
const source = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const args = process.argv.slice(2);
const output = path.resolve(args[args.indexOf("--out") + 1] ?? "");
if (!args.includes("--out") || output === source || source.startsWith(output + path.sep) || path.parse(output).root === output)
 throw new Error("Supply an isolated --out package directory.");
const config = JSON.parse(fs.readFileSync(path.join(source, "package.json"), "utf8"));
if (process.version !== `v${config.lmmsRuntimeVersion}`) throw new Error(`Packaging requires Node ${config.lmmsRuntimeVersion}; found ${process.version}`);
const marker = path.join(output, ".lmms-mcp-package");
if (fs.existsSync(output) && !fs.existsSync(marker) && fs.readdirSync(output).length) throw new Error("Refusing to reuse an unrelated directory.");
fs.mkdirSync(output, { recursive: true }); fs.writeFileSync(marker, "LMMS MCP generated package\n");
for (const child of ["dist", "licenses"]) {
 const target = path.join(output, child);
 if (!target.startsWith(output + path.sep)) throw new Error("Invalid package path");
 fs.rmSync(target, { recursive: true, force: true });
}
fs.cpSync(path.join(source, "dist", "src"), path.join(output, "dist", "src"), { recursive: true });
fs.copyFileSync(path.join(source, "package.json"), path.join(output, "package.json"));
fs.copyFileSync(path.join(source, "package-lock.json"), path.join(output, "package-lock.json"));
const npm = process.env.npm_execpath ?? path.join(path.dirname(process.execPath), "node_modules", "npm", "bin", "npm-cli.js");
execFileSync(process.execPath, [npm, "ci", "--omit=dev", "--ignore-scripts", "--no-audit", "--no-fund"], { cwd: output, stdio: "inherit" });
const bin = path.join(output, "bin"); fs.mkdirSync(bin, { recursive: true });
const runtime = path.join(bin, process.platform === "win32" ? "node.exe" : "node");
fs.copyFileSync(process.execPath, runtime); fs.chmodSync(runtime, 0o755);
const licenses = path.join(output, "licenses"); fs.mkdirSync(licenses, { recursive: true });
const response = await fetch(`https://raw.githubusercontent.com/nodejs/node/v${config.lmmsRuntimeVersion}/LICENSE`);
if (!response.ok) throw new Error(`Cannot fetch pinned Node license: ${response.status}`);
fs.writeFileSync(path.join(licenses, "NODE-LICENSE"), await response.text());
fs.copyFileSync(path.join(source, "..", "..", "LICENSE.txt"), path.join(licenses, "LMMS-COPYING"));
const locked = JSON.parse(fs.readFileSync(path.join(source, "package-lock.json"), "utf8"));
const manifest = { packageVersion: config.version, runtime: { version: process.version, platform: process.platform, arch: process.arch, sha256: createHash("sha256").update(fs.readFileSync(runtime)).digest("hex") }, packages: [] };
function packages(directory) {
 for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
  if (!entry.isDirectory() || entry.name === ".bin") continue;
  const location = path.join(directory, entry.name);
  if (entry.name.startsWith("@")) { packages(location); continue; }
  const metadata = path.join(location, "package.json"); if (!fs.existsSync(metadata)) continue;
  const pkg = JSON.parse(fs.readFileSync(metadata, "utf8"));
  manifest.packages.push({ name: pkg.name, version: pkg.version, license: pkg.license, integrity: locked.packages[path.relative(output, location).split(path.sep).join("/")]?.integrity });
  const target = path.join(licenses, pkg.name.replaceAll("/", "_")); fs.mkdirSync(target, { recursive: true });
  for (const name of fs.readdirSync(location)) if (/^(license|licence|notice|copying)/i.test(name) && fs.statSync(path.join(location, name)).isFile()) fs.copyFileSync(path.join(location, name), path.join(target, name));
 }
}
packages(path.join(output, "node_modules"));
fs.writeFileSync(path.join(output, "versions.json"), JSON.stringify(manifest, null, 2) + "\n");
execFileSync(runtime, [path.join(output, "dist", "src", "index.js"), "--help"], { env: { ...process.env, PATH: "" }, stdio: "inherit" });
