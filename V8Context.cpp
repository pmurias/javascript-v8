#include <v8.h>
#include "V8Context.h"
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
#undef New
#include <pthread.h>
#include <time.h>

#undef do_open
#undef do_close

#include <sstream>

#define L(...) fprintf(stderr, ##__VA_ARGS__)

using namespace v8;
using namespace std;

int V8Context::number = 0;

void set_perl_error(const TryCatch& try_catch) {
    Handle<Message> msg = try_catch.Message();

    char message[1024];
    snprintf(
        message,
        1024,
        "%s at %s:%d",
        *(String::Utf8Value(try_catch.Exception())),
        !msg.IsEmpty() ? *(String::AsciiValue(msg->GetScriptResourceName())) : "EVAL",
        !msg.IsEmpty() ? msg->GetLineNumber() : 0
    );

    sv_setpv(ERRSV, message);
    sv_utf8_upgrade(ERRSV);
}

Handle<Value>
check_perl_error() {
    if (!SvOK(ERRSV))
        return Handle<Value>();

    const char *err = SvPV_nolen(ERRSV);

    if (err && strlen(err) > 0) {
        Handle<String> error = String::New(err, strlen(err) - 1); // no newline
        sv_setsv(ERRSV, &PL_sv_no);
        Handle<Value> v = ThrowException(Exception::Error(error));
        return v;
    }

    return Handle<Value>();
}

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

#define SETUP_PERL_CALL(PUSHSELF) \
    int len = args.Length(); \
\
    dSP; \
    ENTER; \
    SAVETMPS; \
\
    PUSHMARK(SP); \
\
    PUSHSELF; \
\
    for (int i = 1; i < len; i++) { \
        SV *arg = context->v82sv(args[i]); \
        mXPUSHs(arg); \
    } \
    PUTBACK;

#define CONVERT_PERL_RESULT() \
    Handle<Value> error = check_perl_error(); \
\
    if (!error.IsEmpty()) { \
        FREETMPS; \
        LEAVE; \
        return error; \
    } \
    SPAGAIN; \
\
    Handle<Value> v = context->sv2v8(POPs); \
\
    PUTBACK; \
    FREETMPS; \
    LEAVE; \
\
    return v;


V8ObjectData::V8ObjectData(Handle<Object> object_, SV* sv_, V8Context* context_, int hash_)
    : context(context_)
    , sv(sv_)
    , object(Persistent<Object>::New(object_))
    , hash(hash_)
{ 
    context->register_v8_object(this);
    SV *ptr = newSViv((IV) this);
    sv_magicext(sv, ptr, PERL_MAGIC_ext,
        &V8ObjectData::vtable, "v8closure", 0);
    SvREFCNT_dec(ptr); // refcnt is incremented by sv_magicext
}

V8ObjectData::~V8ObjectData() {
    if (context) context->remove_v8_object(this);
    object.Dispose();
}

MGVTBL V8ObjectData::vtable = {
    0,
    0,
    0,
    0,
    V8ObjectData::svt_free
};

int V8ObjectData::svt_free(pTHX_ SV* sv, MAGIC* mg) {
    delete (V8ObjectData*)SvIV(mg->mg_obj);
    return 0;
};

V8ObjectData* sv_object_data(SV* sv) {
    if (MAGIC *mg = mg_find(sv, PERL_MAGIC_ext)) {
        if (mg->mg_virtual == &V8ObjectData::vtable) {
            return (V8ObjectData*)SvIV(mg->mg_obj);
        }
    }
    return NULL;
}

inline void PerlObjectData::init (SV* sv_, V8Context* context_) {
    context = context_;
    set_sv(sv_);
    add_size(size());
}

PerlObjectData::PerlObjectData(SV* sv_, V8Context* context_) {
    init(sv_, context_);
}

PerlObjectData::PerlObjectData(SV* sv_, Handle<Object> object_, V8Context* context_) {
    init(sv_, context_);
    set_object(object_);
}

size_t PerlObjectData::size() {
    return sizeof(PerlObjectData);
}

