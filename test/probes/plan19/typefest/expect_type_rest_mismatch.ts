type SE<L, R> = (<T>() => T extends (L & T) | T ? true : false) extends <T>() => T extends (R & T) | T ? true : false ? true : false;
type MA<A extends boolean> = A extends true ? [] : [1];
interface P<Actual> {
  eq: <Expected>(...m: MA<SE<Actual, Expected>>) => true;
}
declare function e<A>(): P<A>;
type G = { (argument: number): string; key: string };
e<((argument: number) => string) & {key: string}>().eq<G>();
e<G>().eq<G>();
type X = SE<((argument: number) => string) & {key: string}, G>;
const x: X = true;
