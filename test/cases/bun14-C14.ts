// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
enum Status { Active = "ACTIVE", Done = "DONE" }
const s: Status = "active";
const sentinel: number = "";
