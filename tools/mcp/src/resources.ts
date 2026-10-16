import { createHash } from "node:crypto";
import { BridgeClient } from "./bridge-client.js";

export interface ResourceDefinition {
  uriTemplate: string;
  uriPattern: RegExp;
  name: string;
  description: string;
  mimeType: string;
  read: (client: BridgeClient, uri: string, match: RegExpMatchArray) => Promise<{
    contents: Array<{ uri: string; mimeType: string } & ({ text: string } | { blob: string })>;
  }>;
}

export const RESOURCES: ResourceDefinition[] = [
  {
    uriPattern: /^lmms:\/\/instances\/([^/]+)\/projects\/([^/]+)\/summary$/,
    uriTemplate: "lmms://instances/{instanceId}/projects/{projectId}/summary",
    name: "Project Summary",
    description: "Compact project state, tempo, time signature, and revision",
    mimeType: "application/json",
    read: async (client, uri, match) => {
      const projectId = match[2];
      const res = await client.call("get_project", { projectId });
      return {
        contents: [{
          uri,
          mimeType: "application/json",
          text: JSON.stringify(res, null, 2)
        }]
      };
    }
  },
  {
    uriPattern: /^lmms:\/\/instances\/([^/]+)\/capabilities$/,
    uriTemplate: "lmms://instances/{instanceId}/capabilities",
    name: "LMMS Capabilities",
    description: "Active permissions, supported tools and features",
    mimeType: "application/json",
    read: async (client, uri) => {
      const res = await client.call("get_status");
      return {
        contents: [{
          uri,
          mimeType: "application/json",
          text: JSON.stringify(res, null, 2)
        }]
      };
    }
  },
  {
    uriPattern: /^lmms:\/\/instances\/([^/]+)\/projects\/([^/]+)\/tracks\/([^/]+)$/,
    uriTemplate: "lmms://instances/{instanceId}/projects/{projectId}/tracks/{trackId}",
    name: "Track Details",
    description: "Track settings, instrument and clips",
    mimeType: "application/json",
    read: async (client, uri, match) => {
      await client.call("get_project", { projectId: match[2] });
      const trackId = match[3];
      const clipsRes = await client.call("list_clips", { trackId });
      return {
        contents: [{
          uri,
          mimeType: "application/json",
          text: JSON.stringify(clipsRes, null, 2)
        }]
      };
    }
  },
  {
    uriTemplate: "lmms://instances/{instanceId}/projects/{projectId}/clips/{clipId}/summary",
    uriPattern: /^lmms:\/\/instances\/([^/]+)\/projects\/([^/]+)\/clips\/([^/]+)\/summary$/,
    name: "Clip Summary", description: "Revision and bounded note summary for a MIDI clip", mimeType: "application/json",
    read: async (client, uri, match) => {
      await client.call("get_project", { projectId: match[2] });
      const notes = await client.call("get_clip_summary", { clipId: match[3] });
      return { contents: [{ uri, mimeType: "application/json", text: JSON.stringify(notes) }] };
    }
  },
  {
    uriTemplate: "lmms://instances/{instanceId}/artifacts/{artifactId}",
    uriPattern: /^lmms:\/\/instances\/([^/]+)\/artifacts\/([^/]+)$/,
    name: "Audio Artifact", description: "Verified immutable WAV artifact, up to 16 MiB", mimeType: "audio/wav",
    read: async (client, uri, match) => {
      const chunks: Buffer[] = []; let offset = 0; let total = 0; let hash: string | undefined;
      do {
        const chunk = await client.call("read_artifact", { artifactId: match[2], offset, limit: 256 * 1024 });
        if (!Number.isSafeInteger(chunk.totalBytes) || chunk.totalBytes > 16 * 1024 * 1024 || chunk.totalBytes < 1 || chunk.offset !== offset
          || (hash !== undefined && (chunk.sha256 !== hash || chunk.totalBytes !== total))) throw new Error("Invalid artifact metadata.");
        total = chunk.totalBytes; hash = chunk.sha256;
        const bytes = Buffer.from(chunk.data, "base64");
        if (!bytes.length || bytes.length > 256 * 1024 || offset + bytes.length > total) throw new Error("Invalid artifact chunk.");
        chunks.push(bytes); offset += bytes.length;
      } while (offset < total);
      const bytes = Buffer.concat(chunks);
      if (createHash("sha256").update(bytes).digest("hex") !== hash) throw new Error("Artifact checksum mismatch.");
      return { contents: [{ uri, mimeType: "audio/wav", blob: bytes.toString("base64") }] };
    }
  }

];
