#ifndef _V8Context_h_
#define _V8Context_h_

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#undef New
#undef Null

#include <vector>
#include <map>
#include <string>

#include <v8.h>

#include <string>

using namespace v8;
using namespace std;

typedef map<string, Persistent<Object> > ObjectMap;
typedef map<int, long> SvMap;
typedef map<int, Handle<Value> > HandleMap;

class V8Context {
    public:
        V8Context(int time_limit = 0, const char* flags = NULL, bool enable_blessing = false, const char* bless_prefix = NULL);
        ~V8Context();

        void bind(const char*, SV*);
        SV* eval(SV* source, SV* origin = NULL);
        bool idle_notification();
        int adjust_amount_of_external_allocated_memory(int bytes);
        void set_flags_from_string(char *str);

        Handle<Value> sv2v8(SV*);
        SV*           v82sv(Handle<Value>);

        Persistent<Context> context;
        SvMap seenv8;
        vector<void*> objects;
        vector<void*> closures;

    private:
        Handle<Value>    sv2v8(SV*, HandleMap& seen);
        SV*              v82sv(Handle<Value>, SvMap& seen);

        Handle<Value>    rv2v8(SV*, HandleMap& seen);
        Handle<Array>    av2array(AV*, HandleMap& seen, long ptr);
        Handle<Object>   hv2object(HV*, HandleMap& seen, long ptr);
        Handle<Function> cv2function(CV*);
        Handle<String>   sv2v8str(SV* sv);
        Handle<Object>   blessed2object(SV *sv);

        SV* array2sv(Handle<Array>, SvMap& seen, int hash);
        SV* object2sv(Handle<Object>, SvMap& seen, int hash);
        SV* object2blessed(Handle<Object>, SvMap& seen, int hash);
        SV* function2sv(Handle<Function>);

        void fill_prototype(Handle<Object> prototype, HV* stash);
        Handle<Object> get_prototype(SV* sv);

        ObjectMap prototypes;

        const string get_package_name(const string& package);

        int time_limit_;
        string bless_prefix;
        bool enable_blessing;
        static int number;
};


#endif

