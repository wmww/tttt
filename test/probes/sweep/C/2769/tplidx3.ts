interface A { start: number[]; end: number[]; }
interface I { [index: number | `${string}%`]: number[] | Record<string, string>; }
type T = A | I | number[][] | Array<Record<string, unknown>>;
declare function f(p: T): void;
f({ "0%": [1], "100%": [2] });
type T2 = A | I;
declare function f2(p: T2): void;
f2({ "0%": [1], "100%": [2] });
type S = "width" | (string & Record<never, never>);
declare function h(c: string | { [P in S]?: string | number }): void;
h({ "background-color": "red", width: "1" });
