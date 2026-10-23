// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
/** @type {NS.A.} */ let y1 = 1;
/** @type {Array<NS.A.>} */ let y2 = [1];
/** @type {import("./m").M.} */ let y3 = 1;
/** @type {typeof globalThis.} */ let y4 = 1;
/** @type {typeof p.} */ let y5 = 1;
/** @type {number} */ const sentinel = "";
// @filename: m.d.ts
export namespace M { interface X {} }
// @filename: n.d.ts
declare namespace NS { interface A { a: number } }
declare class P { private "": number }
declare const p: P;