inline void PerlObjectData::add_size(size_t bytes_) {
    bytes += bytes_;
    V8::AdjustAmountOfExternalAllocatedMemory(bytes_);
}

inline void PerlObjectData::set_sv(SV* sv_) {
    sv = sv_;
    if (sv) {
        SvREFCNT_inc(sv);
        add_size(calculate_size(sv));
        ptr = PTR2IV(SvRV(sv));
    }
}

inline void PerlObjectData::set_object(Handle<Object> object_) {
    object = Persistent<Object>::New(object_);
    object.MakeWeak(this, PerlObjectData::destroy);
    context->register_perl_object(this); // register only when we have an object
}

PerlObjectData::~PerlObjectData() {
    if (sv) {
        add_size(-bytes);
        SvREFCNT_dec((SV*)sv);
    }
    context->remove_perl_object(this);
    object.Dispose();
}

void PerlObjectData::destroy(Persistent<Value> object, void *data) {
    delete static_cast<PerlObjectData*>(data);
}

class PerlFunctionData : public PerlObjectData {
private:
    SV *rv;

protected:
    virtual Handle<Value> invoke(const Arguments& args);
    virtual size_t size();

public:
    PerlFunctionData(CV *cv, V8Context* context_) 
        : PerlObjectData((SV*)cv, context_)
    { 
        if (cv) rv = newRV_noinc((SV*)cv);
        Handle<Value> wrap = External::Wrap(this);
        Handle<Value> fn = context->make_function->Call(context->context->Global(), 1, &wrap);
        set_object(Handle<Object>::Cast(fn));
    }

    static Handle<Value> v8invoke(const Arguments& args) {
        PerlFunctionData* data = static_cast<PerlFunctionData*>(External::Unwrap(args[0]));
        return data->invoke(args);
    }
};

size_t PerlFunctionData::size() {
    return sizeof(PerlFunctionData);
}

Handle<Value>
PerlFunctionData::invoke(const Arguments& args) {
    SETUP_PERL_CALL();
    int count = call_sv(rv, G_SCALAR);
    CONVERT_PERL_RESULT();
}

class PerlMethodData : public PerlFunctionData {
private:
    string name;
    virtual Handle<Value> invoke(const Arguments& args);
    virtual size_t size();

public:
    PerlMethodData(const char* name_, V8Context* context_)
        : PerlFunctionData(NULL, context_)
        , name(name_)
    { }
};

Handle<Value>
PerlMethodData::invoke(const Arguments& args) {
    SETUP_PERL_CALL(mXPUSHs(context->v82sv(args.This())))
    int count = call_method(name.c_str(), G_SCALAR | G_EVAL);
    CONVERT_PERL_RESULT()
}

size_t PerlMethodData::size() {
    return sizeof(PerlMethodData);
}

// V8Context class starts here

V8Context::V8Context(int time_limit, const char* flags, bool enable_blessing_, const char* bless_prefix_)
    : time_limit_(time_limit),
      bless_prefix(bless_prefix_),
      enable_blessing(enable_blessing_)
{ 
    V8::SetFlagsFromString(flags, strlen(flags));
    context = Context::New();

    Context::Scope context_scope(context);
    HandleScope handle_scope;

    Local<FunctionTemplate> tmpl = FunctionTemplate::New(PerlFunctionData::v8invoke);
    context->Global()->Set(
        String::New("__perlFunctionWrapper"),
        tmpl->GetFunction()
    );

    Handle<Script> script = Script::Compile(
        String::New(
            "(function(wrap) {"
            "    return function() {"
            "        var args = Array.prototype.slice.call(arguments);"
            "        args.unshift(wrap);"
            "        return __perlFunctionWrapper.apply(this, args)"
            "    };"
            "})"
        )
    );
    make_function = Persistent<Function>::New(Handle<Function>::Cast(script->Run()));

    number++;    
}

void V8Context::register_v8_object(V8ObjectData* data) {
    seen_v8[data->hash] = PTR2IV(data->sv);
    objects.push_back(data);
}

