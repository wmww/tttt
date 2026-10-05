interface I { [key: `${number}%`]: number[] }
declare function f(p: I): void;
f({ "0%": [1], "100%": [2] });
type C = { [x: string & Record<never, never>]: string | number | undefined; width?: string };
declare function h(c: C): void;
h({ "background-color": "red", width: "1" });
