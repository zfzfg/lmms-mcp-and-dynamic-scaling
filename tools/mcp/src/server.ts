import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  ListToolsRequestSchema,
  CallToolRequestSchema,
  ListResourcesRequestSchema,
  ReadResourceRequestSchema,
  ListPromptsRequestSchema,
  GetPromptRequestSchema,
  ErrorCode as McpErrorCode,
  McpError
} from "@modelcontextprotocol/sdk/types.js";
import { BridgeClient } from "./bridge-client.js";
import { TOOLS } from "./tools/index.js";
import { RESOURCES } from "./resources.js";
import { PROMPTS } from "./prompts.js";
import { LmmsBridgeError } from "./errors.js";

export class LmmsMcpServer {
  private server: Server;
  private bridgeClient: BridgeClient;

  constructor(bridgeClient: BridgeClient) {
    this.bridgeClient = bridgeClient;

    this.server = new Server(
      {
        name: "lmms-mcp",
        version: "1.0.0"
      },
      {
        capabilities: {
          tools: {},
          resources: {},
          prompts: {}
        }
      }
    );

    this.setupHandlers();
  }

  private setupHandlers(): void {
    // 1. List Tools
    this.server.setRequestHandler(ListToolsRequestSchema, async () => {
      return {
        tools: TOOLS.map((t) => ({
          name: t.name,
          description: t.description,
          inputSchema: t.inputSchema
        }))
      };
    });

    // 2. Call Tool
    this.server.setRequestHandler(CallToolRequestSchema, async (request) => {
      const toolName = request.params.name;
      const tool = TOOLS.find((t) => t.name === toolName);

      if (!tool) {
        throw new McpError(McpErrorCode.MethodNotFound, `Tool not found: ${toolName}`);
      }

      try {
        const result = await tool.handler(this.bridgeClient, request.params.arguments || {});
        return {
          content: result.content,
          structuredContent: result.structuredContent,
          isError: result.isError ?? false
        };
      } catch (err: unknown) {
        if (err instanceof LmmsBridgeError) {
          return {
            content: [{
              type: "text",
              text: `[Error: ${err.code}] ${err.message}`
            }],
            structuredContent: {
              ok: false,
              error: {
                code: err.code,
                message: err.message,
                details: err.details
              }
            },
            isError: true
          };
        }

        const msg = (err as Error).message || String(err);
        return {
          content: [{
            type: "text",
            text: `Unexpected tool execution error: ${msg}`
          }],
          isError: true
        };
      }
    });

    // 3. List Resources
    this.server.setRequestHandler(ListResourcesRequestSchema, async () => {
      const instanceId = this.bridgeClient.instanceId;
      return {
        resources: [
          {
            uri: `lmms://instances/${instanceId}/capabilities`,
            name: "LMMS Capabilities",
            mimeType: "application/json"
          }
        ]
      };
    });

    // 4. Read Resource
    this.server.setRequestHandler(ReadResourceRequestSchema, async (request) => {
      const uri = request.params.uri;
      for (const resDef of RESOURCES) {
        const match = uri.match(resDef.uriPattern);
        if (match) {
          try {
            return await resDef.read(this.bridgeClient, uri, match);
          } catch (err) {
            throw new McpError(McpErrorCode.InternalError, `Failed to read resource ${uri}: ${(err as Error).message}`);
          }
        }
      }
      throw new McpError(McpErrorCode.InvalidRequest, `Resource URI not supported: ${uri}`);
    });

    // 5. List Prompts
    this.server.setRequestHandler(ListPromptsRequestSchema, async () => {
      return {
        prompts: PROMPTS.map((p) => ({
          name: p.name,
          description: p.description,
          arguments: p.arguments
        }))
      };
    });

    // 6. Get Prompt
    this.server.setRequestHandler(GetPromptRequestSchema, async (request) => {
      const promptName = request.params.name;
      const prompt = PROMPTS.find((p) => p.name === promptName);
      if (!prompt) {
        throw new McpError(McpErrorCode.MethodNotFound, `Prompt not found: ${promptName}`);
      }

      return {
        description: prompt.description,
        messages: prompt.messages((request.params.arguments as Record<string, string>) || {})
      };
    });
  }

  public async start(): Promise<void> {
    const transport = new StdioServerTransport();
    await this.server.connect(transport);
    process.stderr.write(`[lmms-mcp] Companion server started on stdio (Instance: ${this.bridgeClient.instanceId})\n`);
  }
}
