// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare function id<T>(o: T): T;
const e = id({ a: 1, get g() { return this.zz; }, f() { return this.g; } });
const sentinel: number = "";
