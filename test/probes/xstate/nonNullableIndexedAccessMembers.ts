// The apparent type of an intersection with generic members is the intersection of their apparent types (tsc's
// getApparentTypeOfIntersectionType): NonNullable<T["p"]> with p?: X has X's properties
// @strict: true
interface A { params?: { name: string; }; }
function f<T extends A>(t: Readonly<T>, v: NonNullable<Readonly<T>["params"]>, w: Readonly<T>["params"]) {
  v.name;
  w!.name;
  t.params!.name;
  const x = t.params;
  x!.name;
}