void V8Context::remove_v8_object(V8ObjectData* data) {
    SvMap::iterator it = seen_v8.find(data->hash);
    if (it != seen_v8.end())
        seen_v8.erase(it);

    for (vector<V8ObjectData*>::iterator it = objects.begin(); it != objects.end(); it++) {
        if (*it == data) {
            objects.erase(it);
            break;
        }
    }
}

void V8Context::register_perl_object(PerlObjectData* data) {
    if (int ptr = data->ptr)
        seen_perl[data->ptr] = data->object;
}

void V8Context::remove_perl_object(PerlObjectData* data) {
    HandleMap::iterator it = seen_perl.find(data->ptr);
    if (it != seen_perl.end())
        seen_perl.erase(it);
}

V8Context::~V8Context() {
    for (SvMap::iterator it = seen_v8.begin(); it != seen_v8.end(); it++) {
        SV* sv = INT2PTR(SV*, it->second);
        sv = &PL_sv_undef;
    }
    seen_v8.clear();
    for (vector<V8ObjectData*>::iterator it = objects.begin(); it != objects.end(); it++) {
        (*it)->context = NULL;
    }
    for (ObjectMap::iterator it = prototypes.begin(); it != prototypes.end(); it++) {
      it->second.Dispose();
    }
    context.Dispose();
    while(!V8::IdleNotification()); // force garbage collection
}

void
V8Context::bind(const char *name, SV *thing) {
    HandleScope scope;
    Context::Scope context_scope(context);

    context->Global()->Set(String::New(name), sv2v8(thing));
}

// I fucking hate pthreads, this lacks error handling, but hopefully works.
class thread_canceller {
public:
    thread_canceller(int sec)
        : sec_(sec)
    {
        if (sec_) {
            pthread_cond_init(&cond_, NULL);
            pthread_mutex_init(&mutex_, NULL);
            pthread_mutex_lock(&mutex_); // passed locked to canceller
            pthread_create(&id_, NULL, canceller, this);
        }
    }

    ~thread_canceller() {
        if (sec_) {
            pthread_mutex_lock(&mutex_);
            pthread_cond_signal(&cond_);
            pthread_mutex_unlock(&mutex_);
            void *ret;
            pthread_join(id_, &ret);
            pthread_mutex_destroy(&mutex_);
            pthread_cond_destroy(&cond_);
        }
    }

private:

    static void* canceller(void* this_) {
        thread_canceller* me = static_cast<thread_canceller*>(this_);
        struct timeval tv;
        struct timespec ts;
        gettimeofday(&tv, NULL);
        ts.tv_sec = tv.tv_sec + me->sec_;
        ts.tv_nsec = tv.tv_usec * 1000;

        if (pthread_cond_timedwait(&me->cond_, &me->mutex_, &ts) == ETIMEDOUT) {
            V8::TerminateExecution();
        }
        pthread_mutex_unlock(&me->mutex_);
    }

    pthread_t id_;
    pthread_cond_t cond_;
    pthread_mutex_t mutex_;
    int sec_;
};

SV*
V8Context::eval(SV* source, SV* origin) {
    HandleScope handle_scope;
    TryCatch try_catch;
    Context::Scope context_scope(context);

    Handle<Script> script = Script::Compile(
        sv2v8str(source),
        origin ? sv2v8str(origin) : String::New("EVAL")
    );

    if (try_catch.HasCaught()) {
        set_perl_error(try_catch);
        return &PL_sv_undef;
    } else {
        thread_canceller canceller(time_limit_);
        Handle<Value> val = script->Run();

        if (val.IsEmpty()) {
            set_perl_error(try_catch);
            return &PL_sv_undef;
        } else {
            sv_setsv(ERRSV,&PL_sv_undef);
            return v82sv(val);
        }
    }
}

Handle<Value>
V8Context::sv2v8(SV *sv, HandleMap& seen) {
    if (SvROK(sv))
        return rv2v8(sv, seen);
    if (SvPOK(sv)) {
        // Upgrade string to UTF-8 if needed
        char *utf8 = SvPVutf8_nolen(sv);
        return String::New(utf8, SvCUR(sv));
    }
    if (SvIOK(sv)) {
        IV v = SvIV(sv);
        return (v >> 32) > 0 ? Number::New(v) : (Handle<Number>)Integer::New(v);
    }
    if (SvNOK(sv))
        return Number::New(SvNV(sv));
    if (!SvOK(sv))
        return Undefined();
 
    warn("Unknown sv type in sv2v8");
    return Undefined();
}

