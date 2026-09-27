// @strict: true
// @exactOptionalPropertyTypes: true
interface S<Out> { _zod: { output: Out } }
type DV<Options extends readonly S<unknown>[], Disc extends string> = {
  [I in keyof Options]: Options[I] extends { _zod: { output: infer Out } } ? Disc extends keyof Out ? Out[Disc] : never : never;
}[number];
interface U<Options extends readonly S<unknown>[], Disc extends string> { options: Options; disc: Disc }
declare function du<Types extends readonly [S<unknown>, ...S<unknown>[]], Disc extends string>(d: Disc, o: Types): U<Types, Disc>;
declare function get<Options extends readonly S<unknown>[], Disc extends string, const V extends DV<Options, Disc>>(u: U<Options, Disc>, v: V): V;
declare const opt: S<{ type?: "a"; a: string }>;
declare const req: S<{ type: "b"; b: string }>;
const r1 = get(du("type", [opt, req]), undefined);
const u = du("type", [opt, req]);
const r2 = get(u, undefined);
