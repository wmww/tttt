// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class K { [+1] = ""; [-0] = "" }
new K()[1];
new K()[0];
new K()["+1"];
new K()["-0"];
const sentinel: number = "";
