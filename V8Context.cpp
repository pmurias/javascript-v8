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
_perl_method(const Arguments &args)
{
    dSP;
    int count;
    Handle<Value> result = Undefined();

    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
  
    for (int i = 0; i < args.Length(); i ++) {
        //TODO think about refcounts
        XPUSHs(_convert_v8value_to_sv(args[i]));
    }
  
  
    PUTBACK;
  
    count = call_sv(INT2PTR(SV*,args.Data()->Int32Value()),G_SCALAR);
  
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
V8Context::bind_function(const char *name,SV* cod)
{
    HandleScope scope;
    TryCatch try_catch;

    Context::Scope context_scope(context);


    SvREFCNT_inc(cod);

    context->Global()->Set(
        String::New(name),
        FunctionTemplate::New(_perl_method,
                              Integer::New(PTR2IV(cod)))->GetFunction()
    );
}
SV* V8Context::eval(const char* source) {
    HandleScope handle_scope;
    Context::Scope context_scope(context);
    Handle<Script> script = Script::Compile(String::New(source));
    TryCatch try_catch;
    Handle<Value> val = script->Run();
    if (val.IsEmpty()) {
        Handle<Value> exception = try_catch.Exception();
        String::AsciiValue exception_str(exception);
        printf("error\n");
        sv_setpv(ERRSV,*exception_str);
        return &PL_sv_undef;
    } else {
        sv_setsv(ERRSV,&PL_sv_undef);
        return _convert_v8value_to_sv(val);
    }
}
