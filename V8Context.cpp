#include <v8.h>
#include "V8Context.h"
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
#undef New

using namespace v8;

// Internally-used wrapper around coderefs
static IV
calculate_size(SV *sv) {
    dSP;
    ENTER;
    SAVETMPS;

    PUSHMARK(SP);
    XPUSHs(sv);
    PUTBACK;
    int returned = call_pv("Devel::Size::total_size", G_SCALAR);
    if (returned != 1) {
        warn("Error calculating sv size");
        return 0;
    }

    SPAGAIN;
    SV *result = POPs;
    IV size    = SvIV(result);
    PUTBACK;
    FREETMPS;
    LEAVE;

    return size;
}

namespace
{
    class CVInfo
    {
        SV*        ref;
        IV         bytes;
        V8Context* context;

    public:
        CVInfo(SV *sv, V8Context *ctx)
            : context(ctx)
            , ref(newSVsv(sv))
            , bytes(calculate_size(ref) + sizeof(CVInfo))
        {
            V8::AdjustAmountOfExternalAllocatedMemory(bytes);
        };

        ~CVInfo() {
            SvREFCNT_dec(ref);
            V8::AdjustAmountOfExternalAllocatedMemory(-bytes);
        };

        static void destroy(Persistent<Value> o, void *parameter) {
            CVInfo *code = static_cast<CVInfo*>(External::Unwrap(o));
            delete code;
        };

        static Handle<Value> v8invoke(const Arguments& args) {
            CVInfo *code = static_cast<CVInfo*>(External::Unwrap(args.Data()));
            return code->invoke(args);
        }

        Handle<Value> invoke(const Arguments& args);
    };

    Handle<Value>
    CVInfo::invoke(const Arguments& args)
    {
        int len = args.Length();

        dSP;
        PUSHMARK(SP);
        ENTER;
        SAVETMPS;

        for (int i = 0; i < len; i++) {
            SV *arg = context->v82sv(args[i]);
            mXPUSHs(arg);
        }
        PUTBACK;
        int count = call_sv(ref, G_SCALAR);
        SPAGAIN;

        if (count != 1) {
            warn("Error invoking CV from V8");
            return Undefined();
        }

        SV *result = POPs;
        Handle<Value> v = context->sv2v8(result);

        PUTBACK;
        FREETMPS;
        LEAVE;

        return v;
    }
};

// V8Context class starts here

V8Context::V8Context() {
    context = Context::New();
}

V8Context::~V8Context() {
    context.Dispose();
}

void
V8Context::bind(const char *name, SV *thing) {
    HandleScope scope;
    Context::Scope context_scope(context);

    context->Global()->Set(String::New(name), sv2v8(thing));
}

SV*
V8Context::eval(const char* source) {
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
            return v82sv(val);
        }
    }
}

Handle<Value>
V8Context::sv2v8(SV *sv) {
    if (SvROK(sv))
        return rv2v8(sv);
    if (SvPOK(sv))
        return String::New(SvPV_nolen(sv));
    if (SvIOK_UV(sv))
        return Uint32::New(SvUV(sv));
    if (SvIOK(sv))
        return Integer::New(SvIV(sv));
    if (SvNOK(sv))
        return Number::New(SvNV(sv));

    warn("Unkown sv type in sv2v8");
    return Undefined();
}

SV *
V8Context::v82sv(Handle<Value> value) {
    if (value->IsUndefined())
        return &PL_sv_undef;

    if (value->IsNull())
        return &PL_sv_undef;

    if (value->IsInt32())
        return newSViv(value->Int32Value());

    if (value->IsBoolean())
        return newSVuv(value->Uint32Value());

    if (value->IsNumber())
        return newSVnv(value->NumberValue());

    if (value->IsString()) {
        SV *sv = newSVpv(*(String::Utf8Value(value)), 0);
        sv_utf8_decode(sv);
        return sv;
    }

    if (value->IsArray()) {
        Handle<Array> array = Handle<Array>::Cast(value);
        return array2sv(array);
    }

    if (value->IsFunction()) {
        Handle<Function> fn = Handle<Function>::Cast(value);
        return function2sv(fn);
    }

    if (value->IsObject()) {
        Handle<Object> object = Handle<Object>::Cast(value);
        return object2sv(object);
    }

    warn("Unknown v8 value in v82sv");
    return &PL_sv_undef;
}

