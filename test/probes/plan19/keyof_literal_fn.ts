const e = { a: () => 1, b: 2 };
type K = keyof typeof e;
type V = (typeof e)[keyof typeof e];
const m = { f(x: string) { return x; } };
type K2 = keyof typeof m;
type IsFn<T> = T extends (...a: any[]) => any ? 'y' : 'n';
type Y = IsFn<typeof e.a>;
function g<T>(t: T) { const o = { h: () => t }; type Z = keyof typeof o; return o; }
