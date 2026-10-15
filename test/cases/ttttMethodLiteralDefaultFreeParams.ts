// @strict: true
// A method whose unannotated parameter has a literal default (`strict = true`) has a signature that mentions only its
// annotations: reading a property of the class through `T[keyof T]` in a conditional type doesn't resolve its return
// type, which here needs the keys of the mapped types being resolved (mikro-orm's EntityDTO with a `toJSON(strict =
// true): Omit<EntityDTO<this>, ...>` entity: the second mapped type's keys stayed deferred)
type IsOpt<T, K extends keyof T> = T[K] extends { brand: true } ? false : K extends OptKeys<T> ? true : false;
type OptKeys<T> = { [K in keyof T]-?: undefined extends T[K] ? K : never }[keyof T];
type Req<T, K extends keyof T> = IsOpt<T, K> extends false ? K : never;
type Opt<T, K extends keyof T> = IsOpt<T, K> extends false ? never : K;
type DTO<T> = { [K in keyof T as Req<T, K>]: T[K] } & { [K in keyof T as Opt<T, K>]?: T[K] };
class B {
    title!: string;
    perex?: string;
    toJSON(strict = true): Omit<DTO<this>, "x"> { return null!; }
}
type E3<X> = { [K in keyof X as Req<X, K>]: 1 };
type E4<X> = { [K in keyof X as Opt<X, K>]: 1 };
const x2: keyof E3<B> = 5;
const x3: keyof E4<B> = 5;
declare const dto: DTO<B>;
const p: number = dto.perex;
