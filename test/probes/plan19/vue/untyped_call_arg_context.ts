// @strict: true
declare const V: any
export const y = V.define({ render() { return this.$slots } })
export const z = V({ render() { return this.$slots } }, x => x)
