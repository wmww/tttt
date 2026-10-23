// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function m14<B extends new (...args: Missing[]) => {}>(b: B) { return class extends b { x = 1 }; }
const sentinel: number = "";
