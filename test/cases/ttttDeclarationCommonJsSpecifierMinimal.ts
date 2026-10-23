// @module: nodenext
// @declaration: true
// @emitDeclarationOnly: true
// @allowImportingTsExtensions: true
// An import type of a module nothing in a CommonJS file imports, under nodenext: tsgo's inferPreference skips every
// relative import for a CommonJS specifier (the ES-import check is a TODO there), so the ending is minimal even when
// the file's imports write `.ts` (ethereumjs's `import("./net/server")`, not `"./net/server/rlpxserver.ts"`)
// @filename: package.json
{ "name": "app" }
// @filename: net/server/rlpxserver.ts
export class RlpxServer { started = false; }
// @filename: net/server/index.ts
export * from "./rlpxserver.ts";
// @filename: config.ts
import { RlpxServer } from "./net/server/index.ts";
export class Config { server: RlpxServer | undefined = undefined; }
// @filename: client.ts
import { Config } from "./config.ts";
export class Client {
    constructor(readonly config: Config) {}
    server() { return this.config.server; }
}
