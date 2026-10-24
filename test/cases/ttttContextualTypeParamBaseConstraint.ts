// @strict: true
// A type parameter's contextual signature comes from its base constraint (tsc's getApparentType), and the base
// constraint of a union with an unconstrained type parameter in it is none: `V extends D | ((eb: EB) => void)` gives
// the callback nothing, TS7006 (kysely's OperandValueExpressionOrList with an `any` database)
interface EB { e: 1 }
declare function h1<D, V extends D | ((eb: EB) => void)>(v: V): void;
h1((eb) => {});
declare function h5<D extends object, V extends D | ((eb: EB) => void)>(v: V): void;
h5((eb) => {});
function k<D>() {
  const x: D | ((eb: EB) => void) = (eb) => {};
}
function k2<D, V extends D | ((eb: EB) => void)>() {
  const x: V = (eb) => {};
}
interface Expression<T> { t: T }
interface QB<DB, TB> { db: DB; tb: TB }
type OperandExpressionFactory<DB, TB extends keyof DB, V> = (eb: QB<DB, TB>) => Expression<V>;
type ValueExpression<DB, TB extends keyof DB, V> = V | Expression<V> | OperandExpressionFactory<DB, TB, V>;
type SelectType<T> = T extends { s: infer S } ? S : T;
type XT<DB, TB extends keyof DB, RE> = SelectType<RE extends string ? DB[TB] : unknown>;
declare class UQB<DB, TB extends keyof DB> {
  where<RE extends string, VE extends ValueExpression<DB, TB, XT<DB, TB, RE> | null>>(lhs: RE, op: string, rhs: VE): UQB<DB, TB>;
}
declare const q: UQB<any, "a">;
q.where('id', '=', (eb) => eb);
declare const q2: UQB<{ a: number }, "a">;
q2.where('id', '=', (eb) => eb);
