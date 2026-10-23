// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "esnext", "module": "esnext", "moduleResolution": "bundler", "types": [], "skipLibCheck": true}, "files": ["index.ts", "a.ts", "sentinel.ts"]}
// @filename: a.ts
enum E { A = "0", B = "b", C = "1.5", D = "-1", F = "1e3", G = "Infinity" }
enum N { Z = 0, O = 1 }
type Item = { [E.A]: 1; [E.B]: 2; [E.C]: 3; [E.D]: 4; [E.F]: 5; [E.G]: 6 };
type Num = { [N.Z]: 1; [N.O]: 2 };
type Plain = { "0": 1; 1: 2; b: 3 };
export function f1({ [E.A]: a, ...rest }: Required<Item>) { const r: never = rest; const k: never = null! as keyof Required<Item>; return { [E.A]: a, ...rest }; }
export function f2({ [E.C]: a, ...rest }: Required<Item>) { const r: never = rest; const k: never = null! as keyof Required<Item>; return { [E.C]: a, ...rest }; }
export function f3({ [N.Z]: a, ...rest }: Required<Num>) { const r: never = rest; const k: never = null! as keyof Required<Num>; return { [N.Z]: a, ...rest }; }
export function f4({ ["0"]: a, ...rest }: Required<Plain>) { const r: never = rest; const k: never = null! as keyof Required<Plain>; return { ["0"]: a, ...rest }; }
export function f5({ [E.A]: a, ...rest }: Partial<Item>) { const r: never = rest; const k: never = null! as keyof Partial<Item>; return { [E.A]: a, ...rest }; }
export function f6({ [E.C]: a, ...rest }: Partial<Item>) { const r: never = rest; const k: never = null! as keyof Partial<Item>; return { [E.C]: a, ...rest }; }
export function f7({ [N.Z]: a, ...rest }: Partial<Num>) { const r: never = rest; const k: never = null! as keyof Partial<Num>; return { [N.Z]: a, ...rest }; }
export function f8({ ["0"]: a, ...rest }: Partial<Plain>) { const r: never = rest; const k: never = null! as keyof Partial<Plain>; return { ["0"]: a, ...rest }; }
export function f9({ [E.A]: a, ...rest }: Readonly<Item>) { const r: never = rest; const k: never = null! as keyof Readonly<Item>; return { [E.A]: a, ...rest }; }
export function f10({ [E.C]: a, ...rest }: Readonly<Item>) { const r: never = rest; const k: never = null! as keyof Readonly<Item>; return { [E.C]: a, ...rest }; }
export function f11({ [N.Z]: a, ...rest }: Readonly<Num>) { const r: never = rest; const k: never = null! as keyof Readonly<Num>; return { [N.Z]: a, ...rest }; }
export function f12({ ["0"]: a, ...rest }: Readonly<Plain>) { const r: never = rest; const k: never = null! as keyof Readonly<Plain>; return { ["0"]: a, ...rest }; }
export function f13({ [E.A]: a, ...rest }: Pick<Item, keyof Item>) { const r: never = rest; const k: never = null! as keyof Pick<Item, keyof Item>; return { [E.A]: a, ...rest }; }
export function f14({ [E.C]: a, ...rest }: Pick<Item, keyof Item>) { const r: never = rest; const k: never = null! as keyof Pick<Item, keyof Item>; return { [E.C]: a, ...rest }; }
export function f15({ [N.Z]: a, ...rest }: Pick<Num, keyof Num>) { const r: never = rest; const k: never = null! as keyof Pick<Num, keyof Num>; return { [N.Z]: a, ...rest }; }
export function f16({ ["0"]: a, ...rest }: Pick<Plain, keyof Plain>) { const r: never = rest; const k: never = null! as keyof Pick<Plain, keyof Plain>; return { ["0"]: a, ...rest }; }
export function f17({ [E.A]: a, ...rest }: { [K in keyof Item]: Item[K] }) { const r: never = rest; const k: never = null! as keyof { [K in keyof Item]: Item[K] }; return { [E.A]: a, ...rest }; }
export function f18({ [E.C]: a, ...rest }: { [K in keyof Item]: Item[K] }) { const r: never = rest; const k: never = null! as keyof { [K in keyof Item]: Item[K] }; return { [E.C]: a, ...rest }; }
export function f19({ [N.Z]: a, ...rest }: { [K in keyof Num]: Num[K] }) { const r: never = rest; const k: never = null! as keyof { [K in keyof Num]: Num[K] }; return { [N.Z]: a, ...rest }; }
export function f20({ ["0"]: a, ...rest }: { [K in keyof Plain]: Plain[K] }) { const r: never = rest; const k: never = null! as keyof { [K in keyof Plain]: Plain[K] }; return { ["0"]: a, ...rest }; }
export function f21({ [E.A]: a, ...rest }: { [K in keyof Item as K]: Item[K] }) { const r: never = rest; const k: never = null! as keyof { [K in keyof Item as K]: Item[K] }; return { [E.A]: a, ...rest }; }
export function f22({ [E.C]: a, ...rest }: { [K in keyof Item as K]: Item[K] }) { const r: never = rest; const k: never = null! as keyof { [K in keyof Item as K]: Item[K] }; return { [E.C]: a, ...rest }; }
export function f23({ [N.Z]: a, ...rest }: { [K in keyof Num as K]: Num[K] }) { const r: never = rest; const k: never = null! as keyof { [K in keyof Num as K]: Num[K] }; return { [N.Z]: a, ...rest }; }
export function f24({ ["0"]: a, ...rest }: { [K in keyof Plain as K]: Plain[K] }) { const r: never = rest; const k: never = null! as keyof { [K in keyof Plain as K]: Plain[K] }; return { ["0"]: a, ...rest }; }
export function f25({ [E.A]: a, ...rest }: Record<keyof Item, 1>) { const r: never = rest; const k: never = null! as keyof Record<keyof Item, 1>; return { [E.A]: a, ...rest }; }
export function f26({ [E.C]: a, ...rest }: Record<keyof Item, 1>) { const r: never = rest; const k: never = null! as keyof Record<keyof Item, 1>; return { [E.C]: a, ...rest }; }
export function f27({ [N.Z]: a, ...rest }: Record<keyof Num, 1>) { const r: never = rest; const k: never = null! as keyof Record<keyof Num, 1>; return { [N.Z]: a, ...rest }; }
export function f28({ ["0"]: a, ...rest }: Record<keyof Plain, 1>) { const r: never = rest; const k: never = null! as keyof Record<keyof Plain, 1>; return { ["0"]: a, ...rest }; }
export function f29({ [E.A]: a, ...rest }: Required<Partial<Item>>) { const r: never = rest; const k: never = null! as keyof Required<Partial<Item>>; return { [E.A]: a, ...rest }; }
export function f30({ [E.C]: a, ...rest }: Required<Partial<Item>>) { const r: never = rest; const k: never = null! as keyof Required<Partial<Item>>; return { [E.C]: a, ...rest }; }
export function f31({ [N.Z]: a, ...rest }: Required<Partial<Num>>) { const r: never = rest; const k: never = null! as keyof Required<Partial<Num>>; return { [N.Z]: a, ...rest }; }
export function f32({ ["0"]: a, ...rest }: Required<Partial<Plain>>) { const r: never = rest; const k: never = null! as keyof Required<Partial<Plain>>; return { ["0"]: a, ...rest }; }
// @filename: index.ts
enum E { A = "0", B = "b", C = "1.5" }
type M = Required<{ [E.A]: 1; [E.B]: 2; [E.C]: 3 }>;
const k: E = null! as keyof M;
const n: never = null! as keyof M;
export function f({ [E.A]: a, ...rest }: M) { const r: never = rest; return { [E.A]: a, ...rest }; }
// @filename: sentinel.ts
export const sentinel: number = "";
