// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function g(...a: boolean[]): void;
declare const x: unknown, b: number, c: number, d: number, an: any;
declare let n: number;
g(x as number < b, c > (d));
g(x satisfies unknown as number < b, c > (d));
const t = [x as number < b, c > (d)];
const u = x as any < b > (an);
g(n++ < b, c > (d));
const sentinel: number = "";