Handle<Value>
V8Context::sv2v8(SV *sv) {
    HandleMap seen;
    return sv2v8(sv, seen);
}

Handle<String> V8Context::sv2v8str(SV* sv)
{
    // Upgrade string to UTF-8 if needed
    char *utf8 = SvPVutf8_nolen(sv);
    return String::New(utf8, SvCUR(sv));
}

inline SV* find_seen(const SvMap& seen, int hash) {
    SvMap::const_iterator it = seen.find(hash);
    if (it == seen.end())
        return NULL;
    SV* cached = INT2PTR(SV*, it->second);
    return newRV(cached);
}

#undef TRY_CACHE
#define TRY_CACHE(s, p) \
if (SV* cached = find_seen(s, p)) \
    return cached; \

SV *
V8Context::v82sv(Handle<Value> value, SvMap& seen) {
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
        String::Utf8Value str(value);
        SV *sv = newSVpvn(*str, str.length());
        sv_utf8_decode(sv);
        return sv;
    }

    if (value->IsArray() || value->IsObject() || value->IsFunction()) {
        int hash = value->ToObject()->GetIdentityHash();

        TRY_CACHE(seen, hash);
        TRY_CACHE(seen_v8, hash);

        if (value->IsFunction()) {
            Handle<Function> fn = Handle<Function>::Cast(value);
            return function2sv(fn, hash);
        }

        if (value->IsArray()) {
            Handle<Array> array = Handle<Array>::Cast(value);
            return array2sv(array, seen, hash);
        }

        if (value->IsObject()) {
            Handle<Object> object = Handle<Object>::Cast(value);
            return object2sv(object, seen, hash);
        }
    }

    warn("Unknown v8 value in v82sv");
    return &PL_sv_undef;
}

SV *
V8Context::v82sv(Handle<Value> value) {
    SvMap seen;
    return v82sv(value, seen);
}

void 
V8Context::fill_prototype(Handle<Object> prototype, HV* stash) {
    HE *he;
    while (he = hv_iternext(stash)) {
        SV *key = HeSVKEY_force(he);
        Local<String> name = String::New(SvPV_nolen(key));

        if (prototype->Has(name))
            continue;

        prototype->Set(name, (new PerlMethodData(SvPV_nolen(key), this))->object);
    }
}

Handle<Object>
V8Context::get_prototype(SV *sv) {
    HV *stash = SvSTASH(SvRV(sv));
    char *package = HvNAME(stash);

    std::string pkg(package);
    ObjectMap::iterator it;

    Persistent<Object> prototype;

    it = prototypes.find(pkg);
    if (it != prototypes.end()) {
        prototype = it->second;
    }
    else {
        prototype = prototypes[pkg] = Persistent<Object>::New(Object::New());

        if (AV *isa = mro_get_linear_isa(stash)) {
            for (int i = 0; i <= av_len(isa); i++) {
                SV **sv = av_fetch(isa, i, 0);
                HV *stash = gv_stashsv(*sv, 0);
                fill_prototype(prototype, stash);
            }
        }
    }

    return prototype;
}

inline Handle<Value> find_seen(const HandleMap& seen, int ptr) {
    HandleMap::const_iterator it = seen.find(ptr);
    if (it != seen.end()) {
        return it->second;
    }
    return Handle<Value>();
}

#undef TRY_CACHE
#define TRY_CACHE(s, p) \
{ \
    Handle<Value> cached(find_seen(s, p)); \
    if (!cached.IsEmpty()) \
        return cached; \
}

