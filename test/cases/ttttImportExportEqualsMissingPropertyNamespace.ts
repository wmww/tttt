// @module: nodenext
// @strict: true
// A named import of a property of what `export =` names, used as a namespace: a missing property is tsc's unknownSymbol
// (TS2305 at the import, every meaning, silent at `nope.X`), an existing one is a value and no namespace (TS2503 when
// no other namespace of that name is found)
// @filename: /node_modules/@types/dbg/index.d.ts
declare var dbg: dbg.Debug & { yes: dbg.Debug };
export = dbg;
declare namespace dbg {
    interface Debug { (ns: string): Debugger; }
    interface Debugger { enabled: boolean; }
}
// @filename: /node_modules/@types/dbg/package.json
{ "name": "@types/dbg", "version": "1.0.0", "types": "index.d.ts" }
// @filename: /a.ts
import type { nope, yes } from "dbg";
export interface I { d?: nope.Debugger; e?: yes.Debugger }
