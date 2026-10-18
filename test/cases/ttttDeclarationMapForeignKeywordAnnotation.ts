// @declaration: true
// @declarationMap: true
// @strict: true
// A signature declared in another file reuses its keyword annotations (`number`, `void`): they're that file's nodes and
// aren't mapped, even where this file has the same keyword at the same offset (a keyword node has no span: the
// signature's file says where it was written). babel-standalone's preset-stage-1, babel-parser's flow mixin.
// @filename: c.ts
export class Q { m(code: number): void {} }
// @filename: d.ts
export class R { m(code: number): void {} }
import { Q } from "./c";
export const v = new Q().m;
