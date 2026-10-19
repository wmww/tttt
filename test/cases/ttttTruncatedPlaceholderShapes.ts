// @strict: true
// @noErrorTruncation: false
// Past a message's truncation length: a conditional's union branch is one placeholder (visitAndTransformType), a
// placeholder member of a union or intersection isn't parenthesized (a type reference node), and the symbolless
// empty object type of a narrowing still prints `{}` (createAnonymousTypeNodeEx prints it from its members)
function f<T>(
  long: { aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: string; bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb: number; cccccccccccccccccccccccccc: boolean; ddddddddddddddddddd: 1 },
  c: T extends string ? { s: T } : { n: T } | { m: T },
  q: (T extends string ? { x: T } : { y: T }) & ({} | null),
  g: (() => T) | (() => string),
  u: T | undefined,
) {
  const x1: number = [long, c] as const;
  const x2: number = [long, q, q] as const;
  const x3: number = [long, g, g] as const;
  if (u !== undefined) { const x4: number = [long, u, u] as const; }
}
