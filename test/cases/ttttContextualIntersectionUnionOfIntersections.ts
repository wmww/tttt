// A contextual type `C & Check<C>` whose type parameter's constraint is a union of intersections: distributing over the
// union, each alternative intersection is flattened into the members, so its properties still say something (a
// regression of the distribution: react-navigation's static config callbacks lost their parameter types)
// @strict: true
// @declaration: true
interface Opts { cb?: (o: { placement: 'regular' | 'inline' }) => void; n?: number }
type Strict<C> = string extends keyof C ? {} : Record<Exclude<keyof C, keyof Opts | 'a' | 'b'>, never>;
type Either = ({ a: 1 } & Opts) | ({ b: 2 } & Opts);
type Check<C> = C extends { n: infer N } ? { n: N } : unknown;
declare function make<const C extends Either & Strict<C>>(config: C & Check<C>): C;
export const made = make({ a: 1, cb: ({ placement }) => { const p: 'regular' | 'inline' = placement; } });
