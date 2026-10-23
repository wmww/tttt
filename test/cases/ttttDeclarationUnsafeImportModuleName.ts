// @module: commonjs
// @target: es2015
// @declaration: true
// @noTypesAndSymbols: true
// TS2883 for a module's own symbol (`typeof import("nested")` reachable only through another package's node_modules)
// names it like tsc's symbol name: the file's path without its extension, in double quotes (the types baseline's
// `import("nested")` vs tsgo's `import("foo/node_modules/nested")` is another bug, agents/triage.md)
// @filename: r/node_modules/foo/node_modules/nested/index.d.ts
declare const v: { a: 1 };
export default v;
// @filename: r/node_modules/foo/node_modules/nested/other.d.ts
declare const w: { b: 1 };
export default w;
// @filename: r/node_modules/foo/index.d.ts
export declare function load(): Promise<typeof import("nested")>;
export declare function loadOther(): Promise<typeof import("nested/other")>;
// @filename: r/entry.ts
import { load, loadOther } from "foo";
export const x = load();
export const y = [load(), loadOther()];
