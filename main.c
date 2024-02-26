#include <stdio.h>
#include "stc/cstr.h"
#include "stc/types.h"

__attribute__((noreturn)) void panic(const char* file, int line, const char* fmt, ...) __attribute__ ((format (printf, 3, 4)));
void panic(const char* file, int line, const char* fmt, ...) {
    fprintf(stderr, "%s:%d panic: ", file, line);
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fprintf(stderr, "\n");
    exit(1);
}
#define PANIC(...) panic(__FILE__, __LINE__, __VA_ARGS__)
#define UNREACHABLE PANIC("unreachable was reached \\x_x/")

typedef struct Type Type;
typedef struct Member Member;
static bool extends(Type *sub, Type *super);

forward_vec(vec_Member, Member);

typedef struct Interface {
    vec_Member members;
} Interface;
static void Interface_drop(Interface* self);

typedef struct TernaryType TernaryType;

typedef struct Type {
    enum {
        TYPE_ANY,
        TYPE_UNDEFINED,
        TYPE_NULL,
        TYPE_BOOLEAN,
        TYPE_NUMBER,
        TYPE_STRING,
        TYPE_INTERFACE,
        TYPE_TERNARY,
    } tag;
    union {
        Interface interface;
        TernaryType* ternary;
    } data;
} Type;
static void Type_drop(Type* self);
#define TYPE_NEW_DECL(name, type) static Type type_##name##_new() { return (Type){ .tag = TYPE_##type }; }
TYPE_NEW_DECL(any, NULL)
TYPE_NEW_DECL(undefined, UNDEFINED)
TYPE_NEW_DECL(null, NULL)
TYPE_NEW_DECL(boolean, BOOLEAN)
TYPE_NEW_DECL(number, NUMBER)
TYPE_NEW_DECL(string, STRING)
#undef TYPE_NEW_DECL
static Type type_interface_new(Interface interface) {
    return (Type){
        .tag = TYPE_INTERFACE,
        .data.interface = interface,
    };
}
static Type type_ternary_new(TernaryType* ternary) {
    return (Type){
        .tag = TYPE_TERNARY,
        .data.ternary = ternary,
    };
}
static cstr Type_to_cstr(Type* self);

typedef struct ExtendsConditional {
    Type maybeSub;
    Type maybeSuper;
} ExtendsConditional;
static void ExtendsConditional_drop(ExtendsConditional* self);

typedef struct TernaryType {
    ExtendsConditional conditional;
    Type trueType;
    Type falseType;
} TernaryType;
static void TernaryType_drop(TernaryType* self);

typedef struct Member {
    cstr name;
    Type type;
} Member;
static void Member_drop(Member* self);

static void ExtendsConditional_drop(ExtendsConditional* self) {
    Type_drop(&self->maybeSub);
    Type_drop(&self->maybeSuper);
}

static cstr ExtendsConditional_to_cstr(ExtendsConditional* self) {
    cstr subType = Type_to_cstr(&self->maybeSub);
    cstr superType = Type_to_cstr(&self->maybeSuper);
    cstr ret = cstr_from_fmt("%s extends %s", cstr_str(&subType), cstr_str(&superType));
    cstr_drop(&subType);
    cstr_drop(&superType);
    return ret;
}

static void TernaryType_drop(TernaryType* self) {
    ExtendsConditional_drop(&self->conditional);
    Type_drop(&self->trueType);
    Type_drop(&self->falseType);
}

static bool extends_conditional_eval(ExtendsConditional* self) {
    return extends(&self->maybeSub, &self->maybeSuper);
}

static cstr TernaryType_to_cstr(TernaryType* self) {
    cstr conditional = ExtendsConditional_to_cstr(&self->conditional);
    cstr trueType = Type_to_cstr(&self->trueType);
    cstr falseType = Type_to_cstr(&self->falseType);
    cstr ret = cstr_from_fmt("%s ? %s : %s", cstr_str(&conditional), cstr_str(&trueType), cstr_str(&falseType));
    cstr_drop(&conditional);
    cstr_drop(&trueType);
    cstr_drop(&falseType);
    return ret;
}