Handle<Value>
V8Context::rv2v8(SV *sv, HandleMap& seen) {
    SV *ref  = SvRV(sv);

    if (V8ObjectData* data = sv_object_data(ref))
        return data->object;

    int ptr = PTR2IV(ref);

    TRY_CACHE(seen, ptr);
    TRY_CACHE(seen_perl, ptr);

    unsigned t = SvTYPE(ref);
    if (sv_isobject(sv)) {
        return blessed2object(sv);
    }
    if (t == SVt_PVAV) {
        return av2array((AV*)ref, seen, ptr);
    }
    if (t == SVt_PVHV) {
        return hv2object((HV*)ref, seen, ptr);
    }
    if (t == SVt_PVCV) {
        return cv2function((CV*)ref);
    }

    warn("Unknown reference type in sv2v8()");
    return Undefined();
}

Handle<Object>
V8Context::blessed2object(SV *sv) {
    Handle<Object> object = Object::New();

    object->SetHiddenValue(String::New("perlPtr"), External::Wrap(sv));
    object->SetPrototype(get_prototype(sv));

    new PerlObjectData(sv, object, this);

    return object;
}

Handle<Array>
V8Context::av2array(AV *av, HandleMap& seen, long ptr) {
    I32 i, len = av_len(av) + 1;
    Handle<Array> array = Array::New(len);
    seen[ptr] = array;
    for (i = 0; i < len; i++) {
        array->Set(Integer::New(i), sv2v8(*av_fetch(av, i, 0), seen));
    }
    return array;
}

Handle<Object>
V8Context::hv2object(HV *hv, HandleMap& seen, long ptr) {
    I32 len;
    char *key;
    SV *val;

    hv_iterinit(hv);
    Handle<Object> object = Object::New();
    seen[ptr] = object;
    while (val = hv_iternextsv(hv, &key, &len)) {
        object->Set(String::New(key, len), sv2v8(val, seen));
    }
    return object;
}

Handle<Object>
V8Context::cv2function(CV *cv) {
    return (new PerlFunctionData(cv, this))->object;
}

SV*
V8Context::array2sv(Handle<Array> array, SvMap& seen, int hash) {
    AV *av = newAV();
    SV *rv = newRV_noinc((SV*)av);
    SvREFCNT_inc(rv);
    seen[hash] = PTR2IV(av);

    for (int i = 0; i < array->Length(); i++) {
        Handle<Value> elementVal = array->Get( Integer::New( i ) );
        av_push(av, v82sv(elementVal, seen));
    }
    return rv;
}

SV *
V8Context::object2sv(Handle<Object> obj, SvMap& seen, int hash) {
    Local<Value> ptr = obj->GetHiddenValue(String::New("perlPtr"));

    if (!ptr.IsEmpty()) {
        SV* sv = (SV*)External::Unwrap(ptr);
        SvREFCNT_inc(sv);
        return sv;
    }

    if (enable_blessing && obj->Has(String::New("__perlPackage"))) {
        return object2blessed(obj, hash);
    }

    HV *hv = newHV();
    SV *rv = newRV_noinc((SV*)hv);
    SvREFCNT_inc(rv);
    seen[hash] = PTR2IV(hv);

    Local<Array> properties = obj->GetPropertyNames();
    for (int i = 0; i < properties->Length(); i++) {
        Local<Integer> propertyIndex = Integer::New( i );
        Local<String> propertyName = Local<String>::Cast( properties->Get( propertyIndex ) );
        String::Utf8Value propertyNameUTF8( propertyName );

        Local<Value> propertyValue = obj->Get( propertyName );
        hv_store(hv, *propertyNameUTF8, 0 - propertyNameUTF8.length(), v82sv(propertyValue, seen), 0);
    }
    return rv;
}

static void
my_gv_setsv(pTHX_ GV* const gv, SV* const sv){
    ENTER;
    SAVETMPS;

    sv_setsv_mg((SV*)gv, sv_2mortal(newRV_inc((sv))));

    FREETMPS;
    LEAVE;
}

#ifdef dVAR
    #define DVAR dVAR;
#endif

#define SETUP_V8_CALL(ARGS_OFFSET) \
    DVAR \
    dXSARGS; \
\
    bool die = false; \
