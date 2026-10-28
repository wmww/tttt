// After a parameter's modifiers a reserved word is the name tsc's parseBindingIdentifier rejects: TS1359 (mutation
// testing seed 20261101, contextualParamTypeVsNestedReturnTypeInference4: `declare function` where a parameter was expected)
declare function a(x: number,
declare function b(): void;
declare function c(public function): void;
type F = (function) => void;
function d(class) {}
