// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I { me: this }
function g<T>(t: T & I): T & I { return t.me; }
const sentinel: number = "";
