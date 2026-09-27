// @strict: true
type P1 = ((s: string) => s is "a") extends (arg: any) => arg is infer R ? R : 0;
declare class Z<O> {
  o: O;
  refine<Ch extends (arg: O) => unknown>(check: Ch): Ch extends (arg: any) => arg is infer R ? this & Z<R> : this;
}
declare const z: Z<string>;
const s = z.refine((s): s is "a" => s === "a");
const o: 1 = s.o;
