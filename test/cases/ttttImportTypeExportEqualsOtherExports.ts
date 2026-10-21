// `import("m").Config` of a TypeScript module with `export =` names the assigned value's exports, not the module's
// own `export { type Config }` beside it (TS2694; only a CommonJS `module.exports =` keeps its typedefs next to it,
// tsgo's getTypeFromImportTypeNode). dompurify 3's purify.cjs.d.ts; a named import still finds it
// @module: commonjs
// @filename: node_modules/dp/index.d.ts
interface Config { a?: string }
interface DOMPurify { sanitize(s: string, c?: Config): string }
declare const _default: DOMPurify;
export { type Config, type DOMPurify };
// @ts-ignore
export = _default;
// @filename: a.ts
export type C1 = import('dp').Config;
export type C2 = import('dp').DOMPurify;
import type { Config } from 'dp';
export type C3 = Config;
