// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type A = <T>({#x: a}: any) => void;
interface I { m({#x: b}: any): void }
declare function f({#x: c}: any): void;
const sentinel: number = "";
