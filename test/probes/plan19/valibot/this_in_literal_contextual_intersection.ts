// @strict: true
// `this` in a method of a literal whose contextual type is T & X: the apparent type, C & X (T's constraint), and
// (T & X)['k'] with k only in T distributes to T['k'] & unknown
interface C { kind: string; types?: { output: unknown } }
declare function get<S extends C>(s: S, d?: NonNullable<S['types']>['output']): S;
function f<T extends C>(t: T): T & { extra: 1; run(u: unknown): void } {
  return {
    ...t,
    extra: 1,
    run(u) {
      get(this, u);
    },
  };
}
