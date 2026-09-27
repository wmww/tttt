type OptionOne = { kind: "one"; s: string; };
type OptionTwo = { kind: "two"; x: number; y: number; };
type Options = OptionOne | OptionTwo;
type OptionHandlers = {
  [K in Options['kind']]: (option: Options & { kind: K }) => string;
}
declare const optionHandlers: OptionHandlers;
function handleOption<K extends Options['kind']>(option: Options & { kind: K }, kind: K): string {
  const handler = optionHandlers[kind];
  return handler(option);
};
