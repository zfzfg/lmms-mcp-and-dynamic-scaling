import { McpServer, ResourceTemplate, fromJsonSchema, JsonSchemaType } from "@modelcontextprotocol/server";
import { serveStdio, StdioServerHandle } from "@modelcontextprotocol/server/stdio";
import * as z from "zod/v4";
import { BridgeClient } from "./bridge-client.js";
import { TOOLS } from "./tools/index.js";
import { RESOURCES } from "./resources.js";
import { PROMPTS } from "./prompts.js";
import { LmmsBridgeError } from "./errors.js";

export class LmmsMcpServer {
  private handle?: StdioServerHandle;
  constructor(private bridgeClient: BridgeClient) {}

  public createServer(): McpServer {
    const server = new McpServer({ name: "lmms-mcp", version: "1.1.0" });
    for (const tool of TOOLS) {
      server.registerTool(tool.name, {
        description: tool.description,
        inputSchema: fromJsonSchema<Record<string, unknown>>(tool.inputSchema as JsonSchemaType),
        outputSchema: fromJsonSchema<Record<string, unknown>>(tool.outputSchema! as JsonSchemaType),
        annotations: tool.annotations
      }, async (args) => {
        try {
          return await tool.handler(this.bridgeClient, args);
        } catch (error) {
          const detail = error instanceof LmmsBridgeError
            ? { code: error.code, message: error.message, details: error.details }
            : { code: "INTERNAL_ERROR", message: error instanceof Error ? error.message : String(error) };
          return { isError: true, content: [{ type: "text" as const, text: detail.message }],
            structuredContent: { ok: false, error: detail } };
        }
      });
    }
    for (const resource of RESOURCES) {
      server.registerResource(resource.name, new ResourceTemplate(resource.uriTemplate, { list: undefined }),
        { description: resource.description, mimeType: resource.mimeType }, async (uri) => {
          const match = uri.toString().match(resource.uriPattern);
          if (!match) { throw new Error("Invalid resource URI."); }
          return resource.read(this.bridgeClient, uri.toString(), match);
        });
    }
    for (const prompt of PROMPTS) {
      const shape: Record<string, z.ZodType> = {};
      for (const arg of prompt.arguments ?? []) { shape[arg.name] = arg.required ? z.string() : z.string().optional(); }
      server.registerPrompt(prompt.name, { description: prompt.description, argsSchema: z.object(shape) }, async (args) => ({
        description: prompt.description, messages: prompt.messages(args as Record<string, string>)
      }));
    }
    return server;
  }

  public async start(): Promise<void> {
    this.handle = serveStdio(() => this.createServer(), { legacy: "serve",
      onerror: (error) => process.stderr.write(`[lmms-mcp] ${error.message}\n`) });
    process.stdin.once("end", () => { this.bridgeClient.close(); });
  }
  public async close(): Promise<void> { this.bridgeClient.close(); await this.handle?.close(); }
}
