// @strict: true
// @noEmit: true
// `this.x = v` with an optional property x gives v the target's type as its contextual type, undefined included
// (tsgo's getContextualTypeForAssignmentExpression ends in getTypeOfExpression(left)): adonis' `this.stack = await
// this.prompt.choice(...)` infers Result from it, so the narrowed `this.stack` still holds undefined afterwards
declare function choice<Choice extends string, Result extends any = Choice>(title: string, choices: readonly Choice[]): Promise<Result>;
declare function $<T extends HTMLElement>(d: string): T;
class C {
  declare stack?: 'server' | 'named' | 'router'
  t?: HTMLDivElement;
  async run() {
    const stackChoices = ['server', 'router', 'named']
    if (!this.stack) {
      this.stack = await choice('x', stackChoices)
    }
    if (!stackChoices.includes(this.stack)) {
      return
    }
    this.t = $('.x');
  }
}
