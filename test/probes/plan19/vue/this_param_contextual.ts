// @strict: true
declare function dc<T>(o: { created?(): void; data: T } & ThisType<T & { $emit(e: string): void }>): T
export const a = dc({
  data: { x: 1 },
  created(this) {
    this.$emit('one')
  },
})
declare function f(cb: (this: string) => void): void
f(function (this) { this.length })
