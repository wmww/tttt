// @strict: true
type BooleanKey<T, K extends keyof T = keyof T> = K extends any
  ? T[K] extends boolean | undefined
    ? T[K] extends never | undefined
      ? never
      : K
    : never
  : never
type DefineProps<T, BKeys extends keyof T> = Readonly<T> & {
  readonly [K in BKeys]-?: boolean
}
type LooseRequired<T> = { [P in keyof (T & Required<T>)]: T[P] }
declare function defineProps<PropNames extends string = string>(props: PropNames[]): Readonly<{ [key in PropNames]?: any }>
declare function defineProps<TypeProps>(): DefineProps<LooseRequired<TypeProps>, BooleanKey<TypeProps>>
declare function expectType<T>(value: T): void

const props0 = defineProps<{ bool?: boolean }>()
function test<T extends boolean>() {
  const props = defineProps<{ foo: T; bar: string; x?: boolean }>()
  expectType<boolean>(props.x)
}
