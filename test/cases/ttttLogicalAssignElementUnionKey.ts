// A logical assignment to an element access writes it (tsc's checkElementAccessExpression takes AccessFlags.Writing for
// any assignment target): a union key is the intersection of the properties' types, never here (babel, vite, renovate's
// `o[key] ??= v` under @ts-expect-error), unlike a property access, which reads its read type there
// @strict: true
declare const o: { a: string; b: number };
declare const k: 'a' | 'b';
o[k] ??= 1;
o[k] ||= "s";
o[k] &&= 2;
declare const p: { a?: string; b?: number };
p[k] ??= 1;
o["a"] ??= 1;
const r = (p[k] ??= 1);
