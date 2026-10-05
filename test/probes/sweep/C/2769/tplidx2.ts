interface I { [index: number | `${string}%`]: number[] | Record<string, string>; }
declare function f(p: I): void;
f({ "0%": [1], "100%": [2] });
type S = "width" | (string & Record<never, never>);
type C = { [P in S]?: string | number };
declare function h(c: C): void;
h({ "background-color": "red", width: "1" });
const c: C = { "background-color": "red" };
type D = { [x: string & Record<never, never>]: string };
const d: D = { "background-color": "red" };
