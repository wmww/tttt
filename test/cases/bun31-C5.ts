// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class H { [+2] = 1; }
new H()[2];
new H()["+2"];
const sentinel: number = "";
