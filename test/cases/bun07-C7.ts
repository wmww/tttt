// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
const o3 = { ...s3, a: (() => s3)() }; var s3 = { z: o3.a };
const sentinel: number = "";
