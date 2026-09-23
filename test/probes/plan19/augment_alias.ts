// @filename: node_modules/lib/index.d.ts
interface S { <T>(x: T): T; any: () => any; }
export { type S };
// @filename: aug.d.ts
import { S } from 'lib';
declare module 'lib' { interface S { extra: number } }
// @filename: main.ts
import { S } from 'lib';
declare const s: S;
s(1); s.any(); s.extra;
