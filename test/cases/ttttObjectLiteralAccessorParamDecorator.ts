// @experimentalDecorators: true
// A parameter decorator of an object literal's setter inside a class member's body can't decorate anything (TS1206)
// and isn't checked (no TS2304 for its name, tsc's checkDecorators doesn't look at it); a class's own setter's is.
// Seed 20261117, decoratorMetadataObjectLiteralMethodNoCrash delete-stmt
class C {
    method() {
        return {
            m(@dec x: string) {},
            set s(@dec v: number) {}
        };
    }
    set t(@dec v: number) {}
}
