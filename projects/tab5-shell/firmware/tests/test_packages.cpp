#include "package_runtime.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace tab5_shell;
int main() {
    assert(sha256_hex("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    char dir[]="/tmp/tab5-packages-XXXXXX"; std::filesystem::path root(mkdtemp(dir));
    Shell shell({(root/"home").string(),root.string(),true}); std::string error,output;
    assert(shell.prepare(error)); Packages packages(shell);
    auto emit=[&](std::string_view text){output+=text;};
    std::string script="print('Hello, ' .. (arg[1] or 'Tab5') .. '!')\n";
    std::string manifest="TAB5SHELL-PKG-1\nname hello\nversion 1.0.0\nentry hello.lua\nsha256 "+sha256_hex(script)+"\n";
    PackageMetadata metadata;
    assert(parse_package(manifest,metadata,error));
    assert(!parse_package(manifest+"name duplicate\n",metadata,error));
    auto unsafe=manifest; unsafe.replace(unsafe.find("hello.lua"),9,"../secret");
    assert(!parse_package(unsafe,metadata,error));
    assert(shell.write_file("hello.pkg",manifest,error)); assert(shell.write_file("hello.lua",script,error));
    assert(packages.command({"install","/home/hello.pkg"},emit)==0);
    output.clear(); assert(packages.run("hello",{"world"},emit)==0 && output=="Hello, world!\n");
    output.clear(); assert(packages.command({"list"},emit)==0 && output.find("hello 1.0.0 verified")!=std::string::npos);
    assert(packages.command({"install","/home/hello.pkg"},emit)==1);
    assert(shell.write_file("/home/.packages/hello.lua","print('tampered')",error));
    output.clear(); assert(packages.run("hello",{},emit)==1 && output.find("integrity")!=std::string::npos);
    assert(packages.command({"remove","hello"},emit)==0); assert(packages.run("hello",{},emit)==127);
    assert(packages.command({"remove","../escape"},emit)==2);
    assert(packages.command({"install","/sd/PRIVATE/hello.pkg"},emit)==1);
    ScriptStats stats; output.clear();
    assert(run_lua(script,{"measured"},emit,stats)==0);
    std::cout << "Lua hello peak allocator bytes=" << stats.peak_bytes << " instructions=" << stats.instructions << "\n";
    assert(stats.peak_bytes<kLuaMemoryLimit);
    assert(run_lua("assert(io==nil and os==nil and package==nil and debug==nil and load==nil and dofile==nil)",{},emit,stats)==0);
    assert(run_lua("while true do end",{},emit,stats)==1 && stats.instructions>=100000);
    assert(run_lua("return string.rep('x',1024*1024)",{},emit,stats)==1 && stats.peak_bytes<=kLuaMemoryLimit);
    assert(run_lua("local f; f=function() return string.gsub('x','.',f) end; f()",{},emit,stats)==1);
    output.clear(); assert(run_lua("while true do print(string.rep('a',1000)) end",{},emit,stats)==1);
    assert(output.size()<kMaximumOutputBytes+256);
    output.clear(); assert(run_lua("error({})",{},emit,stats)==1);
    output.clear(); assert(run_lua("print('\\27[2J')",{},emit,stats)==0 && output.find('\x1b')==std::string::npos);
    assert(run_lua(std::string("\x1bLua",4),{},emit,stats)==1); // bytecode disabled
    std::filesystem::remove_all(root);
    std::cout << "PASS packages real install/list/run/remove/integrity + bounded Lua ASan+UBSan\n";
}
