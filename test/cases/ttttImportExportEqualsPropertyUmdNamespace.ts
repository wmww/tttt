// @module: nodenext
// @strict: true
// A named import of a property of what `export =` names isn't a namespace: `dbg.Debugger` goes past the import to the
// module's UMD global (tsc's resolveName skips an alias without the meaning; ethereumjs's `import type { debug } from
// "debug"`)
// @filename: /node_modules/@types/dbg/index.d.ts
declare var dbg: dbg.Debug & { dbg: dbg.Debug };
export = dbg;
export as namespace dbg;
declare namespace dbg {
    interface Debug { (ns: string): Debugger; }
    interface Debugger { enabled: boolean; }
}
// @filename: /node_modules/@types/dbg/package.json
{ "name": "@types/dbg", "version": "1.0.0", "types": "index.d.ts" }
// @filename: /a.ts
import type { dbg } from "dbg";
export interface I { d?: dbg.Debugger; e: typeof dbg }
declare const i: I;
export const enabled = i.d?.enabled;
