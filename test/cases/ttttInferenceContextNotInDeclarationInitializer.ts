// @strict: true
// The return type of a generic function inferred while a call of it infers its own type arguments: the initializers in
// its body aren't arguments of that call, their contextual types aren't instantiated with its inferences (trpc: an
// inferred return type was any, depending on which file a checker saw first; scripts, so that main.ts is checked first)
// @filename: main.ts
const r = outer(1);
r.ctx.o.v;
r.zz;
// @filename: inner.ts
interface Opts<T> { make?: (x: T) => number; v?: T }
function inner<T>(r: T, opts?: Opts<T>) {
  const o = { v: r, make: (x) => 1 } satisfies Opts<T>;
  const ctx = { o, r };
  return ctx;
}
// @filename: outer.ts
function outer<T>(r: T) {
  const ctx = inner(r, { make: (x) => 1 });
  return { ctx };
}
