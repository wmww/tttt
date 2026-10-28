// @strict: true
// origin: npm SII-Holos/synergy @ a283cf3 apps/web, ProviderConnectionFlow.tsx:503 (`setFormStore.bind(null, "value")` with
// solid-js 1.9's SetStoreFunction): `never[never]` is never (tsc's getPropertyTypeForIndexType: never is a key of every
// kind), not the error type, so the base signature (tsc's getBaseSignature) of a chain of key parameters keeps never
// flip: yes
type W<T> = Exclude<T, string | number>;
type KeyOf<T> = [T] extends [never] ? never : keyof T;
type Form = { value: string; other: number }
type A = never[never];
type D = W<Form["value"]>[KeyOf<W<Form["value"]>>];
export const a: [A, D] = 1 as any as number;
interface S3<T> {
  <K1 extends KeyOf<W<T>>, K2 extends KeyOf<W<W<T>[K1]>>, K3 extends KeyOf<W<W<W<T>[K1]>[K2]>>>(k1: K1, k2: K2, k3: K3, ...rest: KeyOf<W<W<W<T>[K1]>[K2]>[K3]>[]): void;
}
declare const s3: S3<Form>;
export const g: number = s3.bind(null, "value");
