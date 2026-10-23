// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022" } }
// @filename: a.ts
let t1: new ({ a b }) => void;
// @filename: b.ts
let t2: { m({ a b }): void };
// @filename: c.ts
let t3: new ([ a b ]) => void;
