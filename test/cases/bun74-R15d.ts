// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
const q = <await>(x: await) => x;
// @filename: b.ts
export {};
const q = <T, await>(x: T) => x;
