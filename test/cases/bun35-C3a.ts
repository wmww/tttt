// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "resolveJsonModule": false, "paths": { "j": ["./d.json", "./f.ts"] } } }
// @filename: a.ts
import "j";
const sentinel: number = "";
// @filename: d.json
{}
// @filename: f.ts
export {};
