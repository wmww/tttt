// @strict: true
// A computed member of an object literal whose contextual type is a generic mapped type reads that type's index
// signatures, which its key type's lower bound gives it (tsc's getContextualTypeForObjectLiteralElement: an element
// with a dynamic name takes findApplicableIndexInfo of the apparent type's index infos): `[actionId]: (input) => input`
// in a generic call's inference is typed, no TS7006. openclaw's plugin-sdk/feature-plugin.test.ts
type Op = { kind: "query" | "action"; input: { t: unknown } };
type Contract = { pluginId: string; operations: Readonly<Record<string, Op>> };
type OpName<C extends Contract> = keyof C["operations"] & string;
type Handlers<C extends Contract> = { [K in OpName<C>]: (input: C["operations"][K]["input"]["t"]) => unknown };
declare const actionId: string;
type C1 = { pluginId: "p"; operations: { [x: string]: { input: { t: unknown }; kind: "action" } } };
const h: Handlers<C1> = { [actionId]: (input) => input };
declare function definePlugin<C extends Contract>(d: { contract: C; setup: () => Handlers<C> }): void;
declare const c1: C1;
definePlugin({ contract: c1, setup: () => ({ [actionId]: (input) => input }) });
definePlugin({ contract: c1, setup: () => ({ x: (input) => input }) });
