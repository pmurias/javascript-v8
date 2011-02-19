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
    } else if (value->IsArray()) {
        Handle<Array> arrayVal = Handle<Array>::Cast( value );
        AV *av = newAV();
        for (int i = 0; i < arrayVal->Length(); i++) {
            Handle<Value> elementVal = arrayVal->Get( v8::Integer::New( i ) );
            if ( elementVal->IsArray() || elementVal->IsObject() ) {
                av_push( av, newRV_noinc( _convert_v8value_to_sv( elementVal ) ) );
            } else {
                av_push( av, _convert_v8value_to_sv( elementVal ) );
            }
        }
        return (SV *) av;
    } else if (value->IsObject()) {
        Handle<Object> objectVal = Handle<Object>::Cast( value );
        HV *hv = newHV();
        Local<Array> properties = objectVal->GetPropertyNames();
        for (int i = 0; i < properties->Length(); i++) {
            Local<Integer> propertyIndex = Integer::New( i );
            Local<String> propertyName = Local<String>::Cast( properties->Get( propertyIndex ) );
            String::Utf8Value propertyNameUTF8( propertyName );

            Local<Value> propertyValue = objectVal->Get( propertyName );
            if ( propertyValue->IsArray() || propertyValue->IsObject() ) {
                hv_store(hv, *propertyNameUTF8, 0 - propertyNameUTF8.length(), newRV_noinc(_convert_v8value_to_sv( propertyValue ) ), 0 );
            } else {
                hv_store(hv, *propertyNameUTF8, 0 - propertyNameUTF8.length(), _convert_v8value_to_sv( propertyValue ), 0 );
            }
        }
        return (SV *) hv;
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
        /* TODO think about refcounts */
        XPUSHs(_convert_v8value_to_sv(args[i]));
    }
  
  
    PUTBACK;
  
    SV *code = (SV *) External::Unwrap(args.Data());
    count = call_sv(code, G_SCALAR);
  
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
V8Context::bind_function(const char *name,SV* code)
{
    HandleScope scope;
    TryCatch try_catch;

    Context::Scope context_scope(context);


    /*we will decrement the refcnt in the destructor*/
    SvREFCNT_inc(code);
    used.push_back(code);

    context->Global()->Set(
        String::New(name),
        FunctionTemplate::New(_perl_method,
                              External::Wrap((void *) code))->GetFunction()
    );
}
SV* V8Context::eval(const char* source) {
    HandleScope handle_scope;
    TryCatch try_catch;
    Context::Scope context_scope(context);
    Handle<Script> script = Script::Compile(String::New(source));

    if (try_catch.HasCaught()) {
        Handle<Value> exception = try_catch.Exception();
        String::AsciiValue exception_str(exception);
        sv_setpvn(ERRSV, *exception_str, exception_str.length());
        return &PL_sv_undef;
    } else {
        Handle<Value> val = script->Run();

        if (val.IsEmpty()) {
            Handle<Value> exception = try_catch.Exception();
            String::AsciiValue exception_str(exception);
            sv_setpvn(ERRSV, *exception_str, exception_str.length());
            return &PL_sv_undef;
        } else {
            sv_setsv(ERRSV,&PL_sv_undef);
            return _convert_v8value_to_sv(val);
        }
    }
}

V8Context::~V8Context() {
      for (int i=0;i<used.size();i++) {
          SvREFCNT_dec(used[i]);
      }
      context.Dispose();
}
