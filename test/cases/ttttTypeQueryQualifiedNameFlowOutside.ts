// @strict: true
// @noEmit: true

// A qualified name in `typeof a.b.c` is narrowed by the code around the function expression it's in: tsc's flow walk
// stops at a function's start only for property and element access expressions, which a qualified name isn't
declare const params: { replyOptions?: { onTyping?: (x: number) => void } };
const f = params.replyOptions?.onTyping
  ? (t: Parameters<NonNullable<typeof params.replyOptions.onTyping>>[0]) => t
  : undefined;

function g(p: { o?: { a: string } }) {
  if (p.o) {
    const h = function (x: typeof p.o.a) { return x; };
    const o = { m(y: typeof p.o.a): typeof p.o.a { return y; } };
    let z: typeof p.o.a = "";
  }
}

let v: { o?: { a: number } } = {};
if (v.o) {
  const k = () => {
    // The expression is a property access: not narrowed in the arrow
    v.o.a;
    let w: typeof v.o.a = 1;
  };
}
