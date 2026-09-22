// @strict: true
declare const enum K { A, B }
interface PA { readonly kind: K.A; name: string; init: number }
interface SPA { readonly kind: K.B; name: string; oinit: number }
type NE = PA & { readonly name: "n" } | SPA & { readonly oinit: 1 };
type P = Pick<PA & { readonly name: "n" }, "kind" | "name">;
type PK = P["kind"];
type E = Extract<NE, P>;
declare function t<T extends NE>(node: T): Extract<NE, Pick<T, "kind" | keyof T & "op" | keyof T & "name">>;
declare let pa: PA & { readonly name: "n" };
pa = t(pa);
type X1 = (SPA & { readonly oinit: 1 }) extends P ? 1 : 0;
type X2 = SPA extends P ? 1 : 0;
type X3 = { kind: K.B } extends { kind: K.A } ? 1 : 0;
type X4 = { readonly kind: K.B, name: string } extends { readonly kind: K.A, readonly name: "n" } ? 1 : 0;
declare const e: E;
pa = e;
type K2<T> = "kind" | keyof T & "op" | keyof T & "name";
type K3 = K2<PA & { readonly name: "n" }>;
type E2<T> = Extract<NE, Pick<T, "kind" | "name">>;
declare const e2: E2<PA & { readonly name: "n" }>;
pa = e2;
