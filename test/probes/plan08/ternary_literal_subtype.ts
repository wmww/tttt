// @strict: true
interface R { skipped: boolean; diags: readonly string[]; files?: string[] }
declare const emptyArray: never[];
declare function emit(): R;
declare const only: boolean;
const r = only ? { skipped: true, diags: emptyArray } : emit();
r.files;
