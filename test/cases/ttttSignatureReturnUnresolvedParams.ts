// A signature's return types that aren't related are reported as `get(...)` when either signature has parameters
// (tsgo's incompatibleReporter). A generic member of an instantiated type is resolved lazily: its parameter count was
// read before it was, so the message said `get()` unless something had resolved it earlier (plan 62: the message
// depended on the checker count in redux-toolkit)
type Opt<T> = { [K in keyof T]?: T[K] };
interface Api<T> { util: { get<K extends string>(name: K, arg: number): T[] }; tag: Opt<T> }
declare const s: Api<{ a: string }>;
const d: Api<{ a: number }> = s;
