// @strict: true
// tsc: clean. instantiateContextualType replaces only top-level instantiable types, so actorRef is read through the
// generic mapped type. tttt's second pass instantiates the whole parameter type with C = {}: no keys, `({ spawn })` is
// implicitly any (TS7031). xstate: assign({ prop: ({ spawn }) => ... }) x7 in the runtime tests.
type Args<C> = { context: C; spawn: (x: string) => number };
type PropertyAssigner<C> = { [K in keyof C]?: ((args: Args<C>) => C[K]) | C[K] };
declare function assign<C extends object>(a: PropertyAssigner<C & {}>): (c: C) => void;
const f: (c: {}) => void = assign({ actorRef: ({ spawn }) => spawn('a') });
