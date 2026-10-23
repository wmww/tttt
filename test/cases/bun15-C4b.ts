// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const C1: { new(): {}; 2: number } = class { static [+2] = 1; };
const sentinel: number = "";
