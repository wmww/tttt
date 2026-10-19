// `this.constructor === C` keeps the `this` type of C (tsc compares symbols, the `this` type has its class's): atproto's BskyAgent.clone narrowed `this` to never

class Base {
  m = 1
  copyInto<T extends Base>(t: T): T { return t }
}
class Agent extends Base {
  clone(): this {
    if (this.constructor === Agent) {
      const agent = new Agent()
      agent.m = this.m
      return this.copyInto(agent as this)
    }
    if (this.constructor === Base) {
      this.m
    }
    throw new TypeError()
  }
}
class G<T> {
  x!: T
  f() {
    if (this.constructor === G) return this.x
  }
}
interface I { m: number }
function h(this: I) {
  if (this.constructor === Agent) this.m
}
