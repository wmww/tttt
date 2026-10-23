// @strict: true
// Inference to an indexed access `(typeof R)[Key]` goes to its apparent type's signatures and index signatures, not
// its properties (tsc's inferFromProperties takes getPropertiesOfObjectType, none for an indexed access): Key is
// inferred from `key` alone, not "a" from R[key]'s first member Spec<"a"> (openclaw's dreaming.ts)
type K = "a" | "b" | "c";
type Payloads = { a: { x: number }; b: { y: string }; c: { z: boolean } };
type Spec<Key extends K> = { method: string; apply: (payload: Payloads[Key]) => void };
declare const R: { [Key in K]: Spec<Key> };
declare function loadSpec<Key extends K>(key: Key, spec: (typeof R)[Key]): Key;
declare function loadSpec2<Key extends K>(spec: (typeof R)[Key]): Key;
declare function loadSpec3<Key extends K>(spec: Spec<Key>): Key;
type Fns = { [P in K]: (x: Payloads[P]) => P };
declare const fns: Fns;
declare function pick<Key extends K>(f: Fns[Key]): Key;
export function load(key: K) {
    const r1 = loadSpec(key, R[key]);
    const r2 = loadSpec2(R[key]);
    const r3 = loadSpec3(R.a);
    const r4 = pick(fns.b);
    const r5 = pick((x: { y: string }) => "b" as const);
}
