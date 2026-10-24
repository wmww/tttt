//// [test/cases/npm_mcp-namecheap_TS2883.ts] ////

//// [package.json]
{"name": "sdk", "version": "1.0.0", "exports": {"./server/mcp.js": "./dist/server/mcp.js"}}
//// [index.d.ts]
export declare class Server { x: number }
//// [mcp.d.ts]
import { Server } from './index.js';
export declare class McpServer {
    readonly server: Server;
}
//// [index.ts]
// @ts-nocheck - the SDK's types are too deep
import { McpServer } from "sdk/server/mcp.js";
export default function () {
  const server = new McpServer();
  return server.server;
}


//// [index.js]
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.default = default_1;
// @ts-nocheck - the SDK's types are too deep
const mcp_js_1 = require("sdk/server/mcp.js");
function default_1() {
    const server = new mcp_js_1.McpServer();
    return server.server;
}


//// [index.d.ts]
export default function (): any;
