// origin: npm freelensapp/freelens @ 088d085 packages/core (tsconfig.bench.json), src/renderer/components/network-port-forwards/port-forward-menu.tsx
// flip: no
// @strict: true
// A homomorphic mapped type over a type parameter constrained to an intersection has the properties of every part,
// an interface whose base is a mapped type (Partial<M>) among them
interface MenuProps { open(): void; id?: string }
interface MAP extends Partial<MenuProps> { toolbar?: boolean }
interface MAP2 extends MenuProps { toolbar?: boolean }
interface Deps { store: number }
function f1<Props extends MAP & Deps>(p: Readonly<Props>) { return p.store; }
function f2<Props extends MAP2 & Deps>(p: Readonly<Props>) { return p.store; }
function f3<Props extends Deps & MAP>(p: Readonly<Props>) { return p.store; }
function f4<Props extends MAP>(p: Readonly<Props>) { return p.toolbar; }
function f5<Props extends MAP & Deps>(p: Readonly<Props>) { return p.toolbar; }
