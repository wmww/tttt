// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
let x: ([...a,]: any);
let y: ({...a,}: any);
let z: ([...a,], b);
