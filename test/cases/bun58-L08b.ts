// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true, "module": "commonjs" }, "files": ["m.js", "aug.ts"] }
// @filename: aug.ts
export {};
declare module "./m" {
  export const c: number;
  export namespace d { const nd: number }
  export function f(): void;
  export class g {}
  export var h: number;
  export enum i { A }
}
const sentinel: number = "";
// @filename: m.js
exports.c = 1;
exports.d = 1;
exports.f = 1;
exports.g = 1;
exports.h = 1;
exports.i = 1;
