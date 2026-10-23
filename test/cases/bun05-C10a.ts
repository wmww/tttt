// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
async function imp() { const i2: number = await import(("./m")); }
export {};
const sentinel: number = "";
// @filename: m.ts
export const v = 1;
