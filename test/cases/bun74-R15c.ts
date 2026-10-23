// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {}; declare function async<T>(x: T): T; declare const x: number; type U = number;
async <U,>(x);
// @filename: b.ts
export {}; declare function async<T>(x: T): T; declare const x: number; type U = number; type T = number;
async <T extends U>(x);
// @filename: c.ts
export {}; declare function async<T>(x: T): T; declare const x: number; type U = number; type T = number;
async <T = U>(x);
