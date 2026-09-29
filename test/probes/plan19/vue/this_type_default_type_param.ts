// @strict: true
type Computed<C> = { [K in keyof C]: C[K] extends () => infer R ? R : never }
declare function define<B, C extends Record<string, () => any> = {}, M extends Record<string, Function> = {}>(
  o: { setup?: () => B; computed?: C; methods?: M; render?(): void } & ThisType<B & Computed<C> & M>
): void
define({
  setup() { return { b: 123 } },
  computed: {
    d() { return this.b + 1 },
  },
  methods: {
    m() { return this.b },
  },
  render() {
    const x: number = this.d
    this.m
  },
})
declare function define2<B, C extends Record<string, () => any> = {}>(
  o: { setup?: () => B; computed?: C; render?(): void } & ThisType<B & C>
): C
const r2 = define2({
  setup() { return { b: 123 } },
  computed: {
    d() { return this.b + 1 },
  },
})
