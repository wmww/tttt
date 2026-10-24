// origin: npm task-definition-ts@1.0.18 https://github.com/shoma-mano/task-definition-ts tsconfig.bench.json seed 20261103
// flip: yes
// @noEmit: true
// @filename: tsconfig.json
{ "compilerOptions": { "allowJs": true, "checkJs": true, "strict": true, "noEmit": true } }
// @filename: dist/index.d.mts
export declare const f: () => string;
// @filename: dist/index.mjs
export const f = () => { const stack = new Error().stack; return stack[1].getFileName(); };
// @filename: dist/index.d.cts
export declare const g: () => string;
// @filename: dist/index.cjs
exports.g = () => { const stack = new Error().stack; return stack[1].getFileName(); };
// @filename: dist/other.d.mts
export declare const h: number;
// @filename: dist/other.js
export const h = 1;
// @filename: src/a.ts
export const x = 1;
