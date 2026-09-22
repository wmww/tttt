// @strict: true
declare function a(chunk: string[]): number[];
declare function b(chunk: string[], m: boolean, t: boolean): number;
function pick(x: number) { return x ? a : b; }
const f = pick(1);
const r = f(["x"], true, false);
declare const g: ((x: string) => void) | ((x: string, y?: number) => void);
g("a", 1);
