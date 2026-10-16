import { BridgeClient } from "./bridge-client.js";

export interface ResourceDefinition {
  uriPattern: RegExp;
  name: string;
  description: string;
  mimeType: string;
  read: (client: BridgeClient, uri: string, match: RegExpMatchArray) => Promise<{
    contents: Array<{
      uri: string;
      mimeType: string;
      text?: string;
      blob?: string;
    }>;
  }>;
}

export const RESOURCES: ResourceDefinition[] = [
  {
    uriPattern: /^lmms:\/\/instances\/([^/]+)\/projects\/([^/]+)\/summary$/,
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
    name: "Track Details",
    description: "Track settings, instrument and clips",
    mimeType: "application/json",
    read: async (client, uri, match) => {
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
  }
];
