//// [test/cases/ttttDeclarationComputedParameterName.ts] ////

//// [ttttDeclarationComputedParameterName.ts]
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


//// [ttttDeclarationComputedParameterName.js]
// A computed property name naming a parameter, in an inferred return type: the parameter is as visible as its
// function (no TS4060), and the name is written as it is
export function insensitiveEq(field, value) {
    return {
        [field]: {
            $regex: value,
            $options: "i",
        },
    };
}
export const arrow = (key) => ({ [key]: 1 });


//// [ttttDeclarationComputedParameterName.d.ts]
export declare function insensitiveEq(field: string, value: string): {
    [field]: {
        $regex: string;
        $options: "i";
    };
};
export declare const arrow: (key: string) => {
    [x: string]: number;
};
