// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const dec: any;
@dec interface = 1
// @filename: b.ts
@dec type
T = 1
// @filename: c.ts
@dec namespace = 1
// @filename: d.ts
@dec module
M = 1
