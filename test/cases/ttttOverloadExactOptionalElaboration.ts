// @strict: true
// @exactOptionalPropertyTypes: true
// @target: es2022
// An overload candidate's elaborateElementwise TS2412 is one of its errors (tsgo collects it into the candidate's
// diagnostics; TypeScript 6 logged it directly next to TS2769)
interface IMd { readonly value: string; readonly isTrusted?: boolean }
declare class Md implements IMd { value: string; isTrusted?: boolean | undefined }
interface Box { readonly md?: { markdown: IMd }[] }
interface Base { readonly message: string; readonly custom?: boolean | Box }
interface P<T> extends Base { readonly buttons?: { label: string; run(): T }[]; readonly cancelButton?: { run(): T } | true | string }
interface PC<T> extends P<T> { readonly cancelButton: { run(): T } }
interface PD<T> extends P<T> { readonly cancelButton: true | string }
interface S {
  prompt<T>(p: PC<T>): Promise<T | undefined>;
  prompt<T>(p: PD<T>): Promise<T>;
  prompt<T>(p: P<T>): Promise<T>;
}
interface S1 { prompt<T>(p: P<T>): Promise<T>; }
declare const s: S;
declare const s1: S1;
declare const detail: string | Md;
declare function isString(x: unknown): x is string;
async function f() {
  await s.prompt({
    message: "m",
    custom: isString(detail) ? undefined : { md: [{ markdown: detail }] },
    cancelButton: { run: () => 1 },
  });
  await s1.prompt({
    message: "m",
    custom: isString(detail) ? undefined : { md: [{ markdown: detail }] },
  });
}
