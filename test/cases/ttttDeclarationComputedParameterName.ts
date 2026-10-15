// @declaration: true
// A computed property name naming a parameter, in an inferred return type: the parameter is as visible as its
// function (no TS4060), and the name is written as it is
export function insensitiveEq(field: string, value: string) {
    return {
        [field]: {
            $regex: value,
            $options: "i" as const,
        },
    };
}
export const arrow = (key: string) => ({ [key]: 1 });
