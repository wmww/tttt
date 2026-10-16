// A union made with subtype reduction (inferred return types) keeps an origin of the named unions it was made of when
// they and the members outside them don't overlap and are all left (tsc's getUnionTypeWorker after removeSubtypes):
// `Exit | Primitive`, a narrowed subtype of both dropped (effect's fiberRuntime `Exit.Exit<any, never> | core.Primitive`)
// @strict: true
// @declaration: true
interface A { _op: "a" } interface B { _op: "b" } interface F { _op: "f"; _tag: "Failure" }
interface ExitS { _tag: "Success"; value: number } interface ExitF { _tag: "Failure"; cause: string }
type Exit = ExitS | ExitF;
export type Primitive = A | B | F;
declare function isExit(u: unknown): u is Exit;
declare function get(): Primitive;
export function run() {
  const p = get();
  if (!isExit(p)) return p;
  return null! as Exit;
}
export function run2() {
  const p = get();
  if (!isExit(p)) { return p; } else if (p._tag === "Failure") { return p; }
  return null! as Exit;
}
