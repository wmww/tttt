// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const cond: boolean;
declare function g2(...args: [number] | []): void;
g2(...(cond ? [1] : []));
const sentinel: number = "";
