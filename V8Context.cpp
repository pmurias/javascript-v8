#include <v8.h>
#include "V8Context.h"
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
#undef New
using namespace v8;
static SV *_convert_v8value_to_sv(Handle<Value> value)
{
    if (value->IsUndefined()) {
        return &PL_sv_undef;
    } else if (value->IsNull()) {
        return &PL_sv_undef;
    } else if (value->IsInt32()) {
        return newSViv(value->Int32Value());
    } else if (value->IsBoolean()) {
        return newSVuv(value->Uint32Value());
    } else if (value->IsNumber()) {
        return newSVnv(value->NumberValue());
    } else if (value->IsString()) {
        SV *sv = newSVpv(*(String::Utf8Value(value)), 0);
        sv_utf8_decode(sv);
        return sv;
    } else {
        croak("Can not convert js value to a perl one");
        return &PL_sv_undef;
    }
}
SV* V8Context::eval(const char* source) {
    HandleScope handle_scope;
    Persistent<Context> context = Context::New();
    Context::Scope context_scope(context);
    Handle<Script> script = Script::Compile(String::New(source));
    SV* result = _convert_v8value_to_sv(script->Run());
    context.Dispose();
    return result;
}
