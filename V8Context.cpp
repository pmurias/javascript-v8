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
static Handle<Value>
_convert_sv_to_v8value(SV *sv)
{
    HandleScope scope;

    if (0) ;
    else if (SvIOK_UV(sv))
        return Uint32::New(SvUV(sv));
    else if (SvIOK(sv))
        return Integer::New(SvIV(sv));
    else if (SvNOK(sv))
        return Number::New(SvNV(sv));
    else if (SvPOK(sv))
        return String::New(SvPV_nolen(sv));

    return Undefined();
}

static Handle<Value>
_perl_method_by_name(const Arguments &args)
{
    dSP;
    int count;
    Handle<Value> result = Undefined();
    char ** arguments;

    ENTER;
    SAVETMPS;

    String::Utf8Value method(args.Data()->ToString());
    if (0) Perl_warn(aTHX_ "method called: %s", *method);

    arguments = new char *[args.Length() + 1];
    for (int i = 0; i < args.Length(); i ++) {
        String::Utf8Value str(args[i]);
        arguments[i] = savepv(*str);
    }
    arguments[args.Length()] = NULL;

    count = call_argv(*method, G_SCALAR, arguments);

    for (int i = 0; i < args.Length(); i ++) {
        Safefree(arguments[i]);
    }
    delete arguments;

    SPAGAIN;

    if (count >= 1) {
        result = _convert_sv_to_v8value(POPs);
    }

    PUTBACK;
    FREETMPS;
    LEAVE;

    return result;
}



void
V8Context::register_method_by_name(const char *method)
{
    HandleScope scope;
    TryCatch try_catch;

    Context::Scope context_scope(context);

    context->Global()->Set(
        String::New(method),
        FunctionTemplate::New(_perl_method_by_name,
                              String::New(method))->GetFunction()
    );
}
SV* V8Context::eval(const char* source) {
    HandleScope handle_scope;
    Context::Scope context_scope(context);
    Handle<Script> script = Script::Compile(String::New(source));
    SV* result = _convert_v8value_to_sv(script->Run());
    return result;
}
