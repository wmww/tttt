#include "error.h"

void ErrorCtx_drop(ErrorCtx* self) {
    vec_Error_drop(&self->errors);
}

void ErrorCtx_display(ErrorCtx* self, const char* src) {
    c_foreach(i, vec_Error, self->errors) {
        const char* start = nullptr;
        const char* end = nullptr;
        int line = 1;
        if (i.ref->location > src) {
            start = i.ref->location;
            while (start > src && *start != '\n') {
                start--;
            }
            if (*start == '\n') {
                start++;
            }
            for (const char* i = src; i < start; i++) {
                if (*i == '\n') {
                    line++;
                }
            }
            end = i.ref->location;
            while(*end && *end != '\n') {
                end++;
            }
        }
        printf("line %d: " CLR_ERR "Error:" CLR0 " %s\n", line, i.ref->static_message);
        if (start && end) {
            printf(CLR_STR "%.*s" CLR0 "\n", (int)(end - start), start);
            for (;start < i.ref->location; start++) {
                printf(" ");
            }
            printf(CLR_ERR "^" CLR0 "\n");
        } else {
            printf(CLR_ERR "(invalid location %p)" CLR0 "\n", (void*)i.ref->location);
        }
    }
}

bool ErrorCtx_empty(ErrorCtx* self) {
    return vec_Error_empty(&self->errors);
}

void ErrorCtx_append_consume(ErrorCtx* self, ErrorCtx other) {
    c_foreach(i, vec_Error, other.errors) {
        vec_Error_push(&self->errors, *i.ref);
        *i.ref = (Error){0};
    }
    ErrorCtx_drop(&other);
}

void ErrorCtx_parse_error(ErrorCtx* self, const char* location, const char* static_message) {
    if (!self) {
        return;
    }
    vec_Error_push(&self->errors, (Error){
        .location = location,
        .static_message = static_message,
    });
}

void ErrorCtx_type_error(ErrorCtx* self, const char* static_message) {
    if (!self) {
        return;
    }
    vec_Error_push(&self->errors, (Error){
        .location = nullptr,
        .static_message = static_message,
    });
}
