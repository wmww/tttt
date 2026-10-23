// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "esnext", "module": "esnext", "moduleResolution": "bundler", "types": [], "skipLibCheck": true}, "files": ["index.ts", "declared__spread.ts", "declared__spread_const.ts", "declared__spread_extra.ts", "declared__spread_before.ts", "declared__spread_twice.ts", "declared__spread_optional.ts", "declared__spread_of_spread.ts", "declared__rest.ts", "declared__rest_direct.ts", "declared__function_rest.ts", "declared__reverse.ts", "declared__reverse_readonly.ts", "declared__reverse_optional.ts", "declared__reverse_boxed.ts", "declared__reverse_limited.ts", "declared__reverse_spread.ts", "declared__reverse_twice.ts", "declared_num__spread_const.ts", "declared_num__spread_before.ts", "declared_num__rest.ts", "declared_num__reverse.ts", "declared_num__reverse_readonly.ts", "declared_num__reverse_spread.ts", "required__spread_const.ts", "required__spread_extra.ts", "required__spread_of_spread.ts", "required__reverse.ts", "required__reverse_limited.ts", "partial__spread_const.ts", "partial__spread_optional.ts", "partial__function_rest.ts", "partial__reverse.ts", "partial__reverse_boxed.ts", "readonly__spread.ts", "readonly__spread_const.ts", "readonly__spread_twice.ts", "readonly__rest_direct.ts", "readonly__reverse.ts", "readonly__reverse_optional.ts", "readonly__reverse_twice.ts", "record__spread_const.ts", "record__spread_before.ts", "record__rest.ts", "record__reverse.ts", "record__reverse_readonly.ts", "record__reverse_spread.ts", "record_num__spread_const.ts", "record_num__spread_extra.ts", "record_num__spread_of_spread.ts", "record_num__reverse.ts", "record_num__reverse_limited.ts", "pick__spread_const.ts", "pick__spread_optional.ts", "pick__function_rest.ts", "pick__reverse.ts", "pick__reverse_boxed.ts", "own__spread.ts", "own__spread_const.ts", "own__spread_twice.ts", "own__rest_direct.ts", "own__reverse.ts", "own__reverse_optional.ts", "own__reverse_twice.ts", "as_clause__spread_const.ts", "as_clause__spread_before.ts", "as_clause__rest.ts", "as_clause__reverse.ts", "as_clause__reverse_readonly.ts", "as_clause__reverse_spread.ts", "intersected__spread_const.ts", "intersected__spread_extra.ts", "intersected__spread_of_spread.ts", "intersected__reverse.ts", "intersected__reverse_limited.ts", "twice__spread_const.ts", "twice__spread_optional.ts", "twice__function_rest.ts", "twice__reverse.ts", "twice__reverse_boxed.ts", "interface__spread.ts", "interface__spread_const.ts", "interface__spread_twice.ts", "interface__rest_direct.ts", "interface__reverse.ts", "interface__reverse_optional.ts", "interface__reverse_twice.ts", "class__spread_const.ts", "class__spread_before.ts", "class__rest.ts", "class__reverse.ts", "class__reverse_readonly.ts", "class__reverse_spread.ts", "plain__spread_const.ts", "plain__spread_extra.ts", "plain__spread_of_spread.ts", "plain__reverse.ts", "plain__reverse_limited.ts", "sentinel.ts"]}
// @filename: as_clause__rest.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { [P in keyof Item as P]: Item[P] };
declare const x2: { [P in keyof Item as P]: Item[P] };
const { extra, ...y } = { ...x, extra: 1 };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: as_clause__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { [P in keyof Item as P]: Item[P] };
declare const x2: { [P in keyof Item as P]: Item[P] };
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: as_clause__reverse_readonly.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { [P in keyof Item as P]: Item[P] };
declare const x2: { [P in keyof Item as P]: Item[P] };
declare function f<T>(x: { readonly [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: as_clause__reverse_spread.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { [P in keyof Item as P]: Item[P] };
declare const x2: { [P in keyof Item as P]: Item[P] };
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = { ...f(x) };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: as_clause__spread_before.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { [P in keyof Item as P]: Item[P] };
declare const x2: { [P in keyof Item as P]: Item[P] };
const y = { extra: 1, ...x };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: as_clause__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { [P in keyof Item as P]: Item[P] };
declare const x2: { [P in keyof Item as P]: Item[P] };
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: class__rest.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare class C { [K.ID]: string; [K.N]: number }
declare const x: C;
declare const x2: C;
const { extra, ...y } = { ...x, extra: 1 };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: class__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare class C { [K.ID]: string; [K.N]: number }
declare const x: C;
declare const x2: C;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: class__reverse_readonly.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare class C { [K.ID]: string; [K.N]: number }
declare const x: C;
declare const x2: C;
declare function f<T>(x: { readonly [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: class__reverse_spread.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare class C { [K.ID]: string; [K.N]: number }
declare const x: C;
declare const x2: C;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = { ...f(x) };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: class__spread_before.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare class C { [K.ID]: string; [K.N]: number }
declare const x: C;
declare const x2: C;
const y = { extra: 1, ...x };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: class__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare class C { [K.ID]: string; [K.N]: number }
declare const x: C;
declare const x2: C;
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__function_rest.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
function f({ ...y }: typeof x) { return y; }
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__rest.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
const { extra, ...y } = { ...x, extra: 1 };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__rest_direct.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
const { ...y } = x;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__reverse_boxed.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
declare function f<T>(x: { [P in keyof T]: T[P] | null }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__reverse_limited.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
declare function f<T>(x: { [P in keyof T & (K.ID | K.N | M.Z | "0" | "b")]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__reverse_optional.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
declare function f<T>(x: { [P in keyof T]?: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__reverse_readonly.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
declare function f<T>(x: { readonly [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__reverse_spread.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = { ...f(x) };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__reverse_twice.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(f(x));
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__spread.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
const y = { ...x };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__spread_before.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
const y = { extra: 1, ...x };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__spread_extra.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
const y = { ...x, extra: 1 };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__spread_of_spread.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
const y = { ...{ ...x } };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__spread_optional.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
const y = { ...(c ? x : undefined) };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared__spread_twice.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item;
declare const x2: Item;
const y = { ...x, ...x2 };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared_num__rest.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Num;
declare const x2: Num;
const { extra, ...y } = { ...x, extra: 1 };
const p1: never = null! as keyof typeof y;
const p2: M = null! as keyof typeof y;
const { [M.Z]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared_num__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Num;
declare const x2: Num;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: M = null! as keyof typeof y;
const { [M.Z]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared_num__reverse_readonly.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Num;
declare const x2: Num;
declare function f<T>(x: { readonly [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: M = null! as keyof typeof y;
const { [M.Z]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared_num__reverse_spread.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Num;
declare const x2: Num;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = { ...f(x) };
const p1: never = null! as keyof typeof y;
const p2: M = null! as keyof typeof y;
const { [M.Z]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared_num__spread_before.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Num;
declare const x2: Num;
const y = { extra: 1, ...x };
const p1: never = null! as keyof typeof y;
const p2: M = null! as keyof typeof y;
const { [M.Z]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: declared_num__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Num;
declare const x2: Num;
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: M = null! as keyof typeof y;
const { [M.Z]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: index.ts
enum K { ID = "id", N = "0" }
type Item = { [K.ID]: string; [K.N]: number };
declare const x: Required<Item>;
declare const r: Readonly<Item>;
const y = { ...x } as const;
const k: K = null! as keyof typeof y;
const p1: never = null! as keyof typeof y;
const z = { ...r };
const p2: never = null! as keyof typeof z;
const w = { ...x, extra: 1 };
const p3: never = null! as keyof typeof w;
function g<T extends Required<Item>>(t: T) { const s = { ...t }; const p4: never = null! as keyof typeof s; }
const { [K.ID]: id, ...rest } = { ...x } as const;
const p5: never = rest;
export {};
// @filename: interface__rest_direct.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
interface I { [K.ID]: string; [K.N]: number }
declare const x: I;
declare const x2: I;
const { ...y } = x;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: interface__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
interface I { [K.ID]: string; [K.N]: number }
declare const x: I;
declare const x2: I;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: interface__reverse_optional.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
interface I { [K.ID]: string; [K.N]: number }
declare const x: I;
declare const x2: I;
declare function f<T>(x: { [P in keyof T]?: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: interface__reverse_twice.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
interface I { [K.ID]: string; [K.N]: number }
declare const x: I;
declare const x2: I;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(f(x));
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: interface__spread.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
interface I { [K.ID]: string; [K.N]: number }
declare const x: I;
declare const x2: I;
const y = { ...x };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: interface__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
interface I { [K.ID]: string; [K.N]: number }
declare const x: I;
declare const x2: I;
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: interface__spread_twice.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
interface I { [K.ID]: string; [K.N]: number }
declare const x: I;
declare const x2: I;
const y = { ...x, ...x2 };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: intersected__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item & { plain: 1 };
declare const x2: Item & { plain: 1 };
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: intersected__reverse_limited.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item & { plain: 1 };
declare const x2: Item & { plain: 1 };
declare function f<T>(x: { [P in keyof T & (K.ID | K.N | M.Z | "0" | "b")]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: intersected__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item & { plain: 1 };
declare const x2: Item & { plain: 1 };
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: intersected__spread_extra.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item & { plain: 1 };
declare const x2: Item & { plain: 1 };
const y = { ...x, extra: 1 };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: intersected__spread_of_spread.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Item & { plain: 1 };
declare const x2: Item & { plain: 1 };
const y = { ...{ ...x } };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: own__rest_direct.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { [P in K]: P };
declare const x2: { [P in K]: P };
const { ...y } = x;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: own__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { [P in K]: P };
declare const x2: { [P in K]: P };
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: own__reverse_optional.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { [P in K]: P };
declare const x2: { [P in K]: P };
declare function f<T>(x: { [P in keyof T]?: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: own__reverse_twice.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { [P in K]: P };
declare const x2: { [P in K]: P };
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(f(x));
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: own__spread.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { [P in K]: P };
declare const x2: { [P in K]: P };
const y = { ...x };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: own__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { [P in K]: P };
declare const x2: { [P in K]: P };
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: own__spread_twice.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { [P in K]: P };
declare const x2: { [P in K]: P };
const y = { ...x, ...x2 };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: partial__function_rest.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Partial<Item>;
declare const x2: Partial<Item>;
function f({ ...y }: typeof x) { return y; }
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: partial__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Partial<Item>;
declare const x2: Partial<Item>;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: partial__reverse_boxed.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Partial<Item>;
declare const x2: Partial<Item>;
declare function f<T>(x: { [P in keyof T]: T[P] | null }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: partial__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Partial<Item>;
declare const x2: Partial<Item>;
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: partial__spread_optional.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Partial<Item>;
declare const x2: Partial<Item>;
const y = { ...(c ? x : undefined) };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: pick__function_rest.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Pick<Item, K.ID | K.N>;
declare const x2: Pick<Item, K.ID | K.N>;
function f({ ...y }: typeof x) { return y; }
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: pick__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Pick<Item, K.ID | K.N>;
declare const x2: Pick<Item, K.ID | K.N>;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: pick__reverse_boxed.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Pick<Item, K.ID | K.N>;
declare const x2: Pick<Item, K.ID | K.N>;
declare function f<T>(x: { [P in keyof T]: T[P] | null }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: pick__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Pick<Item, K.ID | K.N>;
declare const x2: Pick<Item, K.ID | K.N>;
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: pick__spread_optional.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Pick<Item, K.ID | K.N>;
declare const x2: Pick<Item, K.ID | K.N>;
const y = { ...(c ? x : undefined) };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: plain__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { "0": 1; 1: 2; b: 3; "x y": 4 };
declare const x2: { "0": 1; 1: 2; b: 3; "x y": 4 };
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: string = null! as keyof typeof y;
const { ["0"]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: plain__reverse_limited.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { "0": 1; 1: 2; b: 3; "x y": 4 };
declare const x2: { "0": 1; 1: 2; b: 3; "x y": 4 };
declare function f<T>(x: { [P in keyof T & (K.ID | K.N | M.Z | "0" | "b")]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: string = null! as keyof typeof y;
const { ["0"]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: plain__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { "0": 1; 1: 2; b: 3; "x y": 4 };
declare const x2: { "0": 1; 1: 2; b: 3; "x y": 4 };
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: string = null! as keyof typeof y;
const { ["0"]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: plain__spread_extra.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { "0": 1; 1: 2; b: 3; "x y": 4 };
declare const x2: { "0": 1; 1: 2; b: 3; "x y": 4 };
const y = { ...x, extra: 1 };
const p1: never = null! as keyof typeof y;
const p2: string = null! as keyof typeof y;
const { ["0"]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: plain__spread_of_spread.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: { "0": 1; 1: 2; b: 3; "x y": 4 };
declare const x2: { "0": 1; 1: 2; b: 3; "x y": 4 };
const y = { ...{ ...x } };
const p1: never = null! as keyof typeof y;
const p2: string = null! as keyof typeof y;
const { ["0"]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: readonly__rest_direct.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Readonly<Item>;
declare const x2: Readonly<Item>;
const { ...y } = x;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: readonly__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Readonly<Item>;
declare const x2: Readonly<Item>;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: readonly__reverse_optional.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Readonly<Item>;
declare const x2: Readonly<Item>;
declare function f<T>(x: { [P in keyof T]?: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: readonly__reverse_twice.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Readonly<Item>;
declare const x2: Readonly<Item>;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(f(x));
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: readonly__spread.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Readonly<Item>;
declare const x2: Readonly<Item>;
const y = { ...x };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: readonly__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Readonly<Item>;
declare const x2: Readonly<Item>;
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: readonly__spread_twice.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Readonly<Item>;
declare const x2: Readonly<Item>;
const y = { ...x, ...x2 };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: record__rest.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Record<K, 1>;
declare const x2: Record<K, 1>;
const { extra, ...y } = { ...x, extra: 1 };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: record__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Record<K, 1>;
declare const x2: Record<K, 1>;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: record__reverse_readonly.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Record<K, 1>;
declare const x2: Record<K, 1>;
declare function f<T>(x: { readonly [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: record__reverse_spread.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Record<K, 1>;
declare const x2: Record<K, 1>;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = { ...f(x) };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: record__spread_before.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Record<K, 1>;
declare const x2: Record<K, 1>;
const y = { extra: 1, ...x };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: record__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Record<K, 1>;
declare const x2: Record<K, 1>;
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: record_num__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Record<M, 1>;
declare const x2: Record<M, 1>;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: M = null! as keyof typeof y;
const { [M.Z]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: record_num__reverse_limited.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Record<M, 1>;
declare const x2: Record<M, 1>;
declare function f<T>(x: { [P in keyof T & (K.ID | K.N | M.Z | "0" | "b")]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: M = null! as keyof typeof y;
const { [M.Z]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: record_num__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Record<M, 1>;
declare const x2: Record<M, 1>;
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: M = null! as keyof typeof y;
const { [M.Z]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: record_num__spread_extra.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Record<M, 1>;
declare const x2: Record<M, 1>;
const y = { ...x, extra: 1 };
const p1: never = null! as keyof typeof y;
const p2: M = null! as keyof typeof y;
const { [M.Z]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: record_num__spread_of_spread.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Record<M, 1>;
declare const x2: Record<M, 1>;
const y = { ...{ ...x } };
const p1: never = null! as keyof typeof y;
const p2: M = null! as keyof typeof y;
const { [M.Z]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: required__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Required<Item>;
declare const x2: Required<Item>;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: required__reverse_limited.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Required<Item>;
declare const x2: Required<Item>;
declare function f<T>(x: { [P in keyof T & (K.ID | K.N | M.Z | "0" | "b")]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: required__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Required<Item>;
declare const x2: Required<Item>;
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: required__spread_extra.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Required<Item>;
declare const x2: Required<Item>;
const y = { ...x, extra: 1 };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: required__spread_of_spread.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Required<Item>;
declare const x2: Required<Item>;
const y = { ...{ ...x } };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: sentinel.ts
export const sentinel: number = "";
// @filename: twice__function_rest.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Required<Partial<Item>>;
declare const x2: Required<Partial<Item>>;
function f({ ...y }: typeof x) { return y; }
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: twice__reverse.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Required<Partial<Item>>;
declare const x2: Required<Partial<Item>>;
declare function f<T>(x: { [P in keyof T]: T[P] }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: twice__reverse_boxed.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Required<Partial<Item>>;
declare const x2: Required<Partial<Item>>;
declare function f<T>(x: { [P in keyof T]: T[P] | null }): T;
const y = f(x);
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: twice__spread_const.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Required<Partial<Item>>;
declare const x2: Required<Partial<Item>>;
const y = { ...x } as const;
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
// @filename: twice__spread_optional.ts
enum K { ID = "id", N = "0", X = "x y" }
enum M { Z = 0, O = 1 }
type Item = { [K.ID]: string; [K.N]: number; [K.X]: boolean };
type Num = { [M.Z]: string; [M.O]: number };
declare const c: boolean;
declare const x: Required<Partial<Item>>;
declare const x2: Required<Partial<Item>>;
const y = { ...(c ? x : undefined) };
const p1: never = null! as keyof typeof y;
const p2: K = null! as keyof typeof y;
const { [K.ID]: taken, ...left } = y;
const p3: never = left;
const p4: never = y;
export {};
