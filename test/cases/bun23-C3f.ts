// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class K { static readonly "x-y": unique symbol = Symbol(); }
const o: 1 = { [K["x-y"]]: 1 };
const sentinel: number = "";
