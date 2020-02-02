#include <cstdio>
#include <cassert>
#include <vector>
#include <string>
#include "cd_preproc.h"

namespace {


    void log_func(void* log_data, cd_pp_loglevel_t level, const char* msg)
    {
        assert(log_data == (void*)42);
        const char * kind = nullptr;
        switch(level) {
        case CD_PP_LOGLEVEL_DEBUG:      kind = "[debug]"; break;
        case CD_PP_LOGLEVEL_WARNING:    kind = "[warning]"; break;
        case CD_PP_LOGLEVEL_ERROR:      kind = "[error]"; break;
        default: assert(false); return;
        }
        fprintf(stderr, "%s %s", kind, msg);
    }

    cd_pp_state_t new_state()
    {
        cd_pp_state_t pp_state{0};
        pp_state.log_func = log_func;
        pp_state.log_data = (void*)42;
        return pp_state;
    }

}


int main(int argc, const char * argv[]) {
    
    {   // Check interning
        cd_pp_state_t pp_state = new_state();

        std::vector<const char*> strings_ptr;
        std::vector<std::string> strings;
        for(size_t i=0; i<100; i++) {
            std::string t = "abc" + std::to_string(i);
            strings_ptr.push_back(cd_pp_str_intern(&pp_state, t.c_str(), t.c_str()+t.length()));
            strings.emplace_back(std::move(t));
        }
        for(size_t i=0; i<100; i++) {
            std::string t = "abc" + std::to_string(i) + "abc";
            strings_ptr.push_back(cd_pp_str_intern(&pp_state, t.c_str(), t.c_str()+t.length()));
            strings.emplace_back(std::move(t));
        }
        assert(pp_state.str_map.fill == strings.size());
        for(size_t i=1, n=strings_ptr.size(); i<n; i++) {
            assert(strings_ptr[i-1] != strings_ptr[i]);
        }
        for(size_t i=0; i<strings.size(); i++) {
            auto & t = strings[i];
            auto * p = cd_pp_str_intern(&pp_state, t.c_str(), t.c_str()+t.length());
            assert(p == strings_ptr[i]);
        }
        assert(pp_state.str_map.fill == strings.size());
        cd_pp_state_free(&pp_state);
    }

    fprintf(stdout, "woohoo\n");
    return 0;
}
