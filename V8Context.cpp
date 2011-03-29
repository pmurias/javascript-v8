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
    return 1000;
    /*
     * There are horrible bugs in the current Devel::Size, so we can't do this
     * accurately. But if there weren't, this is how we'd do it!
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
    IV size    = SvIV(POPs);
    PUTBACK;
    FREETMPS;
    LEAVE;

    return size;
    */
}

namespace
{
    class CVInfo
    {
        SV*        ref;
        IV         bytes;
        V8Context* context;

    public:
        CVInfo(CV *cv, V8Context *ctx)
            : context(ctx)
            , ref(newRV_inc((SV*)cv))
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
        ENTER;
        SAVETMPS;

        PUSHMARK(SP);
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

        Handle<Value> v = context->sv2v8(POPs);

        PUTBACK;
        FREETMPS;
        LEAVE;

        return v;
    }

    class ClosureData
    {
    public:
        V8Context *context;
        Persistent<Function> function;

        ClosureData(CV* code, V8Context *ctx, Handle<Function> fn)
            : context(ctx)
            , function(Persistent<Function>::New(fn))
        {
            SV *ptr = newSViv((IV) this);
            sv_magicext((SV*) code, ptr, PERL_MAGIC_ext,
                &ClosureData::vtable, "v8closure", 0);
            SvREFCNT_dec(ptr); // refcnt is incremented by sv_magicext
            CvXSUBANY(code).any_ptr = static_cast<void*>(this);
        };
        ~ClosureData() {
            function.Dispose();
        };
        static ClosureData *for_cv(CV *code) {
            return static_cast<ClosureData*>(CvXSUBANY(code).any_ptr);
        }

    private:
        static MGVTBL vtable;
        static int svt_free(pTHX_ SV*, MAGIC*);
    };

    MGVTBL ClosureData::vtable = {
        0,
        0,
        0,
        0,
        ClosureData::svt_free,
        0,
        0,
        0
    };

    int ClosureData::svt_free(pTHX_ SV* sv, MAGIC* mg) {
        ClosureData *data = ClosureData::for_cv((CV *)sv);
        delete data;
        return 0;
    };
};

// V8Context class starts here

V8Context::V8Context() {
    context = Context::New();
}

V8Context::~V8Context() {
    context.Dispose();
    while(!V8::IdleNotification()); // force garbage collection
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
        return cv2function((CV*)ref);
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
V8Context::cv2function(CV *cv) {
    CVInfo *code = new CVInfo(cv, this);

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

    HandleScope     scope;
    ClosureData    *data = ClosureData::for_cv(cv);
    V8Context      *self = data->context;
    Handle<Context> ctx  = self->context;
    Context::Scope  context_scope(ctx);
    Handle<Value>   argv[items];

    for (I32 i = 0; i < items; i++) {
        argv[i] = self->sv2v8(ST(i));
    }

    ST(0) = self->v82sv(data->function->Call(ctx->Global(), items, argv));
    sv_2mortal(ST(0));
    XSRETURN(1);
}

SV*
V8Context::function2sv(Handle<Function> fn) {
    CV          *code = newXS(NULL, v8closure, __FILE__);
    ClosureData *data = new ClosureData(code, this, fn);
    return newRV_noinc((SV*)code);
}
