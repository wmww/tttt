// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f1({ a: \u0062 }: any): void;
type T7 = ({ a: \u{62}c }: any) => void;
type T8 = ({ \u0061: b }: any) => void;
const sentinel: number = "";