Handle<Value>
V8Context::rv2v8(SV *sv) {
    SV *ref  = SvRV(sv);
    svtype t = SvTYPE(ref);
    if (t == SVt_PVAV) {
        return av2array((AV*)ref);
    }
    if (t == SVt_PVHV) {
        return hv2object((HV*)ref);
    }
    if (t == SVt_PVCV) {
        return cv2function(sv);
    }
    warn("Unknown reference type in sv2v8()");
    return Undefined();
}

Handle<Array>
V8Context::av2array(AV *av) {
    I32 i, len = av_len(av) + 1;
    Handle<Array> array = Array::New(len);
    for (i = 0; i < len; i++) {
        array->Set(i, sv2v8(*av_fetch(av, i, 0)));
    }
    return array;
}

Handle<Object>
V8Context::hv2object(HV *hv) {
    I32 len;
    char *key;
    SV *val;

    hv_iterinit(hv);
    Handle<Object> object = Object::New();
    while (val = hv_iternextsv(hv, &key, &len)) {
        object->Set(String::New(key, len), sv2v8(val));
    }
    return object;
}

Handle<Function>
V8Context::cv2function(SV *sv) {
    CVInfo *code = new CVInfo(sv, this);

    Local<External>         wrap = External::New((void*) code);
    Persistent<External>    weak = Persistent<External>::New(wrap);
    Local<FunctionTemplate> tmpl = FunctionTemplate::New(CVInfo::v8invoke, weak);
    Handle<Function>        fn   = tmpl->GetFunction();
    weak.MakeWeak(static_cast<void*>(code), CVInfo::destroy);

    return fn;
}

SV*
V8Context::array2sv(Handle<Array> array) {
    AV *av = newAV();
    for (int i = 0; i < array->Length(); i++) {
        Handle<Value> elementVal = array->Get( Integer::New( i ) );
        av_push( av, v82sv( elementVal ) );
    }
    return newRV_noinc((SV *) av);
}

SV *
V8Context::object2sv(Handle<Object> obj) {
    HV *hv = newHV();
    Local<Array> properties = obj->GetPropertyNames();
    for (int i = 0; i < properties->Length(); i++) {
        Local<Integer> propertyIndex = Integer::New( i );
        Local<String> propertyName = Local<String>::Cast( properties->Get( propertyIndex ) );
        String::Utf8Value propertyNameUTF8( propertyName );

        Local<Value> propertyValue = obj->Get( propertyName );
        hv_store(hv, *propertyNameUTF8, 0 - propertyNameUTF8.length(), v82sv( propertyValue ), 0 );
    }
    return newRV_noinc((SV*)hv);
}

XS(v8closure) {
#ifdef dVAR
    dVAR;
#endif
    dXSARGS;
    AV *data = (AV *) CvXSUBANY(cv).any_ptr;

    V8Context       *self =
        reinterpret_cast<V8Context*>(SvIV(*av_fetch(data, 0, 0)));
    Handle<Function> fn   = Handle<Function>(
        reinterpret_cast<Function*>(SvIV(*av_fetch(data, 1, 0)))
    );

    HandleScope      scope;
    Context::Scope   context_scope(self->context);
    Handle<Value>   *argv = new Handle<Value>[items];

    for (I32 i = 0; i < items; i++) {
        argv[i] = self->sv2v8(ST(i));
    }

    Handle<Object> global = self->context->Global();
    Handle<Value>  result = fn->Call(global, items, argv);

    delete[] argv;

    ST(0) = self->v82sv(result);
    sv_2mortal(ST(0));
    XSRETURN(1);
}

static int free_function(pTHX_ SV* sv, MAGIC* mg) {
    AV *data = (AV *) CvXSUBANY(sv).any_ptr;
    Persistent<Function> fn =
        reinterpret_cast<Function*>(SvIV(*av_fetch(data, 1, 0)));
    fn.Dispose();
    return 0;
}

static MGVTBL v8closure_vtbl = {
    0,
    0,
    0,
    0,
    free_function, /* svt_free */
    0,
    0,
    0
};

SV*
V8Context::function2sv(Handle<Function> fn) {
    CV *code = newXS(NULL, v8closure, __FILE__);
    AV *data = newAV();
    av_push(data, newSViv((IV) this));
    av_push(data, newSViv((IV) *Persistent<Function>::New(fn)));

    MAGIC *magic = sv_magicext((SV*) code, (SV*) data, PERL_MAGIC_ext,
        &v8closure_vtbl, "v8closure", 0);
    SvREFCNT_dec(data); // Incremented in sv_magicext

    /* Attaching our data as magic will make it get freed when the CV gets
     * freed. We're going to attach it here too though so we can get at it
     * quickly. */
    CvXSUBANY(code).any_ptr = (void*) data;

    return newRV_noinc((SV*)code);
}
