#include "../wrf_sdirk3_config.h"
#include <cstdlib>
#include <iostream>
#include <string>

extern "C" void wrf_sdirk3_set_config_int(const char*,int);

struct SavedEnv {
    const char* key;
    bool present;
    std::string value;
    explicit SavedEnv(const char* name) : key(name), present(std::getenv(name)!=nullptr),
        value(present ? std::getenv(name) : "") {}
    ~SavedEnv() { if (present) setenv(key,value.c_str(),1); else unsetenv(key); }
};

int main() {
    using namespace wrf::sdirk3;
    SavedEnv env2("WRF_SDIRK3_STAGE2_GMRES_RESTART"), env3("WRF_SDIRK3_STAGE3_GMRES_RESTART");
    int failures=0, checks=0;
    const auto check = [&](const SDIRK3Config& cfg, int expected, const char* path) {
        ++checks;
        const bool ok = cfg.stage2_gmres_restart==expected && cfg.stage3_gmres_restart==expected;
        std::cout << (ok ? "PASS " : "FAIL ") << path << " expected=" << expected
                  << " stage2=" << cfg.stage2_gmres_restart << " stage3=" << cfg.stage3_gmres_restart << '\n';
        if (!ok) ++failures;
    };
    check(SDIRK3Config{},0,"default-off");
    for (int requested : {-1,0,100,256,1000,1001}) {
        const int expected = std::max(0,std::min(requested,1000));
        const auto text = std::to_string(requested);
        SDIRK3Config namelist;
        namelist.load_from_namelist("&dynamics\n sdirk3_stage2_gmres_restart = "+text+
            ",\n sdirk3_stage3_gmres_restart = "+text+",\n/\n");
        check(namelist,expected,"namelist parsed");
        if (!namelist.validate()) ++failures;
        check(namelist,expected,"namelist validated");
        setenv(env2.key,text.c_str(),1); setenv(env3.key,text.c_str(),1);
        SDIRK3Config environment;
        environment.load_from_env();
        check(environment,expected,"env parsed");
        if (!environment.validate()) ++failures;
        check(environment,expected,"env validated");
        wrf_sdirk3_set_config_int("stage2_gmres_restart",requested);
        wrf_sdirk3_set_config_int("stage3_gmres_restart",requested);
        check(g_sdirk3_config,expected,"runtime setter");
        if (!g_sdirk3_config.validate()) ++failures;
        check(g_sdirk3_config,expected,"runtime validated");
    }
    std::cout << "Krylov budget config: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
