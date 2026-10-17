// @strict: true
// A deferred function body (plan 78) assigns a module's `let`, which an arrow function walked up front (an expression
// body) reads: another file's check, which comes first, asks for its return type. The assignment marks
// (symbol_assignments()) have to be there, or the `let` narrows to its initial `undefined` (run with baseline_tests
// --defer-bodies)
// @filename: a.ts
import "./b";
let context: { name: string } | undefined = undefined;
export const getContext = () => context;
export function init() {
    context ||= { name: "a" };
}
// @filename: b.ts
import { getContext } from "./a";
const found = getContext();
export const name: string | undefined = found?.name;
