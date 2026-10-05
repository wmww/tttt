interface H { a: string }
interface M { [key: string]: string | H }
interface J extends M { "": H }
declare function f(j: J): void;
f({ "": { a: "x" }, w: "y" });
type L = { "": H } & Record<string, string | H>;
declare function g(j: L): void;
g({ w: "y", "": { a: "x" } });
