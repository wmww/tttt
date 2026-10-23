// @filename: m.ts
declare const d: number;
export default d;
export const bar = 1;
export type T = number;
// @filename: a.ts
// An import's `type` is an identifier first (tsc's parseImportDeclarationOrImportEqualsDeclaration): the type-only
// modifier only where what follows can't take it as the default import's name, and spelled with an escape it's no
// TS1260. Babel's import-default-and-named-id-type and import-type-escaped-error fixtures
import type, { bar } from "./m";
import typ\u{65} d2 from "./m";
import typ\u0065 { T } from "./m";
import typ\u0065 * as ns from "./m";
export { bar, d2, ns };
let t: T = 0;
