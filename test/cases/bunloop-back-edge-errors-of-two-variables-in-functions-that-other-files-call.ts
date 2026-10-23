// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "types": [], "lib": ["es2022"] }, "files": ["c.ts", "b.ts", "a.ts"] }
// @filename: a.ts
interface Item { next: Item | null }
declare const more: boolean; declare const item: Item;
export function build() { let last: Item | null = null; let n: number | string = 0; while (more) { n = more && n === last ? "s" : 5; last = n === 0 ? item : null; } return last; }
export function build2() { let last: Item | null = null; let n: number | string = 0; while (more) { last = n === 0 ? item : null; n = more && n === last ? "s" : 5; } return n; }
export function build3() { let a: number | string = 0; let b: number | string = 0; while (more) { a = more && b === "x" ? "s" : 5; b = more && a === "y" ? "t" : 6; } return b; }
// @filename: b.ts
import { build, build2, build3 } from "./a"; export const x = build(); export const y = build2(); export const z = build3();
// @filename: c.ts
import { build3, build2, build } from "./a"; export const z = build3(); export const y = build2(); export const x = build();
