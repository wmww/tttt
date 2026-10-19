// @strict: false
// @strictNullChecks: false
// A literal widened for its undefined properties (strictNullChecks off) is no longer an object literal (tsc's
// getWidenedTypeOfObjectLiteral), so an intersection with more properties still fits it: one in a spread's intersection
// `{ a: undefined } & Omit<T, K>` is widened that way only (valibot's expectSchemaIssue under flagflip's sloppy flags)
declare function eq<E>(expected: E): void;
export function f<T extends { kind: string; message: string }>(base: Omit<T, 'input'>, value: unknown) {
  eq({ issues: [{ requirement: undefined, path: undefined, ...base, input: value }] });
}

// (Only there: a union of array literal elements still has the properties some members lack, vscode editTelemetry)
export const rows = [
  { tier: 'a', n: 6 },
  { tier: undefined, n: 3 },
  { tier: undefined, n: 2, modelId: 'x' },
].map(entry => ({ ...entry, modelId: entry.modelId ?? 'auto' }));
