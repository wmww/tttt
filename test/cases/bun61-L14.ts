// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
@dec namespace class {}
// @filename: b.ts
@dec interface class {}
// @filename: c.ts
@dec type class = 1
// @filename: d.ts
@dec namespace
N {}
// @filename: e.ts
@dec interface
I {}
