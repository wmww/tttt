// @strict: true
// An ambient parameter's pattern destructures its type without null and undefined: it's just documentation (tsc's
// getBindingElementTypeFromParentType). Not an interface's or a function type's
declare function f({ a }: { a: number } | undefined, [b]: [string] | null): void;
declare class C {
  m({ c }: { c: number } | undefined, [d]: [number] | undefined): void;
}
declare namespace N {
  function inner([h]: [number] | null): void;
}
type F = ({ e }: { e: number } | undefined) => void;
interface I {
  n([g]: [number] | undefined): void;
}
