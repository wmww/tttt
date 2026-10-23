// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "ES2022", "module": "ESNext", "moduleResolution": "bundler", "types": []}, "files": ["index.ts", "a.ts", "b.ts"]}
// @filename: a.ts
enum K { ID = "id", N = "n" }
enum Num { Zero, One }
declare const sym: unique symbol;
type Item = { [K.ID]: string; [K.N]: number; plain: boolean };
type NumItem = { [Num.Zero]: string; [Num.One]: number };
const k1: never = null! as keyof Item;
const k2: never = null! as keyof Required<Item>;
const k3: never = null! as keyof Partial<Item>;
const k4: never = null! as keyof Pick<Item, K.ID>;
const k5: never = null! as keyof { [P in keyof Item]: Item[P] };
const k6: never = null! as keyof { [P in K]: 1 };
const k7: never = null! as keyof Record<K, 1>;
const k8: never = null! as keyof Required<NumItem>;
const k9: never = null! as keyof Readonly<Required<Item>>;
function r1({ [K.ID]: id, ...rest }: Item) { const x: never = rest; }
function r2({ [K.ID]: id, ...rest }: Required<Item>) { const x: never = rest; }
function r3({ [K.ID]: id, ...rest }: Partial<Item>) { const x: never = rest; }
function r4({ [K.ID]: id, ...rest }: Record<K, 1>) { const x: never = rest; }
function r5({ [K.ID]: id, ...rest }: { [P in K]: P }) { const x: never = rest; }
function r6({ [Num.Zero]: z, ...rest }: Required<NumItem>) { const x: never = rest; }
function r7({ id, ...rest }: Required<Item>) { const x: never = rest; }
function r8({ ["id"]: id, ...rest }: Required<Item>) { const x: never = rest; }
function r9({ [K.ID]: id, ...rest }: Readonly<Required<Item>>) { const x: never = rest; }
function r10({ [K.ID]: id, ...rest }: Required<Item> & { extra: 1 }) { const x: never = rest; }
function r11({ [K.ID]: id, ...rest }: Pick<Item, K.ID | "plain">) { const x: never = rest; }
function r12({ [K.ID]: id, ...rest }: Omit<Item, "plain">) { const x: never = rest; }
export {};
// @filename: b.ts
enum K { ID = "id", N = "n" }
enum L { ID = "id" }
type Item = { [K.ID]: string; [K.N]: number; plain: boolean };
type Same = { [P in keyof Item as P]: Item[P] };
type Filtered = { [P in keyof Item as Exclude<P, K.N>]: Item[P] };
type Prefixed = { [P in K as `x${P}`]: P };
type ToOther = { [P in K as P extends K.ID ? L.ID : P]: P };
type Merged = { [P in K | "id" as P extends K.ID ? "id" : P]: P };
type ToString = { [P in K as `${P}`]: P };
type Two = { [P in K.ID as P | "also"]: P };
const k1: never = null! as keyof Same;
const k2: never = null! as keyof Filtered;
const k3: never = null! as keyof Prefixed;
const k4: never = null! as keyof ToOther;
const k5: never = null! as keyof Merged;
const k6: never = null! as keyof ToString;
const k7: never = null! as keyof Two;
function s1({ [K.ID]: id, ...rest }: Same) { const x: never = rest; }
function s2({ [K.ID]: id, ...rest }: Filtered) { const x: never = rest; }
function s3({ xid, ...rest }: Prefixed) { const x: never = rest; }
function s4({ [K.ID]: id, ...rest }: ToOther) { const x: never = rest; }
function s5({ [L.ID]: id, ...rest }: ToOther) { const x: never = rest; }
function s6({ [K.ID]: id, ...rest }: Merged) { const x: never = rest; }
function s7({ id, ...rest }: Merged) { const x: never = rest; }
function s8({ [K.ID]: id, ...rest }: ToString) { const x: never = rest; }
function s9({ id, ...rest }: ToString) { const x: never = rest; }
function s10({ [K.ID]: id, ...rest }: Two) { const x: never = rest; }
function s11({ [L.ID]: id, ...rest }: Required<Item>) { const x: never = rest; }
function s12<T extends Item>({ [K.ID]: id, ...rest }: Required<T>) { const x: never = rest; }
function s13(o: Required<Item>) { let id, rest; ({ [K.ID]: id, ...rest } = o); const x: never = rest; }
export {};
// @filename: index.ts
enum K {
  ID = "id",
}

type Item = { [K.ID]: string };

export function f({ [K.ID]: id, ...rest }: Required<Item>): Item {
  return { [K.ID]: id, ...rest };
}
