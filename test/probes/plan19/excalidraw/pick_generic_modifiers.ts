// @strict: true
type E = { a: number; c?: string } | { a: number; b: string; c?: string };
function f<T extends { a: number } & { c?: string }>(t: T) {
  const base: Pick<T, keyof E> = { a: t.a };
  return base;
}
function g<T extends { a: number; c?: string }>(t: T) {
  const base: Pick<T, "a" | "c"> = { a: t.a };
  return base;
}
