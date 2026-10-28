// @strict: true
// An earlier parameter's default reads a later unannotated parameter of a contextually typed function (bun 99-V4
// getTypeAtPosition__01/08, instantiateContextualSignature__01): tsc's assignContextualParameterTypes checks the
// default before it assigns the later one, whose getTypeOfSymbol is then its type in the contextual signature before
// the inference fixed it (`T`), not cached (isParameterOfContextSensitiveSignature); it is assigned its instantiated
// type after. tttt typed it implicitly any (TS7006) for good. (The types baseline of the `b` in the default shows tsc's
// later type, number, where tttt keeps the checked expression's `T`: types only)
export {}
declare function pair<T>(first: T, cb: (a: T, b: T) => void): void;
pair(1, (a = b, b) => {
  const first: never = a;
  const second: never = b;
});
declare function accept(f: (x: number) => unknown): number;
declare function use(cb: (a: number, b: string) => void): void;
use(function me(a = accept(me), b) {
  const first: never = a;
  const second: never = b;
  return a;
});
