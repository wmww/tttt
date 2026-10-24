// origin: npm mcp-namecheap@1.0.0 https://github.com/cfdude/mcp-namecheap tsconfig.json seed 20261212
// flip: yes
// @strict: true
// @declaration: true
// @module: commonjs
// @moduleResolution: bundler
// A `// @ts-nocheck` file under declaration: true whose default-exported function's inferred return type can't be
// named (a class from a nested node_modules path): tsgo reports nothing in a nocheck file, declaration diagnostics
// included; tttt reports TS2883 (and names the anonymous default export '(Missing)').
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
// @ts-nocheck - the SDK's types are too deep
import { McpServer } from "sdk/server/mcp.js";
export default function () {
  const server = new McpServer();
  return server.server;
}
