// @strict: true
// An object literal's type instantiated by a mapper its members don't mention is the literal's type itself (tsc's
// getObjectTypeInstantiation: no outer type parameters referenced). drizzle's tables are built from columns literals,
// and every generic query-builder step made new, equal copies of them (TS2589 in its type tests)
declare function table<N extends string, C extends Record<string, unknown>>(name: N, columns: C): { name: N; columns: C; _: { cols: C } };
const users = table("users", { id: 1 as const, name: "x" as string, get label() { return "u"; } });
type Cols<T extends { columns: unknown }> = T["columns"];
type Wrap<T, U> = { t: T; u: U; cols: Cols<{ columns: T }> };
declare function wrap<U>(u: U): Wrap<typeof users.columns, U>;
const w = wrap(1);
const same: typeof users.columns = w.cols;
type Keys = keyof typeof w.cols;
const k: Keys = "label";
