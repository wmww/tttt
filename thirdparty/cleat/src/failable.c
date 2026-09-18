#include "failable.h"
#include "display.h"
#include <stc/csview.h>

static const csview color_ok     = c_sv_const_init("\x1b[1;32m");
static const csview color_error  = c_sv_const_init("\x1b[1;31m");
static const csview color_normal = c_sv_const_init("\x1b[0m");

ErrorVt const ErrorBaseVt = {
    .type_name = "ErrorBase",
};

void error_base_destroy(ErrorBase* me) {
    if (me) {
        if (me->vt->drop) me->vt->drop(me);
        free(me);
    }
}

DisplayCtx error_base_display(ErrorBase const* error, DisplayCtx d) {
    if (error) {
        d = color_display(color_error, d);
        d = error->vt->display(error, d);
        d = color_display(color_normal, d);
    } else {
        d = color_display(color_ok, d);
        d = lit_display("ok", d);
        d = color_display(color_normal, d);
    }
    return d;
}

cstr error_base_get_message(ErrorBase const* error) {
    return display_into_cstr(error_base_display(error, DEFAULT_DISPLAY));
}

void error_base_eprintln(ErrorBase const* error) {
    eprintln(error);
}

static DisplayCtx string_error_display(ErrorBase const* error, DisplayCtx d) {
    auto me = c_container_of(error, StringError, error);
    return sv_display(cstr_sv(&my message), d);
}

static void string_error_drop(ErrorBase* error) {
    auto me = c_container_of(error, StringError, error);
    cstr_drop(&my message);
}

ErrorBase* string_error_new(cstr message) {
    return &c_new(StringError, {
        .error.vt = &StringErrorVt,
        .message = message,
    })->error;
}

ErrorVt const StringErrorVt = {
    .type_name = "StringError",
    .super = &ErrorBaseVt,
    .display = string_error_display,
    .drop = string_error_drop,
};

failable err_fmt(const char* fmt, ...) {
    cstr message = cstr_null;
    va_list args;
    va_start(args, fmt);
    cstr_vfmt(&message, 0, fmt, args);
    va_end(args);
    return (FailableResult){.error = string_error_new(message)};
}

typedef ErrorBase* ErrorBasePtr;
static inline void error_base_ptr_drop(ErrorBasePtr* me) { error_base_destroy(*me); }
static inline DisplayCtx error_base_ptr_display(ErrorBasePtr me, DisplayCtx d) { return me->vt->display(me, d); }
#define g_class_key ErrorBasePtr, error_base_ptr
#define g_no_clone
#define g_display
#include "generic/vec.h"

struct ErrorCollection {
    ErrorBase error;
    VecErrorBasePtr sub_errrors;
    bool locked; // Used to prevent adding errors from multiple levels
};

static DisplayCtx error_collection_display(ErrorBase const* error, DisplayCtx d) {
    auto me = c_container_of(error, ErrorCollection, error);
    return vec_error_base_ptr_display(my sub_errrors, d);
}

static void error_collection_drop(ErrorBase* error) {
    auto me = c_container_of(error, ErrorCollection, error);
    vec_error_base_ptr_drop(&my sub_errrors);
}

ErrorVt const ErrorCollectionVt = {
    .type_name = "ErrorCollection",
    .super = &ErrorBaseVt,
    .display = error_collection_display,
    .drop = error_collection_drop,
};

void add_error_to_collection(FailableResult* collection, ErrorBase* error) {
    if (collection->error == nullptr) {
        // If there is no previous error, use the new one bare
        collection->error = error;
        // If it's a collection lock it so we don't end up appending to it
        if (error_downcast(collection->error, &ErrorCollectionVt)) {
            ((ErrorCollection*)collection->error)->locked = true;
        }
    } else if (error_downcast(collection->error, &ErrorCollectionVt) &&
        !((ErrorCollection*)collection->error)->locked
    ) {
        // If the previous error is already a collection (and it's not locked), add to it
        ErrorCollection* error_collection = (void*)collection->error;
        vec_error_base_ptr_push(&error_collection->sub_errrors, error);
    } else {
        // Else we have exactly one previous error. Create a new collection and add both it and the new one
        ErrorBase* previous_error = collection->error;
        collection->error = &c_new(ErrorCollection, {
            .error.vt = &ErrorCollectionVt,
            .sub_errrors = c_init(vec_error_base_ptr, {
                previous_error,
                error,
            }),
            .locked = false,
        })->error;
    }
}

void error_base_panic_internal(ErrorBase const* error, const char* file, long long line) {
    cstr message = cstr_d(error);
    default_log_fatal_abort_sv(csview_from(file), line, csview_null, cstr_sv(&message));
}

void failable_result_check_internal(FailableResult result, const char* file, long long line) {
    if (result.error) error_base_panic_internal(result.error, file, line);
}

void failable_result_drop(FailableResult* result) {
    error_base_destroy(result->error);
    result->error = nullptr;
}
