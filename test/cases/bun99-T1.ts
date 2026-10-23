// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2023"], "jsx": "preserve", "module": "esnext", "moduleResolution": "bundler", "skipLibCheck": true}, "files": ["first.ts", "late.ts", "noinfer_u__instanceof.ts", "noinfer_u__not_string.ts", "noinfer_u__truthy.ts", "param__instanceof.ts", "param__not_string.ts", "param__truthy.ts", "plain__instanceof.ts", "plain__not_string.ts", "plain__truthy.ts", "jsx_noinfer_u__instanceof.tsx", "jsx_param__instanceof.tsx", "jsx_plain__instanceof.tsx", "jsx_noinfer_num__instanceof.tsx", "jsx.d.ts", "sentinel.ts"]}
// @filename: first.ts
import { v, w } from "./late";
export const p1: never = v;
export const p2: never = w;
// @filename: jsx.d.ts
declare namespace JSX { interface Element {} interface IntrinsicElements { div: any } }
// @filename: jsx_noinfer_num__instanceof.tsx
class K { k = 1 }
interface O { o: 1 }
declare function G<T>(props: T): JSX.Element;
declare function N(props: object): JSX.Element;
declare const c: boolean;
export function f(k: NoInfer<K | number> | null) {
  const a = k instanceof K ? <G {...k} /> : undefined;
  const b = k instanceof K ? <G s={1} {...k} /> : undefined;
  const d = k instanceof K ? <N {...k} /> : undefined;
  const e = k instanceof K ? <div {...k} /> : undefined;
  const z: never = 1;
}
// @filename: jsx_noinfer_u__instanceof.tsx
class K { k = 1 }
interface O { o: 1 }
declare function G<T>(props: T): JSX.Element;
declare function N(props: object): JSX.Element;
declare const c: boolean;
export function f(k: NoInfer<K | string | undefined> | undefined) {
  const a = k instanceof K ? <G {...k} /> : undefined;
  const b = k instanceof K ? <G s={1} {...k} /> : undefined;
  const d = k instanceof K ? <N {...k} /> : undefined;
  const e = k instanceof K ? <div {...k} /> : undefined;
  const z: never = 1;
}
// @filename: jsx_param__instanceof.tsx
class K { k = 1 }
interface O { o: 1 }
declare function G<T>(props: T): JSX.Element;
declare function N(props: object): JSX.Element;
declare const c: boolean;
export function f<P extends K | string | undefined>(k: P) {
  const a = k instanceof K ? <G {...k} /> : undefined;
  const b = k instanceof K ? <G s={1} {...k} /> : undefined;
  const d = k instanceof K ? <N {...k} /> : undefined;
  const e = k instanceof K ? <div {...k} /> : undefined;
  const z: never = 1;
}
// @filename: jsx_plain__instanceof.tsx
class K { k = 1 }
interface O { o: 1 }
declare function G<T>(props: T): JSX.Element;
declare function N(props: object): JSX.Element;
declare const c: boolean;
export function f(k: K | string | undefined) {
  const a = k instanceof K ? <G {...k} /> : undefined;
  const b = k instanceof K ? <G s={1} {...k} /> : undefined;
  const d = k instanceof K ? <N {...k} /> : undefined;
  const e = k instanceof K ? <div {...k} /> : undefined;
  const z: never = 1;
}
// @filename: late.ts
class K { k = 1 }
declare function id<T>(value: T): T;
declare const k: NoInfer<K | string | undefined> | undefined;
export const v = id(k instanceof K ? { s: 1, ...k } : undefined);
export const w = id(id(k instanceof K ? { ...k } : undefined));
// @filename: noinfer_u__instanceof.ts
class K { k = 1 }
interface O { o: 1 }
declare function id<T>(value: T): T;
declare function two<T, U>(a: T, b: U): [T, U];
declare function bound<T extends object | undefined>(value: T): T;
declare function plain(value: unknown): void;
declare function lazy<T>(make: () => T): T;
declare function given<T>(make: (x: number) => T): T;
declare class Box<T> { constructor(value: T); value: T }
declare function tag<T>(strings: TemplateStringsArray, value: T): T;
declare const c: boolean;
declare function count(value: unknown): number;
declare function over<T>(value: T, more: string): 1;
declare function over<T>(value: T): T;
declare const box: { set<T>(value: T): T };
declare const maybe: undefined | (<T>(value: T) => T);
export function f(k: NoInfer<K | string | undefined> | undefined) {
  const v_id = id(k instanceof K ? { s: 1, ...k } : undefined);
  const p_id: never = v_id;
  const v_id_only = id(k instanceof K ? { ...k } : undefined);
  const p_id_only: never = v_id_only;
  const v_id_and = id(k instanceof K && { s: 1, ...k });
  const p_id_and: never = v_id_and;
  const v_two = two(1, k instanceof K ? { s: 1, ...k } : undefined);
  const p_two: never = v_two;
  const v_bound = bound(k instanceof K ? { s: 1, ...k } : undefined);
  const p_bound: never = v_bound;
  const v_plain = plain(k instanceof K ? { s: 1, ...k } : undefined);
  const p_plain: never = v_plain;
  const v_no_call = (k instanceof K ? { s: 1, ...k } : undefined);
  const p_no_call: never = v_no_call;
  const v_nested = id(k instanceof K ? { a: { s: 1, ...k } } : undefined);
  const p_nested: never = v_nested;
  const v_array = id(k instanceof K ? [{ s: 1, ...k }] : undefined);
  const p_array: never = v_array;
  const v_lazy = lazy(() => (k instanceof K ? { s: 1, ...k } : undefined));
  const p_lazy: never = v_lazy;
  const v_given = given(x => (k instanceof K ? { s: x, ...k } : undefined));
  const p_given: never = v_given;
  const v_nested_call = id(id(k instanceof K ? { s: 1, ...k } : undefined));
  const p_nested_call: never = v_nested_call;
  const v_count_in_id = id(count(k instanceof K ? { s: 1, ...k } : undefined));
  const p_count_in_id: never = v_count_in_id;
  const v_id_in_plain = plain(id(k instanceof K ? { s: 1, ...k } : undefined));
  const p_id_in_plain: never = v_id_in_plain;
  const v_over = over(k instanceof K ? { s: 1, ...k } : undefined);
  const p_over: never = v_over;
  const v_method = box.set(k instanceof K ? { s: 1, ...k } : undefined);
  const p_method: never = v_method;
  const v_optional_call = maybe?.(k instanceof K ? { s: 1, ...k } : undefined);
  const p_optional_call: never = v_optional_call;
  const v_block = lazy(() => { return k instanceof K ? { s: 1, ...k } : undefined; });
  const p_block: never = v_block;
  const v_spread_twice = id(k instanceof K ? { ...k, s: 1, ...k } : undefined);
  const p_spread_twice: never = v_spread_twice;
  const v_three_deep = id(two(1, id(k instanceof K ? { s: 1, ...k } : undefined)));
  const p_three_deep: never = v_three_deep;
  const v_new = new Box(k instanceof K ? { s: 1, ...k } : undefined);
  const p_new: never = v_new;
  const v_tag = tag`${k instanceof K ? { s: 1, ...k } : undefined}`;
  const p_tag: never = v_tag;
  const v_explicit = id<unknown>(k instanceof K ? { s: 1, ...k } : undefined);
  const p_explicit: never = v_explicit;
}
// @filename: noinfer_u__not_string.ts
class K { k = 1 }
interface O { o: 1 }
declare function id<T>(value: T): T;
declare function two<T, U>(a: T, b: U): [T, U];
declare function bound<T extends object | undefined>(value: T): T;
declare function plain(value: unknown): void;
declare function lazy<T>(make: () => T): T;
declare function given<T>(make: (x: number) => T): T;
declare class Box<T> { constructor(value: T); value: T }
declare function tag<T>(strings: TemplateStringsArray, value: T): T;
declare const c: boolean;
declare function count(value: unknown): number;
declare function over<T>(value: T, more: string): 1;
declare function over<T>(value: T): T;
declare const box: { set<T>(value: T): T };
declare const maybe: undefined | (<T>(value: T) => T);
export function f(k: NoInfer<K | string | undefined> | undefined) {
  const v_id = id(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_id: never = v_id;
  const v_id_only = id(typeof k !== "string" ? { ...k } : undefined);
  const p_id_only: never = v_id_only;
  const v_id_and = id(typeof k !== "string" && { s: 1, ...k });
  const p_id_and: never = v_id_and;
  const v_two = two(1, typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_two: never = v_two;
  const v_bound = bound(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_bound: never = v_bound;
  const v_plain = plain(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_plain: never = v_plain;
  const v_no_call = (typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_no_call: never = v_no_call;
  const v_nested = id(typeof k !== "string" ? { a: { s: 1, ...k } } : undefined);
  const p_nested: never = v_nested;
  const v_array = id(typeof k !== "string" ? [{ s: 1, ...k }] : undefined);
  const p_array: never = v_array;
  const v_lazy = lazy(() => (typeof k !== "string" ? { s: 1, ...k } : undefined));
  const p_lazy: never = v_lazy;
  const v_given = given(x => (typeof k !== "string" ? { s: x, ...k } : undefined));
  const p_given: never = v_given;
  const v_nested_call = id(id(typeof k !== "string" ? { s: 1, ...k } : undefined));
  const p_nested_call: never = v_nested_call;
  const v_count_in_id = id(count(typeof k !== "string" ? { s: 1, ...k } : undefined));
  const p_count_in_id: never = v_count_in_id;
  const v_id_in_plain = plain(id(typeof k !== "string" ? { s: 1, ...k } : undefined));
  const p_id_in_plain: never = v_id_in_plain;
  const v_over = over(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_over: never = v_over;
  const v_method = box.set(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_method: never = v_method;
  const v_optional_call = maybe?.(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_optional_call: never = v_optional_call;
  const v_block = lazy(() => { return typeof k !== "string" ? { s: 1, ...k } : undefined; });
  const p_block: never = v_block;
  const v_spread_twice = id(typeof k !== "string" ? { ...k, s: 1, ...k } : undefined);
  const p_spread_twice: never = v_spread_twice;
  const v_three_deep = id(two(1, id(typeof k !== "string" ? { s: 1, ...k } : undefined)));
  const p_three_deep: never = v_three_deep;
  const v_new = new Box(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_new: never = v_new;
  const v_tag = tag`${typeof k !== "string" ? { s: 1, ...k } : undefined}`;
  const p_tag: never = v_tag;
  const v_explicit = id<unknown>(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_explicit: never = v_explicit;
}
// @filename: noinfer_u__truthy.ts
class K { k = 1 }
interface O { o: 1 }
declare function id<T>(value: T): T;
declare function two<T, U>(a: T, b: U): [T, U];
declare function bound<T extends object | undefined>(value: T): T;
declare function plain(value: unknown): void;
declare function lazy<T>(make: () => T): T;
declare function given<T>(make: (x: number) => T): T;
declare class Box<T> { constructor(value: T); value: T }
declare function tag<T>(strings: TemplateStringsArray, value: T): T;
declare const c: boolean;
declare function count(value: unknown): number;
declare function over<T>(value: T, more: string): 1;
declare function over<T>(value: T): T;
declare const box: { set<T>(value: T): T };
declare const maybe: undefined | (<T>(value: T) => T);
export function f(k: NoInfer<K | string | undefined> | undefined) {
  const v_id = id(k ? { s: 1, ...k } : undefined);
  const p_id: never = v_id;
  const v_id_only = id(k ? { ...k } : undefined);
  const p_id_only: never = v_id_only;
  const v_id_and = id(k && { s: 1, ...k });
  const p_id_and: never = v_id_and;
  const v_two = two(1, k ? { s: 1, ...k } : undefined);
  const p_two: never = v_two;
  const v_bound = bound(k ? { s: 1, ...k } : undefined);
  const p_bound: never = v_bound;
  const v_plain = plain(k ? { s: 1, ...k } : undefined);
  const p_plain: never = v_plain;
  const v_no_call = (k ? { s: 1, ...k } : undefined);
  const p_no_call: never = v_no_call;
  const v_nested = id(k ? { a: { s: 1, ...k } } : undefined);
  const p_nested: never = v_nested;
  const v_array = id(k ? [{ s: 1, ...k }] : undefined);
  const p_array: never = v_array;
  const v_lazy = lazy(() => (k ? { s: 1, ...k } : undefined));
  const p_lazy: never = v_lazy;
  const v_given = given(x => (k ? { s: x, ...k } : undefined));
  const p_given: never = v_given;
  const v_nested_call = id(id(k ? { s: 1, ...k } : undefined));
  const p_nested_call: never = v_nested_call;
  const v_count_in_id = id(count(k ? { s: 1, ...k } : undefined));
  const p_count_in_id: never = v_count_in_id;
  const v_id_in_plain = plain(id(k ? { s: 1, ...k } : undefined));
  const p_id_in_plain: never = v_id_in_plain;
  const v_over = over(k ? { s: 1, ...k } : undefined);
  const p_over: never = v_over;
  const v_method = box.set(k ? { s: 1, ...k } : undefined);
  const p_method: never = v_method;
  const v_optional_call = maybe?.(k ? { s: 1, ...k } : undefined);
  const p_optional_call: never = v_optional_call;
  const v_block = lazy(() => { return k ? { s: 1, ...k } : undefined; });
  const p_block: never = v_block;
  const v_spread_twice = id(k ? { ...k, s: 1, ...k } : undefined);
  const p_spread_twice: never = v_spread_twice;
  const v_three_deep = id(two(1, id(k ? { s: 1, ...k } : undefined)));
  const p_three_deep: never = v_three_deep;
  const v_new = new Box(k ? { s: 1, ...k } : undefined);
  const p_new: never = v_new;
  const v_tag = tag`${k ? { s: 1, ...k } : undefined}`;
  const p_tag: never = v_tag;
  const v_explicit = id<unknown>(k ? { s: 1, ...k } : undefined);
  const p_explicit: never = v_explicit;
}
// @filename: param__instanceof.ts
class K { k = 1 }
interface O { o: 1 }
declare function id<T>(value: T): T;
declare function two<T, U>(a: T, b: U): [T, U];
declare function bound<T extends object | undefined>(value: T): T;
declare function plain(value: unknown): void;
declare function lazy<T>(make: () => T): T;
declare function given<T>(make: (x: number) => T): T;
declare class Box<T> { constructor(value: T); value: T }
declare function tag<T>(strings: TemplateStringsArray, value: T): T;
declare const c: boolean;
declare function count(value: unknown): number;
declare function over<T>(value: T, more: string): 1;
declare function over<T>(value: T): T;
declare const box: { set<T>(value: T): T };
declare const maybe: undefined | (<T>(value: T) => T);
export function f<P extends K | string | undefined>(k: P) {
  const v_id = id(k instanceof K ? { s: 1, ...k } : undefined);
  const p_id: never = v_id;
  const v_id_only = id(k instanceof K ? { ...k } : undefined);
  const p_id_only: never = v_id_only;
  const v_id_and = id(k instanceof K && { s: 1, ...k });
  const p_id_and: never = v_id_and;
  const v_two = two(1, k instanceof K ? { s: 1, ...k } : undefined);
  const p_two: never = v_two;
  const v_bound = bound(k instanceof K ? { s: 1, ...k } : undefined);
  const p_bound: never = v_bound;
  const v_plain = plain(k instanceof K ? { s: 1, ...k } : undefined);
  const p_plain: never = v_plain;
  const v_no_call = (k instanceof K ? { s: 1, ...k } : undefined);
  const p_no_call: never = v_no_call;
  const v_nested = id(k instanceof K ? { a: { s: 1, ...k } } : undefined);
  const p_nested: never = v_nested;
  const v_array = id(k instanceof K ? [{ s: 1, ...k }] : undefined);
  const p_array: never = v_array;
  const v_lazy = lazy(() => (k instanceof K ? { s: 1, ...k } : undefined));
  const p_lazy: never = v_lazy;
  const v_given = given(x => (k instanceof K ? { s: x, ...k } : undefined));
  const p_given: never = v_given;
  const v_nested_call = id(id(k instanceof K ? { s: 1, ...k } : undefined));
  const p_nested_call: never = v_nested_call;
  const v_count_in_id = id(count(k instanceof K ? { s: 1, ...k } : undefined));
  const p_count_in_id: never = v_count_in_id;
  const v_id_in_plain = plain(id(k instanceof K ? { s: 1, ...k } : undefined));
  const p_id_in_plain: never = v_id_in_plain;
  const v_over = over(k instanceof K ? { s: 1, ...k } : undefined);
  const p_over: never = v_over;
  const v_method = box.set(k instanceof K ? { s: 1, ...k } : undefined);
  const p_method: never = v_method;
  const v_optional_call = maybe?.(k instanceof K ? { s: 1, ...k } : undefined);
  const p_optional_call: never = v_optional_call;
  const v_block = lazy(() => { return k instanceof K ? { s: 1, ...k } : undefined; });
  const p_block: never = v_block;
  const v_spread_twice = id(k instanceof K ? { ...k, s: 1, ...k } : undefined);
  const p_spread_twice: never = v_spread_twice;
  const v_three_deep = id(two(1, id(k instanceof K ? { s: 1, ...k } : undefined)));
  const p_three_deep: never = v_three_deep;
  const v_new = new Box(k instanceof K ? { s: 1, ...k } : undefined);
  const p_new: never = v_new;
  const v_tag = tag`${k instanceof K ? { s: 1, ...k } : undefined}`;
  const p_tag: never = v_tag;
  const v_explicit = id<unknown>(k instanceof K ? { s: 1, ...k } : undefined);
  const p_explicit: never = v_explicit;
}
// @filename: param__not_string.ts
class K { k = 1 }
interface O { o: 1 }
declare function id<T>(value: T): T;
declare function two<T, U>(a: T, b: U): [T, U];
declare function bound<T extends object | undefined>(value: T): T;
declare function plain(value: unknown): void;
declare function lazy<T>(make: () => T): T;
declare function given<T>(make: (x: number) => T): T;
declare class Box<T> { constructor(value: T); value: T }
declare function tag<T>(strings: TemplateStringsArray, value: T): T;
declare const c: boolean;
declare function count(value: unknown): number;
declare function over<T>(value: T, more: string): 1;
declare function over<T>(value: T): T;
declare const box: { set<T>(value: T): T };
declare const maybe: undefined | (<T>(value: T) => T);
export function f<P extends K | string | undefined>(k: P) {
  const v_id = id(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_id: never = v_id;
  const v_id_only = id(typeof k !== "string" ? { ...k } : undefined);
  const p_id_only: never = v_id_only;
  const v_id_and = id(typeof k !== "string" && { s: 1, ...k });
  const p_id_and: never = v_id_and;
  const v_two = two(1, typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_two: never = v_two;
  const v_bound = bound(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_bound: never = v_bound;
  const v_plain = plain(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_plain: never = v_plain;
  const v_no_call = (typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_no_call: never = v_no_call;
  const v_nested = id(typeof k !== "string" ? { a: { s: 1, ...k } } : undefined);
  const p_nested: never = v_nested;
  const v_array = id(typeof k !== "string" ? [{ s: 1, ...k }] : undefined);
  const p_array: never = v_array;
  const v_lazy = lazy(() => (typeof k !== "string" ? { s: 1, ...k } : undefined));
  const p_lazy: never = v_lazy;
  const v_given = given(x => (typeof k !== "string" ? { s: x, ...k } : undefined));
  const p_given: never = v_given;
  const v_nested_call = id(id(typeof k !== "string" ? { s: 1, ...k } : undefined));
  const p_nested_call: never = v_nested_call;
  const v_count_in_id = id(count(typeof k !== "string" ? { s: 1, ...k } : undefined));
  const p_count_in_id: never = v_count_in_id;
  const v_id_in_plain = plain(id(typeof k !== "string" ? { s: 1, ...k } : undefined));
  const p_id_in_plain: never = v_id_in_plain;
  const v_over = over(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_over: never = v_over;
  const v_method = box.set(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_method: never = v_method;
  const v_optional_call = maybe?.(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_optional_call: never = v_optional_call;
  const v_block = lazy(() => { return typeof k !== "string" ? { s: 1, ...k } : undefined; });
  const p_block: never = v_block;
  const v_spread_twice = id(typeof k !== "string" ? { ...k, s: 1, ...k } : undefined);
  const p_spread_twice: never = v_spread_twice;
  const v_three_deep = id(two(1, id(typeof k !== "string" ? { s: 1, ...k } : undefined)));
  const p_three_deep: never = v_three_deep;
  const v_new = new Box(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_new: never = v_new;
  const v_tag = tag`${typeof k !== "string" ? { s: 1, ...k } : undefined}`;
  const p_tag: never = v_tag;
  const v_explicit = id<unknown>(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_explicit: never = v_explicit;
}
// @filename: param__truthy.ts
class K { k = 1 }
interface O { o: 1 }
declare function id<T>(value: T): T;
declare function two<T, U>(a: T, b: U): [T, U];
declare function bound<T extends object | undefined>(value: T): T;
declare function plain(value: unknown): void;
declare function lazy<T>(make: () => T): T;
declare function given<T>(make: (x: number) => T): T;
declare class Box<T> { constructor(value: T); value: T }
declare function tag<T>(strings: TemplateStringsArray, value: T): T;
declare const c: boolean;
declare function count(value: unknown): number;
declare function over<T>(value: T, more: string): 1;
declare function over<T>(value: T): T;
declare const box: { set<T>(value: T): T };
declare const maybe: undefined | (<T>(value: T) => T);
export function f<P extends K | string | undefined>(k: P) {
  const v_id = id(k ? { s: 1, ...k } : undefined);
  const p_id: never = v_id;
  const v_id_only = id(k ? { ...k } : undefined);
  const p_id_only: never = v_id_only;
  const v_id_and = id(k && { s: 1, ...k });
  const p_id_and: never = v_id_and;
  const v_two = two(1, k ? { s: 1, ...k } : undefined);
  const p_two: never = v_two;
  const v_bound = bound(k ? { s: 1, ...k } : undefined);
  const p_bound: never = v_bound;
  const v_plain = plain(k ? { s: 1, ...k } : undefined);
  const p_plain: never = v_plain;
  const v_no_call = (k ? { s: 1, ...k } : undefined);
  const p_no_call: never = v_no_call;
  const v_nested = id(k ? { a: { s: 1, ...k } } : undefined);
  const p_nested: never = v_nested;
  const v_array = id(k ? [{ s: 1, ...k }] : undefined);
  const p_array: never = v_array;
  const v_lazy = lazy(() => (k ? { s: 1, ...k } : undefined));
  const p_lazy: never = v_lazy;
  const v_given = given(x => (k ? { s: x, ...k } : undefined));
  const p_given: never = v_given;
  const v_nested_call = id(id(k ? { s: 1, ...k } : undefined));
  const p_nested_call: never = v_nested_call;
  const v_count_in_id = id(count(k ? { s: 1, ...k } : undefined));
  const p_count_in_id: never = v_count_in_id;
  const v_id_in_plain = plain(id(k ? { s: 1, ...k } : undefined));
  const p_id_in_plain: never = v_id_in_plain;
  const v_over = over(k ? { s: 1, ...k } : undefined);
  const p_over: never = v_over;
  const v_method = box.set(k ? { s: 1, ...k } : undefined);
  const p_method: never = v_method;
  const v_optional_call = maybe?.(k ? { s: 1, ...k } : undefined);
  const p_optional_call: never = v_optional_call;
  const v_block = lazy(() => { return k ? { s: 1, ...k } : undefined; });
  const p_block: never = v_block;
  const v_spread_twice = id(k ? { ...k, s: 1, ...k } : undefined);
  const p_spread_twice: never = v_spread_twice;
  const v_three_deep = id(two(1, id(k ? { s: 1, ...k } : undefined)));
  const p_three_deep: never = v_three_deep;
  const v_new = new Box(k ? { s: 1, ...k } : undefined);
  const p_new: never = v_new;
  const v_tag = tag`${k ? { s: 1, ...k } : undefined}`;
  const p_tag: never = v_tag;
  const v_explicit = id<unknown>(k ? { s: 1, ...k } : undefined);
  const p_explicit: never = v_explicit;
}
// @filename: plain__instanceof.ts
class K { k = 1 }
interface O { o: 1 }
declare function id<T>(value: T): T;
declare function two<T, U>(a: T, b: U): [T, U];
declare function bound<T extends object | undefined>(value: T): T;
declare function plain(value: unknown): void;
declare function lazy<T>(make: () => T): T;
declare function given<T>(make: (x: number) => T): T;
declare class Box<T> { constructor(value: T); value: T }
declare function tag<T>(strings: TemplateStringsArray, value: T): T;
declare const c: boolean;
declare function count(value: unknown): number;
declare function over<T>(value: T, more: string): 1;
declare function over<T>(value: T): T;
declare const box: { set<T>(value: T): T };
declare const maybe: undefined | (<T>(value: T) => T);
export function f(k: K | string | undefined) {
  const v_id = id(k instanceof K ? { s: 1, ...k } : undefined);
  const p_id: never = v_id;
  const v_id_only = id(k instanceof K ? { ...k } : undefined);
  const p_id_only: never = v_id_only;
  const v_id_and = id(k instanceof K && { s: 1, ...k });
  const p_id_and: never = v_id_and;
  const v_two = two(1, k instanceof K ? { s: 1, ...k } : undefined);
  const p_two: never = v_two;
  const v_bound = bound(k instanceof K ? { s: 1, ...k } : undefined);
  const p_bound: never = v_bound;
  const v_plain = plain(k instanceof K ? { s: 1, ...k } : undefined);
  const p_plain: never = v_plain;
  const v_no_call = (k instanceof K ? { s: 1, ...k } : undefined);
  const p_no_call: never = v_no_call;
  const v_nested = id(k instanceof K ? { a: { s: 1, ...k } } : undefined);
  const p_nested: never = v_nested;
  const v_array = id(k instanceof K ? [{ s: 1, ...k }] : undefined);
  const p_array: never = v_array;
  const v_lazy = lazy(() => (k instanceof K ? { s: 1, ...k } : undefined));
  const p_lazy: never = v_lazy;
  const v_given = given(x => (k instanceof K ? { s: x, ...k } : undefined));
  const p_given: never = v_given;
  const v_nested_call = id(id(k instanceof K ? { s: 1, ...k } : undefined));
  const p_nested_call: never = v_nested_call;
  const v_count_in_id = id(count(k instanceof K ? { s: 1, ...k } : undefined));
  const p_count_in_id: never = v_count_in_id;
  const v_id_in_plain = plain(id(k instanceof K ? { s: 1, ...k } : undefined));
  const p_id_in_plain: never = v_id_in_plain;
  const v_over = over(k instanceof K ? { s: 1, ...k } : undefined);
  const p_over: never = v_over;
  const v_method = box.set(k instanceof K ? { s: 1, ...k } : undefined);
  const p_method: never = v_method;
  const v_optional_call = maybe?.(k instanceof K ? { s: 1, ...k } : undefined);
  const p_optional_call: never = v_optional_call;
  const v_block = lazy(() => { return k instanceof K ? { s: 1, ...k } : undefined; });
  const p_block: never = v_block;
  const v_spread_twice = id(k instanceof K ? { ...k, s: 1, ...k } : undefined);
  const p_spread_twice: never = v_spread_twice;
  const v_three_deep = id(two(1, id(k instanceof K ? { s: 1, ...k } : undefined)));
  const p_three_deep: never = v_three_deep;
  const v_new = new Box(k instanceof K ? { s: 1, ...k } : undefined);
  const p_new: never = v_new;
  const v_tag = tag`${k instanceof K ? { s: 1, ...k } : undefined}`;
  const p_tag: never = v_tag;
  const v_explicit = id<unknown>(k instanceof K ? { s: 1, ...k } : undefined);
  const p_explicit: never = v_explicit;
}
// @filename: plain__not_string.ts
class K { k = 1 }
interface O { o: 1 }
declare function id<T>(value: T): T;
declare function two<T, U>(a: T, b: U): [T, U];
declare function bound<T extends object | undefined>(value: T): T;
declare function plain(value: unknown): void;
declare function lazy<T>(make: () => T): T;
declare function given<T>(make: (x: number) => T): T;
declare class Box<T> { constructor(value: T); value: T }
declare function tag<T>(strings: TemplateStringsArray, value: T): T;
declare const c: boolean;
declare function count(value: unknown): number;
declare function over<T>(value: T, more: string): 1;
declare function over<T>(value: T): T;
declare const box: { set<T>(value: T): T };
declare const maybe: undefined | (<T>(value: T) => T);
export function f(k: K | string | undefined) {
  const v_id = id(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_id: never = v_id;
  const v_id_only = id(typeof k !== "string" ? { ...k } : undefined);
  const p_id_only: never = v_id_only;
  const v_id_and = id(typeof k !== "string" && { s: 1, ...k });
  const p_id_and: never = v_id_and;
  const v_two = two(1, typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_two: never = v_two;
  const v_bound = bound(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_bound: never = v_bound;
  const v_plain = plain(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_plain: never = v_plain;
  const v_no_call = (typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_no_call: never = v_no_call;
  const v_nested = id(typeof k !== "string" ? { a: { s: 1, ...k } } : undefined);
  const p_nested: never = v_nested;
  const v_array = id(typeof k !== "string" ? [{ s: 1, ...k }] : undefined);
  const p_array: never = v_array;
  const v_lazy = lazy(() => (typeof k !== "string" ? { s: 1, ...k } : undefined));
  const p_lazy: never = v_lazy;
  const v_given = given(x => (typeof k !== "string" ? { s: x, ...k } : undefined));
  const p_given: never = v_given;
  const v_nested_call = id(id(typeof k !== "string" ? { s: 1, ...k } : undefined));
  const p_nested_call: never = v_nested_call;
  const v_count_in_id = id(count(typeof k !== "string" ? { s: 1, ...k } : undefined));
  const p_count_in_id: never = v_count_in_id;
  const v_id_in_plain = plain(id(typeof k !== "string" ? { s: 1, ...k } : undefined));
  const p_id_in_plain: never = v_id_in_plain;
  const v_over = over(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_over: never = v_over;
  const v_method = box.set(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_method: never = v_method;
  const v_optional_call = maybe?.(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_optional_call: never = v_optional_call;
  const v_block = lazy(() => { return typeof k !== "string" ? { s: 1, ...k } : undefined; });
  const p_block: never = v_block;
  const v_spread_twice = id(typeof k !== "string" ? { ...k, s: 1, ...k } : undefined);
  const p_spread_twice: never = v_spread_twice;
  const v_three_deep = id(two(1, id(typeof k !== "string" ? { s: 1, ...k } : undefined)));
  const p_three_deep: never = v_three_deep;
  const v_new = new Box(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_new: never = v_new;
  const v_tag = tag`${typeof k !== "string" ? { s: 1, ...k } : undefined}`;
  const p_tag: never = v_tag;
  const v_explicit = id<unknown>(typeof k !== "string" ? { s: 1, ...k } : undefined);
  const p_explicit: never = v_explicit;
}
// @filename: plain__truthy.ts
class K { k = 1 }
interface O { o: 1 }
declare function id<T>(value: T): T;
declare function two<T, U>(a: T, b: U): [T, U];
declare function bound<T extends object | undefined>(value: T): T;
declare function plain(value: unknown): void;
declare function lazy<T>(make: () => T): T;
declare function given<T>(make: (x: number) => T): T;
declare class Box<T> { constructor(value: T); value: T }
declare function tag<T>(strings: TemplateStringsArray, value: T): T;
declare const c: boolean;
declare function count(value: unknown): number;
declare function over<T>(value: T, more: string): 1;
declare function over<T>(value: T): T;
declare const box: { set<T>(value: T): T };
declare const maybe: undefined | (<T>(value: T) => T);
export function f(k: K | string | undefined) {
  const v_id = id(k ? { s: 1, ...k } : undefined);
  const p_id: never = v_id;
  const v_id_only = id(k ? { ...k } : undefined);
  const p_id_only: never = v_id_only;
  const v_id_and = id(k && { s: 1, ...k });
  const p_id_and: never = v_id_and;
  const v_two = two(1, k ? { s: 1, ...k } : undefined);
  const p_two: never = v_two;
  const v_bound = bound(k ? { s: 1, ...k } : undefined);
  const p_bound: never = v_bound;
  const v_plain = plain(k ? { s: 1, ...k } : undefined);
  const p_plain: never = v_plain;
  const v_no_call = (k ? { s: 1, ...k } : undefined);
  const p_no_call: never = v_no_call;
  const v_nested = id(k ? { a: { s: 1, ...k } } : undefined);
  const p_nested: never = v_nested;
  const v_array = id(k ? [{ s: 1, ...k }] : undefined);
  const p_array: never = v_array;
  const v_lazy = lazy(() => (k ? { s: 1, ...k } : undefined));
  const p_lazy: never = v_lazy;
  const v_given = given(x => (k ? { s: x, ...k } : undefined));
  const p_given: never = v_given;
  const v_nested_call = id(id(k ? { s: 1, ...k } : undefined));
  const p_nested_call: never = v_nested_call;
  const v_count_in_id = id(count(k ? { s: 1, ...k } : undefined));
  const p_count_in_id: never = v_count_in_id;
  const v_id_in_plain = plain(id(k ? { s: 1, ...k } : undefined));
  const p_id_in_plain: never = v_id_in_plain;
  const v_over = over(k ? { s: 1, ...k } : undefined);
  const p_over: never = v_over;
  const v_method = box.set(k ? { s: 1, ...k } : undefined);
  const p_method: never = v_method;
  const v_optional_call = maybe?.(k ? { s: 1, ...k } : undefined);
  const p_optional_call: never = v_optional_call;
  const v_block = lazy(() => { return k ? { s: 1, ...k } : undefined; });
  const p_block: never = v_block;
  const v_spread_twice = id(k ? { ...k, s: 1, ...k } : undefined);
  const p_spread_twice: never = v_spread_twice;
  const v_three_deep = id(two(1, id(k ? { s: 1, ...k } : undefined)));
  const p_three_deep: never = v_three_deep;
  const v_new = new Box(k ? { s: 1, ...k } : undefined);
  const p_new: never = v_new;
  const v_tag = tag`${k ? { s: 1, ...k } : undefined}`;
  const p_tag: never = v_tag;
  const v_explicit = id<unknown>(k ? { s: 1, ...k } : undefined);
  const p_explicit: never = v_explicit;
}
// @filename: sentinel.ts
export const sentinel: number = "";