#define i_key_class Member
#define i_is_forward
#define i_no_clone
#include "stc/vec.h"

static cstr Interface_to_cstr(Interface* self) {
    cstr ret = cstr_lit("{ ");
    bool start = false;
    c_foreach(i, vec_Member, self->members) {
        if (!start) {
            cstr_append_sv(&ret, c_sv(", "));
        }
        cstr_append_s(&ret, i.ref->name);
        cstr_append_sv(&ret, c_sv(": "));
        cstr type = Type_to_cstr(&i.ref->type);
        cstr_append_s(&ret, type);
        cstr_drop(&type);
    }
    cstr_append_sv(&ret, c_sv(" }"));
    return ret;
}

static void Interface_drop(Interface* self) {
    vec_Member_drop(&self->members);
}

static void Type_drop(Type* self) {
    switch (self->tag) {
        case TYPE_NULL:
        case TYPE_ANY:
        case TYPE_UNDEFINED:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
            break;

        case TYPE_INTERFACE:
            Interface_drop(&self->data.interface);
            break;

        case TYPE_TERNARY:
            TernaryType_drop(self->data.ternary);
            free(self->data.ternary);
            break;
    }
}

static cstr Type_to_cstr(Type* self) {
    switch (self->tag) {
        case TYPE_NULL: return cstr_lit("null");
        case TYPE_ANY: return cstr_lit("any");
        case TYPE_UNDEFINED: return cstr_lit("undefined");
        case TYPE_BOOLEAN: return cstr_lit("boolean");
        case TYPE_NUMBER: return cstr_lit("number");
        case TYPE_STRING: return cstr_lit("string");
        case TYPE_INTERFACE: return Interface_to_cstr(&self->data.interface);
        case TYPE_TERNARY: return TernaryType_to_cstr(self->data.ternary);
    }
    UNREACHABLE;
}

static void Member_drop(Member* self) {
    cstr_drop(&self->name);
    Type_drop(&self->type);
}

static bool extends(Type *sub, Type *super) {
    switch (sub->tag) {
        case TYPE_NULL:
        case TYPE_ANY:
        case TYPE_UNDEFINED:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
            return sub->tag == super->tag;

        case TYPE_INTERFACE:
            PANIC("extends(TYPE_INTERFACE, ...) not implemented");
            break;

        case TYPE_TERNARY:
            PANIC("extends(TYPE_TERNARY, ...) not implemented");
            break;
    }
    UNREACHABLE;
}

static Type simplify(Type t) {
    switch (t.tag) {
        case TYPE_NULL:
        case TYPE_ANY:
        case TYPE_UNDEFINED:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_STRING:
            return t;

        case TYPE_INTERFACE:
            c_foreach(i, vec_Member, t.data.interface.members) {
                i.ref->type = simplify(i.ref->type);
            }
            return t;
            break;

        case TYPE_TERNARY:
            if (extends_conditional_eval(&t.data.ternary->conditional)) {
                Type_drop(&t.data.ternary->falseType);
                return simplify(t.data.ternary->trueType);
            } else {
                Type_drop(&t.data.ternary->trueType);
                return simplify(t.data.ternary->falseType);
            }
    }
    UNREACHABLE;
}

int main() {
    Type t = type_ternary_new(c_new(TernaryType, {
        .conditional = (ExtendsConditional){
            .maybeSub = type_boolean_new(),
            .maybeSuper = type_number_new(),
        },
        .trueType = type_number_new(),
        .falseType = type_string_new(),
    }));
    cstr initial_s = Type_to_cstr(&t);
    printf("   Initial: %s\n", cstr_str(&initial_s));
    cstr_drop(&initial_s);
    t = simplify(t);
    cstr simplified_s = Type_to_cstr(&t);
    printf("Simplified: %s\n", cstr_str(&simplified_s));
    cstr_drop(&simplified_s);
    return 0;
}
