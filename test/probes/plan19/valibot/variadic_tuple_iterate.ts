interface A { kind: 'a' }
interface B { kind: 'b' }
function f<const S extends A, const T extends readonly B[]>(...pipe: [S, ...T]) {
  for (const item of pipe) {
    item.kind;
  }
  const e = pipe[1];
  const [x, ...rest] = pipe;
  const n: number = pipe.length;
  pipe.map(i => i.kind);
}
function g<T extends readonly B[]>(t: [...T, A], u: readonly [A, ...T]) {
  for (const item of t) item.kind;
  for (const item of u) item.kind;
}
