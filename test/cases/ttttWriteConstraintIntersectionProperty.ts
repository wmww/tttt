// @strict: true
// Writing T["default"] through an intersection constraint whose member declares the property is the property, not the
// other member's string index signature (tsc's getPropertyOfType on the intersection)
type Comp = (props: {}) => string;
type Mod = Record<string, unknown> & { default?: Comp | null | undefined; };
type Num = { [k: string]: number } & { a: number };
export function f<T extends Mod, N extends Num>(c: Comp, n: number) {
  const x: T["default"] = c;
  const y: T["other"] = c;
  const z: NonNullable<T["default"]> = c;
  const a: N["a"] = n;
  const b: N["b"] = n;
}
