#include "package_runtime.h"
#include <algorithm>
#include <cstdlib>
#include <climits>
// Newlib's C++ limits header can omit C99 long-long macros, while the
// corresponding C translation units expose them. Preserve Lua's 64-bit ABI.
#ifndef LLONG_MAX
#define LLONG_MAX __LONG_LONG_MAX__
#define LLONG_MIN (-LLONG_MAX - 1LL)
#define ULLONG_MAX (2ULL * LLONG_MAX + 1ULL)
#endif
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
namespace tab5_shell {
namespace {
struct Context {
    std::size_t used=0, peak=0, output_bytes=0;
    unsigned instructions=0;
    const Shell::Output* output;
};
void* allocate(void* ud, void* pointer, std::size_t old_size, std::size_t size) {
    auto& c=*static_cast<Context*>(ud);
    if (!pointer) old_size=0;
    if (!size) {
#ifdef ESP_PLATFORM
        heap_caps_free(pointer);
#else
        std::free(pointer);
#endif
        c.used-=old_size; return nullptr;
    }
    if (size>kLuaMemoryLimit || c.used-old_size>kLuaMemoryLimit-size) return nullptr;
#ifdef ESP_PLATFORM
    void* value=heap_caps_realloc(pointer,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    void* value=std::realloc(pointer,size);
#endif
    if (value) { c.used=c.used-old_size+size; c.peak=std::max(c.peak,c.used); }
    return value;
}
Context& context(lua_State* l) { return **static_cast<Context**>(lua_getextraspace(l)); }
void instruction_hook(lua_State* l,lua_Debug*) {
    auto& c=context(l); c.instructions+=1000;
    if (c.instructions>=100000) luaL_error(l,"instruction budget exceeded");
}
int print(lua_State* l) {
    auto& c=context(l);
    if (c.output_bytes>=kMaximumOutputBytes) return luaL_error(l,"output budget exceeded");
    const int count=lua_gettop(l);
    for (int i=1;i<=count;++i) {
        std::size_t size=0;
        const char* text=luaL_tolstring(l,i,&size);
        if (size>kMaximumOutputBytes || c.output_bytes+size+2>kMaximumOutputBytes)
            return luaL_error(l,"output budget exceeded");
        if (i>1) { (*c.output)("\t"); ++c.output_bytes; }
        // Scripts cannot inject terminal escape/control sequences.
        for (std::size_t j=0;j<size;++j) {
            const unsigned char ch=text[j];
            char safe=(ch=='\n' || ch=='\t' || (ch>=32 && ch<=126))?static_cast<char>(ch):'?';
            (*c.output)(std::string_view(&safe,1));
        }
        c.output_bytes+=size; lua_pop(l,1);
    }
    (*c.output)("\n"); ++c.output_bytes; return 0;
}
// Library initialization is protected too: no Lua allocation error may unwind
// across C++ stack objects or trigger Lua's panic/abort path.
int initialize(lua_State* l) {
    luaL_requiref(l,"_G",luaopen_base,1); lua_pop(l,1);
    for (const char* name : {"dofile","loadfile","load","collectgarbage"}) {
        lua_pushnil(l); lua_setglobal(l,name);
    }
    luaL_requiref(l,LUA_STRLIBNAME,luaopen_string,1); lua_pop(l,1);
    luaL_requiref(l,LUA_TABLIBNAME,luaopen_table,1); lua_pop(l,1);
    luaL_requiref(l,LUA_MATHLIBNAME,luaopen_math,1); lua_pop(l,1);
    luaL_requiref(l,LUA_UTF8LIBNAME,luaopen_utf8,1); lua_pop(l,1);
    lua_pushcfunction(l,print); lua_setglobal(l,"print");
    return 0;
}
}
int run_lua(std::string_view source,const std::vector<std::string>& args,
            const Shell::Output& output,ScriptStats& stats) {
    if (source.size()>16384 || !output) return 2;
#ifdef ESP_PLATFORM
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<kLuaMemoryLimit+128*1024) {
        output("Lua unavailable: insufficient free PSRAM reserve\n"); return 1;
    }
#endif
    Context c; c.output=&output;
    lua_State* l=lua_newstate(allocate,&c);
    if (!l) { output("Lua allocation failed\n"); return 1; }
    *static_cast<Context**>(lua_getextraspace(l))=&c;
    lua_sethook(l,instruction_hook,LUA_MASKCOUNT,1000);
    lua_pushcfunction(l,initialize);
    int result=lua_pcall(l,0,0,0);
    // At most 512 bytes of command args; initialization leaves ample room in
    // the hard allocator limit. Build arg table inside a protected C call.
    if (result==LUA_OK) {
        lua_pushlightuserdata(l,const_cast<std::vector<std::string>*>(&args));
        lua_pushcclosure(l,[](lua_State* state)->int {
            const auto& values=*static_cast<std::vector<std::string>*>(lua_touserdata(state,lua_upvalueindex(1)));
            lua_newtable(state);
            for (std::size_t i=0;i<values.size();++i) {
                lua_pushlstring(state,values[i].data(),values[i].size()); lua_rawseti(state,-2,i+1);
            }
            lua_setglobal(state,"arg"); return 0;
        },1);
        result=lua_pcall(l,0,0,0);
    }
    if (result==LUA_OK) result=luaL_loadbufferx(l,source.data(),source.size(),"installed-package","t");
    if (result==LUA_OK) result=lua_pcall(l,0,0,0);
    if (result!=LUA_OK) {
        output("Lua error: ");
        // Bounded sanitization of an error value, including user-thrown errors.
        std::size_t size=0; const char* error=lua_type(l,-1)==LUA_TSTRING?lua_tolstring(l,-1,&size):nullptr;
        if (error) {
            for (std::size_t i=0;i<std::min<std::size_t>(size,160);++i) {
                unsigned char ch=error[i]; char safe=ch>=32&&ch<=126?static_cast<char>(ch):'?';
                output(std::string_view(&safe,1));
            }
        } else output("non-string error");
        output("\n");
    }
    stats={c.peak,c.instructions}; lua_close(l);
    return result==LUA_OK?0:1;
}
} // namespace tab5_shell
