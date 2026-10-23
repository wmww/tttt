// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class PA { #p = 1; static m<T extends Missing3>(t: T) { t.#p; } }
const sentinel: number = "";
