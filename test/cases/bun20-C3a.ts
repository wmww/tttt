// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cond(): boolean;
declare function id<T>(x: T): T;
function a13(): string { let x: string | number = ""; do { do { x = id(x); } while (cond()); } while (cond()); return x; }
const sentinel: number = "";
