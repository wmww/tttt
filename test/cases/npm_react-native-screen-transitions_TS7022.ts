// origin: npm react-native-screen-transitions@4.0.0 https://github.com/eds2002/react-native-screen-transitions packages/react-native-screen-transitions/tsconfig.json seed 20261217
// flip: yes
// @strict: true
// @noEmit: true
// An annotated destructuring, `export const { useOptional, ... }: ReturnType<typeof createAnim> = createAnim(cb)`,
// whose callback calls one of the destructured bindings. The annotation gives the bindings their types, so tsgo
// sees no circularity. tttt reports TS7022/TS7024 (15 errors on the project, each provider of that shape).
declare function createProvider(name: string): <P, V>(fn: (props: P) => V) => { Provider: (p: P) => null; useOptional: () => V | null; use: () => V };
type Ctx = { n: number };
const createAnim = createProvider("A")<{}, Ctx>;
export const {
  Provider,
  useOptional,
  use,
}: ReturnType<typeof createAnim> = createAnim((_props) => {
  const parent = useOptional();
  return { n: parent ? parent.n + 1 : 0 };
});
