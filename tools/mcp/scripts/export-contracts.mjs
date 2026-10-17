import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { TOOLS } from "../dist/src/tools/index.js";
import { CHANGE_COMMAND_SCHEMA } from "../dist/src/contracts.js";
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../../doc/mcp/schemas");
for (const [name, schema] of [
 ["mcp-tools-input.schema.json", { $schema: "http://json-schema.org/draft-07/schema#", contractVersion: "1.0", definitions: Object.fromEntries(TOOLS.map(tool => [tool.name, tool.inputSchema])) }],
 ["mcp-tools-output.schema.json", { $schema: "http://json-schema.org/draft-07/schema#", contractVersion: "1.0", definitions: Object.fromEntries(TOOLS.map(tool => [tool.name, tool.outputSchema])) }],
 ["mcp-commands.schema.json", { $schema: "http://json-schema.org/draft-07/schema#", contractVersion: "1.0", ...CHANGE_COMMAND_SCHEMA }]
]) {
 const content = JSON.stringify(schema, null, 2) + "\n"; const target = path.join(root, name);
 if (process.argv.includes("--check")) { if (fs.readFileSync(target, "utf8") !== content) throw new Error(`${name} is out of date; run npm run contracts`); }
 else fs.writeFileSync(target, content);
}