\
    { \
        /* We have to do all this inside a block so that all the proper \
         * destuctors are called if we need to croak. If we just croak in the \
         * middle of the block, v8 will segfault at program exit. */ \
        TryCatch        try_catch; \
        HandleScope     scope; \
        V8ObjectData* data = sv_object_data((SV*)cv); \
        if (data->context) { \
        V8Context      *self = data->context; \
        Handle<Context> ctx  = self->context; \
        Context::Scope  context_scope(ctx); \
        Handle<Value>   argv[items - ARGS_OFFSET]; \
\
        for (I32 i = ARGS_OFFSET; i < items; i++) { \
            argv[i - ARGS_OFFSET] = self->sv2v8(ST(i)); \
        }

#define CONVERT_V8_RESULT() \
        if (try_catch.HasCaught()) { \
            set_perl_error(try_catch); \
            die = true; \
        } \
        else { \
            ST(0) = sv_2mortal(self->v82sv(result)); \
        } \
        } \
        else {\
            die = true; \
            sv_setpv(ERRSV, "Fatal error: V8 context is no more"); \
            sv_utf8_upgrade(ERRSV); \
        } \
    } \
\
    if (die) \
        croak(NULL); \
\
XSRETURN(1);

XS(v8closure) {
    SETUP_V8_CALL(0)
    Handle<Value> result = Handle<Function>::Cast(data->object)->Call(ctx->Global(), items, argv);
    CONVERT_V8_RESULT()
}

XS(v8method) {
    SETUP_V8_CALL(1)
    V8ObjectData* This = (V8ObjectData*)SvIV((SV*)SvRV(ST(0)));
    Handle<Value> result = Handle<Function>::Cast(data->object)->Call(This->object, items - 1, argv);
    CONVERT_V8_RESULT();
}

SV*
V8Context::function2sv(Handle<Function> fn, int hash) {
    CV          *code = newXS(NULL, v8closure, __FILE__);
    V8ObjectData *data = new V8ObjectData(fn->ToObject(), (SV*)code, this, hash);
    return newRV_noinc((SV*)code);
}

SV*
V8Context::object2blessed(Handle<Object> obj, int hash) {
    char package[128];

    snprintf(
        package,
        128, 
        "%s%s::N%d", 
        bless_prefix.c_str(),
        *String::AsciiValue(obj->Get(String::New("__perlPackage"))->ToString()),
        number
    );

    HV *stash = gv_stashpv(package, 0);

    if (!stash) {
        Local<Object> prototype = obj->GetPrototype()->ToObject();

        stash = gv_stashpv(package, GV_ADD);

        Local<Array> properties = prototype->GetPropertyNames();
        for (int i = 0; i < properties->Length(); i++) {
            Local<String> name = properties->Get(i)->ToString();
            Local<Value> property = prototype->Get(name);

            if (!property->IsFunction())
                continue;

            Local<Function> fn = Local<Function>::Cast(property);

            CV *code = newXS(NULL, v8method, __FILE__);
            V8ObjectData *data = new V8ObjectData(fn, (SV*)code, this, 0);

            GV* gv = (GV*)*hv_fetch(stash, *String::AsciiValue(name), name->Length(), TRUE);
            gv_init(gv, stash, *String::AsciiValue(name), name->Length(), GV_ADDMULTI); /* vivify */
            my_gv_setsv(aTHX_ gv, (SV*)code);
        }
    }

    SV* rv = newSV(0);
    SV* sv = newSVrv(rv, package);
    V8ObjectData *data = new V8ObjectData(obj, sv, this, hash);
    sv_setiv(sv, PTR2IV(data));

    return rv;
}

bool
V8Context::idle_notification() {
    /*
    HeapStatistics hs;
    V8::GetHeapStatistics(&hs);
    L(
        "%d %d %d\n", 
        hs.total_heap_size(), 
        hs.total_heap_size_executable(), 
        hs.used_heap_size()
    );
    */
    return V8::IdleNotification();
}

int
V8Context::adjust_amount_of_external_allocated_memory(int change_in_bytes) {
    return V8::AdjustAmountOfExternalAllocatedMemory(change_in_bytes);
}

void
V8Context::set_flags_from_string(char *str) {
    V8::SetFlagsFromString(str, strlen(str));
}
