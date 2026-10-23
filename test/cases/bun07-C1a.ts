// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare function id<T>(o: T): T;
const d = id({ a: 1, get g() { return this.a; }, inc() { this.a++; } });
const s1: string = d.g;
const sentinel: number = "";
