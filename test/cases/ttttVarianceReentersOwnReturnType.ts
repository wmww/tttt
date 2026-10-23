// @strict: true
// @noEmit: true
// Known failing (agents/bugs/sourcesweep_checker.md "pothos's TS7023"): tsgo checks the body first, and the call's
// inference from `this` to CacheNode<T> measures CacheNode's variance, which compares managerForField's type: its return
// type is asked for below the variance barrier, re-enters the same call nested and, a level further, finds the
// cycle: TS7023 and `() => any`. tttt gets FSM<Types> (pothos plugin-smart-subscriptions' cache-node.ts:41)
class CacheNode<Types> {
  fieldManager: FSM<Types> | null = null;
  managerForField() {
    this.fieldManager = new FSM(this);
    return this.fieldManager;
  }
}
class FSM<Types> {
  cacheNode!: CacheNode<Types>;
  constructor(cacheNode: CacheNode<Types>) {}
}
