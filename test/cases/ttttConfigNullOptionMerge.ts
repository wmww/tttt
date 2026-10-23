// @noTypesAndSymbols: true
// A `null` option takes back only what the configs merged before it at its level set (tsgo merges each extended
// config's own options, its explicit nulls included, into the options of the config that lists it): y's base saying
// `"strict": null` keeps x's false, and an object's own `"strict": false, "strict": null` is false; a second
// `compilerOptions` adds to the first (bun's 38-C9, 38-C10b, 38-C10c)
// @filename: tsconfig.json
{ "extends": ["./x.json", "./y.json"], "compilerOptions": { "noEmit": true, "types": [], "noUnusedLocals": false, "noUnusedLocals": null }, "compilerOptions": { "lib": ["es2022"] } }
// @filename: x.json
{ "compilerOptions": { "strict": false } }
// @filename: y.json
{ "extends": "./z.json" }
// @filename: z.json
{ "compilerOptions": { "strict": null } }
// @filename: a.ts
function implicitAny(x) { let unused = 1; return x; }
const sentinel: number = "";
