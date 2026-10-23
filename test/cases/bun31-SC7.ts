// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function use(...a: any[]): void;
type O = { b?: number } | undefined;
function y1(o: O): number { o?.b ??= 1; }
function y2(o: O) { let x: number; o?.b ||= 1; use(x); }
function y3(o: O, s: string | number) { if (typeof s === "string") { o?.b &&= 1; const n: never = s; } }
class Y4 { a: number; constructor(o: O) { o?.["b"] ??= 1; } }
const sentinel: number = "";
