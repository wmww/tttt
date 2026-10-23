// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["esnext"], "target": "esnext", "module": "esnext" } }
// @filename: a.ts
export {};
await using f = function () {};
f[Symbol.asyncDispose] = async () => {};
f.x = 1;
const sentinel: number = "";
