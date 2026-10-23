// @strict: true
// A NoInfer of a union with undefined isn't elaborated into: tsc's getBestMatchIndexedAccessTypeOrUndefined indexes the
// substitution through its apparent type, the union, where undefined has no element, and a substitution is no union to
// pick a best match from (bun's `toEqual<X = T>(expected: NoInfer<X>)` with T `readonly S[] | undefined`, opencode's
// google-transcription test: TS2769 at the argument, not at its elements' properties)
declare const n: number | undefined;
declare const a: any;
interface M<T> { toEqual(expected: T): void; toEqual<X = T>(expected: NoInfer<X>): void }
declare function expect<T>(v: T): M<T>;
declare const segs: readonly { readonly text: string; readonly startSeconds: number; readonly speaker?: string }[] | undefined;
expect(segs).toEqual([
  {
    text: "a",
    startSeconds: n,
    speaker: a,
  },
]);
