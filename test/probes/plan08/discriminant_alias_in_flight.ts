// @strict: true
interface Node { kind: number; parent: Node }
interface Expression extends Node { _e: any }
interface LeftHandSideExpression extends Expression { _l: any }
interface Identifier extends LeftHandSideExpression { kind: 80; text: string }
interface PropertyAccessExpression extends LeftHandSideExpression { kind: 211; expression: LeftHandSideExpression; name: Identifier }
interface ElementAccessExpression extends LeftHandSideExpression { kind: 212; expression: LeftHandSideExpression; argumentExpression: Expression }
type EntityNameExpression = Identifier | PropertyAccessEntityNameExpression;
interface PropertyAccessEntityNameExpression extends PropertyAccessExpression { readonly expression: EntityNameExpression }
type BindableStaticNameExpression = EntityNameExpression | BindableStaticElementAccessExpression;
type LiteralLikeElementAccessExpression = ElementAccessExpression & { readonly argumentExpression: Identifier };
type BindableStaticElementAccessExpression = LiteralLikeElementAccessExpression & { readonly expression: BindableStaticNameExpression };
type BindableStaticAccessExpression = PropertyAccessEntityNameExpression | BindableStaticElementAccessExpression;
export function f(lhs: BindableStaticAccessExpression) {
    const classPrototype = lhs.expression as BindableStaticAccessExpression;
    const constructorFunction = classPrototype.expression;
    return constructorFunction;
}
