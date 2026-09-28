// `typeof f<T>` of a function declared where no type parameter is in scope, instantiated (here by the restrictive
// instantiation of a conditional type): its type arguments are, the function type isn't (tsc's getOuterTypeParameters is
// empty), so Parameters<typeof f<T>> resolves to a tuple (xstate's enqueue.assign = (...args) => assign(...args))
declare function assign<A, B>(a: A, b?: B): number;
function f<T>() {
  type P = Parameters<typeof assign<T, string>>;
  const x: { assign: (...args: Parameters<typeof assign<T, string>>) => void } = {
    assign: (...args) => { assign(...args); }
  };
  const y: { assign: (...args: Parameters<(a: T, b?: string) => number>) => void } = {
    assign: (...args) => { assign(...args); }
  };
}
