// An alias body keeps the alias type parameters it never mentions: f is H<U, Option<U>> in P<U> (tsc), not H<A, Option<U>>
interface Option<T> { v: T }
type H<A, B = Option<A>> = (b: B) => void;
interface P<U> { f: H<U> }
declare const p: P<string>;
const f = p.f;
