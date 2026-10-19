// @strict: true
// A spread overwrites what its type has as properties, not what every object has from Object (tsc's
// checkSpreadPropOverrides goes by getPropertiesOfType): `{ toString, ...ctx }` keeps its `toString` (hono's css test)
interface Ctx { css: string; style(): void }
interface Named { toString(): string; css: string }
declare const ctx: Ctx;
declare const named: Named;
declare const fn: () => void;
const toString = () => "";
const call = 1;

export const a = { toString, ...ctx };
export const b = { toString, ...named };
export const c = { call, ...fn };
export const d = { css: "", ...ctx };
