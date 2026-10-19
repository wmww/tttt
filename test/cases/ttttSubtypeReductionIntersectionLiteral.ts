// @strict: true
// An intersection with a property an object literal lacks isn't its strict subtype (tsc's propertiesRelatedTo checks an
// object literal target's missing properties in the intersection source's optional-properties pass too), so a
// conditional's subtype reduction keeps the intersection and drops the literal (hono's jwt.ts destructuring)
type Alg = "A" | "B";
type Options = { alg: Alg } & { iss?: string; aud?: string[] };
type Strict = { alg: string } & { iss: string };

function verify(algOrOptions: Alg | Options, b: boolean, q: Strict) {
    const { alg, iss, aud } = typeof algOrOptions === "string" ? { alg: algOrOptions } : algOrOptions;
    const r = b ? { alg: "" } : q;
    const s = b ? q : { alg: "" };
    return [alg, iss, aud, r, s];
}
