// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "esnext.decorators"], "target": "es2022" } }
// @filename: a.ts
export {};
declare function dec(...a: any[]): any;
type T4 = string;
const c1 = @dec(() => CE1) class CE1 {};
const c2 = @dec((x: T) => x) class<T> {};
const c4 = @dec((x: T4): string => x) class<T4> {};
const sentinel: number = "";
