interface Parser {
  (input: unknown): unknown;
  readonly schema: string;
  readonly config: number | undefined;
}
function parser(schema: string, config?: number): Parser {
  const func: Parser = (input: unknown) => input;
  // @ts-expect-error
  func.schema = schema;
  // @ts-expect-error
  func.config = config;
  return func;
}
interface P2 { (x: number): number; tag: string }
const g: P2 = function (x: number) { return x; };
g.tag = "a";
const h: P2 = (x: number) => x;
const k: P2 = (x) => x;
k.tag = "b";
k.other = 1;
const m: P2 | undefined = (x: number) => x;
m.tag = "c";
