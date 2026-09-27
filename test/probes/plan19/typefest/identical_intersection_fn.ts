// @strict: true
type Eq<L, R> = (<T>() => T extends (L & T) | T ? true : false) extends (<T>() => T extends (R & T) | T ? true : false) ? true : false;
type F = { (a: number): string; key: string };
type G = ((...args: [a: number]) => string) & { key: string };
type H = { key: string } & { (a: number): string };
type A = Eq<F, G>;
type B = Eq<F, F>;
type C = Eq<G, H>;
type D = Eq<{ a: 1 } & { b: 2 }, { a: 1; b: 2 }>;
