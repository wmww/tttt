// A decorator whose type is a union without common call signatures is not callable as a whole (tsc's resolveDecorator:
// invocationErrorDetails), not resolved member by member (sequelize's `Function | undefined` decorators)
// @strict: true
// @target: esnext
declare const d: Function | undefined;
declare const e: (() => void) | undefined;
declare const f: string | undefined;
class C {
  @d static m() {}
  @e static n() {}
  @f static o() {}
}
d();
e();
