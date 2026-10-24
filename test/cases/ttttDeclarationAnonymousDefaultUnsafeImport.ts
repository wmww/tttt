// An anonymous `export default function` whose inferred return type can only be named through a node_modules path: tsgo's
// symbol tracker has no location for TS2883 (no name, no fallback node), so nothing is reported, and the type is
// emitted as any (the node builder's encounteredError)
// @strict: true
// @declaration: true
// @module: commonjs
// @moduleResolution: bundler
// @filename: node_modules/sdk/package.json
{"name": "sdk", "version": "1.0.0", "exports": {"./server/mcp.js": "./dist/server/mcp.js"}}
// @filename: node_modules/sdk/dist/server/index.d.ts
export declare class Server { x: number }
// @filename: node_modules/sdk/dist/server/mcp.d.ts
import { Server } from './index.js';
export declare class McpServer {
    readonly server: Server;
}
// @filename: src/index.ts
// no directive
import { McpServer } from "sdk/server/mcp.js";
export default function () {
  const server = new McpServer();
  return server.server;
}
