// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "resolveJsonModule": false, "paths": { "@data": ["./data.json", "./other.d.ts"] } }, "files": ["a.ts"] }
// @filename: a.ts
import {} from "@data";
const sentinel: number = "";
// @filename: data.json
{}
// @filename: other.d.ts
export {};
