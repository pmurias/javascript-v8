#ifndef _V8Context_h_
#define _V8Context_h_

#include <EXTERN.h>
#include <perl.h>
#undef New
#undef Null
#include <v8.h>

using namespace v8;

class V8Context {
    public:
        V8Context();
        ~V8Context();

        void bind(const char*, SV*);
        SV* eval(const char*);

        Handle<Value> sv2v8(SV*);
        SV*           v82sv(Handle<Value>);

        Persistent<Context> context;
    private:
        Handle<Value>    rv2v8(SV*);
        Handle<Array>    av2array(AV*);
        Handle<Object>   hv2object(HV*);
        Handle<Function> cv2function(CV*);

        SV* array2sv(Handle<Array>);
        SV* object2sv(Handle<Object>);
        SV* function2sv(Handle<Function>);
};

#endif

