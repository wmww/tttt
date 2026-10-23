// @strict: true
// @exactOptionalPropertyTypes: true,false
// @noEmit: true

// A union property's type takes in the index signatures of the members without the property (tsc's
// createUnionOrIntersectionProperty): through an unknown or undefined-including one, `delete` needs no optional property
type A = { enabled: boolean; k?: string };
declare function n(): A | null;
declare const r: Record<string, unknown>;
const job = n() ?? r;
if (typeof job.k === "string") {
  delete job.enabled;
}
delete job.enabled;

declare const s: A | { [k: string]: string };
delete s.enabled;

declare const u: A | { [k: string]: string | undefined };
delete u.enabled;

declare const a: A | { [k: string]: any };
delete a.enabled;

declare const ro: A | { readonly [k: string]: unknown };
delete ro.enabled;
