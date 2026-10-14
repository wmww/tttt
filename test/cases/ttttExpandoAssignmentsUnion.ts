// @strict: true
// @noEmit: true
// An expando member assigned in several places is the literal-reduced union of what they assign (tsgo's
// getWidenedTypeForAssignmentDeclaration: getUnionType(types)); subtype reduction would relate `typeof u` to NU and
// read u's own `unregister` while it is being computed (tsx's register.ts, TS7022). And an unbraced `if` branch is no
// block scope container: `if (ns) u.unregister = u;` in a block finds that block's `u` (tsgo's
// bindDeferredExpandoAssignment looks in the blockScopeContainer first), which is no expando host
type NU = (() => Promise<void>) & { unregister: () => Promise<void> };
declare const cond: boolean;
declare const ns: string | undefined;
export const register = () => {
  if (cond) {
    const u = (async () => {}) as NU;
    if (ns) {
      u.unregister = u;
    }
    return u;
  }
  const u = async () => {};
  if (ns) {
    u.unregister = u;
  }
  return u;
}
export const register2 = () => {
  {
    const u = (async () => {}) as NU;
    if (ns) u.unregister = u;
  }
  const u = async () => {};
  u.unregister = u;
  return u;
}
