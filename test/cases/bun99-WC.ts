// @filename: tsconfig.json
{"compilerOptions":{"strict":true,"noEmit":true,"target":"es2022","module":"esnext","moduleResolution":"bundler","types":[]},"files":["before.ts","other.ts","zleaf.ts"]}
// @filename: before.ts
css2({ ':hover': { notAProperty: '1' } });
export declare function css2(style: Style2): void;
type Pseudos2 = ':hover';
export type CSSPseudos2 = { [P in Pseudos2]?: Omit<Style2, Pseudos2> };
export type Style2 = CSSPseudos2 & { top?: string };
// @filename: other.ts
import { CSSPseudos2, css2 } from './before';
type T = CSSPseudos2 & { x: 1 };
declare const t: T;
const a: { x: 1 } = t;
const b: never = t;
css2({ ':hover': { notAProperty: '2' } });
// @filename: zleaf.ts
import { css2 } from './before';
css2({ ':hover': { notAProperty: '3' } });
export const sentinel: number = '';
