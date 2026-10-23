// @noTypesAndSymbols: true
// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2023"], "skipLibCheck": true } }
// @filename: a.ts
type O1<K extends string> = { [P in K]: (data: P) => void };
function f1_ab<T extends "a" | "b">(c: O1<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f1_a<T extends "a">(c: O1<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f1_string<T extends string>(c: O1<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f1_abc<T extends "a" | "b" | "c">(c: O1<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
type O2<K extends string> = { db: K } & { [P in K]: (data: P) => void };
function f2_ab<T extends "a" | "b">(c: O2<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f2_a<T extends "a">(c: O2<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f2_string<T extends string>(c: O2<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f2_abc<T extends "a" | "b" | "c">(c: O2<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
type O3<K extends string> = { [P in K]: (data: P) => void } & { db: K };
function f3_ab<T extends "a" | "b">(c: O3<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f3_a<T extends "a">(c: O3<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f3_string<T extends string>(c: O3<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f3_abc<T extends "a" | "b" | "c">(c: O3<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
type O4<K extends string> = { [P in K]: (data: P) => void } & { [P in K]: (data: P) => void };
function f4_ab<T extends "a" | "b">(c: O4<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f4_a<T extends "a">(c: O4<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f4_string<T extends string>(c: O4<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f4_abc<T extends "a" | "b" | "c">(c: O4<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
type O5<K extends string> = { [P in K]: (data: P) => void } & { [P in K]: (data: P) => P };
function f5_ab<T extends "a" | "b">(c: O5<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f5_a<T extends "a">(c: O5<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f5_string<T extends string>(c: O5<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f5_abc<T extends "a" | "b" | "c">(c: O5<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
type O6<K extends string> = { db: K } & { [P in K]?: (data: P) => void };
function f6_ab<T extends "a" | "b">(c: O6<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f6_a<T extends "a">(c: O6<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f6_string<T extends string>(c: O6<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f6_abc<T extends "a" | "b" | "c">(c: O6<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
type O7<K extends string> = { db: K } & { readonly [P in K]: (data: P) => void };
function f7_ab<T extends "a" | "b">(c: O7<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f7_a<T extends "a">(c: O7<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f7_string<T extends string>(c: O7<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f7_abc<T extends "a" | "b" | "c">(c: O7<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
type O8<K extends string> = { db: K } & { [P in K as `on_${P}`]: (data: P) => void };
function f8_ab<T extends "a" | "b">(c: O8<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f8_a<T extends "a">(c: O8<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f8_string<T extends string>(c: O8<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f8_abc<T extends "a" | "b" | "c">(c: O8<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
type O9<K extends string> = { db: K } & { [P in K]: P };
function f9_ab<T extends "a" | "b">(c: O9<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f9_a<T extends "a">(c: O9<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f9_string<T extends string>(c: O9<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f9_abc<T extends "a" | "b" | "c">(c: O9<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
type O10<K extends string> = { db: K } & { [P in K]: { value: P } };
function f10_ab<T extends "a" | "b">(c: O10<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f10_a<T extends "a">(c: O10<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f10_string<T extends string>(c: O10<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f10_abc<T extends "a" | "b" | "c">(c: O10<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
type O11<K extends string> = { db: K } & { n: number } & { [P in K]: (data: P) => void };
function f11_ab<T extends "a" | "b">(c: O11<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f11_a<T extends "a">(c: O11<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f11_string<T extends string>(c: O11<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f11_abc<T extends "a" | "b" | "c">(c: O11<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
type O12<K extends string> = K extends string ? { db: K } & { [P in K]: (data: P) => void } : never;
function f12_ab<T extends "a" | "b">(c: O12<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f12_a<T extends "a">(c: O12<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f12_string<T extends string>(c: O12<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f12_abc<T extends "a" | "b" | "c">(c: O12<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
type O13<K extends string> = { [key: string]: unknown } & { [P in K]: (data: P) => void };
function f13_ab<T extends "a" | "b">(c: O13<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f13_a<T extends "a">(c: O13<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f13_string<T extends string>(c: O13<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f13_abc<T extends "a" | "b" | "c">(c: O13<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
type O14<K extends string> = { db: K } & Record<K, (data: K) => void>;
function f14_ab<T extends "a" | "b">(c: O14<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f14_a<T extends "a">(c: O14<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f14_string<T extends string>(c: O14<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f14_abc<T extends "a" | "b" | "c">(c: O14<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
type O15<K extends string> = { db: K } & Pick<{ a: (data: "a") => void; b: (data: "b") => void; c: (data: "c") => void }, K>;
function f15_ab<T extends "a" | "b">(c: O15<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f15_a<T extends "a">(c: O15<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f15_string<T extends string>(c: O15<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function f15_abc<T extends "a" | "b" | "c">(c: O15<T>, name: T) { const member = c[name]; const probe: never = member; (c[name] as any); c[name]("a"); c[name](name); const asA = c["a" as T]; const p2: never = asA; }
function tuples<T extends unknown[]>(t: [string, ...T], u: [...T, number], i: 0 | 1) { const a: never = t[1]; const b: never = t[i]; const c: never = u[0]; const d: never = t[0]; }
const sentinel: number = "";
export {};
