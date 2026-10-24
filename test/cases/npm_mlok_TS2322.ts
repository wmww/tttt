// origin: npm mlok@0.1.5 https://github.com/smolijar/mlok tsconfig.json seed 20261042
// flip: no
// @strict: true
// @target: es2016
type AllowedOverride = Record<any, any>
declare const mlokNode: <T>(o: AllowedOverride) => MlokRoot<T>
type Mlok<
  MlokedInterface,
  Override extends AllowedOverride = {},
  MlokResultType = MlokedInterface extends (...args: infer A) => infer R
    ? (...args: A) => Mlok<Exclude<R, undefined>, Override>
    : {}
> = Override extends Function | object
  ? Override & MlokResultType & MlokedInterface
  : Override
type MlokRoot<MlokedInterface, ResultType = Mlok<MlokedInterface>> = ResultType & {
  override: <O extends AllowedOverride>(overridesMap: O) => Mlok<MlokedInterface, O>
}
interface Req { getHeaders(): { [k: string]: string | undefined } }
const reqMock = mlokNode<Req>({}).override({
  getHeaders: () => 'foo',
} as const)
const foo: 'foo' = reqMock.getHeaders()
