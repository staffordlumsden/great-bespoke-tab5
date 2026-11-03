#include "package_runtime.h"
#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "mbedtls/sha256.h"
#elif defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif
namespace tab5_shell {
namespace {
bool safe_name(std::string_view name) {
    return !name.empty() && name.size()<=32 && name.front()>='a' && name.front()<='z' &&
        std::all_of(name.begin(),name.end(),[](unsigned char c) {
            return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='-' || c=='_';
        });
}
bool reserved(std::string_view name) {
    for (const auto* command : {"help","clear","pwd","cd","ls","mkdir","cat","echo","touch","rm","df","free","exit","nano","glow","ssh","pkg","wifi","download"})
        if (name==command) return true;
    return false;
}
std::string base_path(std::string_view name) { return "/home/.packages/"+std::string(name); }
}
std::string sha256_hex(std::string_view data) {
    unsigned char digest[32]{};
#ifdef ESP_PLATFORM
    if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(data.data()),data.size(),digest,0)) return {};
#elif defined(__APPLE__)
    CC_SHA256(data.data(),static_cast<CC_LONG>(data.size()),digest);
#else
    SHA256(reinterpret_cast<const unsigned char*>(data.data()),data.size(),digest);
#endif
    const char* hex="0123456789abcdef";
    std::string value;
    for (unsigned char c:digest) { value+=hex[c>>4]; value+=hex[c&15]; }
    return value;
}
bool parse_package(std::string_view text,PackageMetadata& metadata,std::string& error) {
    metadata={};
    if (text.size()>2048 || !text.starts_with("TAB5SHELL-PKG-1\n")) { error="invalid package format"; return false; }
    text.remove_prefix(16);
    while (!text.empty()) {
        auto end=text.find('\n'); auto line=text.substr(0,end);
        auto p=parse_command(line);
        if (!line.empty()) {
            if (!p.ok || p.arguments.size()!=2) { error="invalid metadata field"; return false; }
            const auto& key=p.arguments[0]; const auto& value=p.arguments[1];
            std::string* target=key=="name"?&metadata.name:key=="version"?&metadata.version:
                key=="entry"?&metadata.entry:key=="sha256"?&metadata.sha256:nullptr;
            if (!target || !target->empty() || value.empty()) { error="unknown/duplicate package field"; return false; }
            *target=value;
        }
        if (end==std::string_view::npos) break;
        text.remove_prefix(end+1);
    }
    if (!safe_name(metadata.name) || reserved(metadata.name) || metadata.entry!=metadata.name+".lua" ||
        metadata.version.empty() || metadata.version.size()>31 ||
        !std::all_of(metadata.version.begin(),metadata.version.end(),[](unsigned char c) {
            return (c>='0'&&c<='9') || (c>='a'&&c<='z') || c=='.' || c=='-';
        }) || metadata.sha256.size()!=64 ||
        !std::all_of(metadata.sha256.begin(),metadata.sha256.end(),[](unsigned char c) {
            return (c>='0'&&c<='9') || (c>='a'&&c<='f');
        })) { error="unsafe/missing package metadata"; return false; }
    return true;
}
bool Packages::load(std::string_view name,PackageMetadata& metadata,std::string& source,std::string& error) {
    if (!safe_name(name)) { error="invalid package name"; return false; }
    std::string manifest;
    if (!shell_.read_file(base_path(name)+".pkg",manifest,error,2048) ||
        !parse_package(manifest,metadata,error) || metadata.name!=name ||
        !shell_.read_file(base_path(name)+".lua",source,error,16384)) return false;
    if (sha256_hex(source)!=metadata.sha256) { error="integrity check failed"; return false; }
    return true;
}
int Packages::run(std::string_view name,const std::vector<std::string>& args,const Shell::Output& output) {
    if (!safe_name(name) || reserved(name)) return 127;
    std::string physical,error;
    if (!shell_.file_path(base_path(name)+".pkg",false,physical,error)) return 127;
    struct stat info{}; if (stat(physical.c_str(),&info)) return 127;
    PackageMetadata metadata; std::string source;
    if (!load(name,metadata,source,error)) { output("pkg: "+error+"\n"); return 1; }
    ScriptStats stats;
    return run_lua(source,args,output,stats);
}
int Packages::command(const std::vector<std::string>& args,const Shell::Output& output) {
    std::string error,physical;
    if (args.size()==1 && args[0]=="list") {
        if (!shell_.file_path("/home/.packages",false,physical,error)) { output(error+"\n"); return 1; }
        DIR* dir=opendir(physical.c_str());
        if (!dir) { if (errno==ENOENT) { output("No installed packages\n"); return 0; } output("Cannot read packages\n"); return 1; }
        std::vector<std::string> names;
        while (auto* entry=readdir(dir)) {
            std::string name=entry->d_name;
            if (name.ends_with(".pkg")) { names.push_back(name.substr(0,name.size()-4)); if (names.size()>32) break; }
        }
        closedir(dir);
        if (names.size()>32) { output("Package limit exceeded\n"); return 1; }
        std::sort(names.begin(),names.end());
        int status=0;
        for (const auto& name:names) {
            PackageMetadata metadata; std::string source;
            if (load(name,metadata,source,error)) output(name+" "+metadata.version+" verified\n");
            else { output("Invalid installed package (inspect .packages)\n"); status=1; }
        }
        if (names.empty()) output("No installed packages\n");
        return status;
    }
    if (args.size()==2 && args[0]=="install") {
        auto path=normalize_path(shell_.cwd(),args[1]);
        std::string manifest,source;
        PackageMetadata metadata;
        if (!path.ok || !shell_.read_file(path.logical,manifest,error,2048) || !parse_package(manifest,metadata,error)) {
            output("pkg: "+(path.ok?error:path.error)+"\n"); return 1;
        }
        auto source_path=path.logical.substr(0,path.logical.rfind('/')+1)+metadata.entry;
        if (!shell_.read_file(source_path,source,error,16384) || sha256_hex(source)!=metadata.sha256) {
            output("pkg: source missing or SHA-256 mismatch\n"); return 1;
        }
        if (!shell_.file_path("/home/.packages",true,physical,error)) { output(error+"\n"); return 1; }
        if (mkdir(physical.c_str(),0700) && errno!=EEXIST) { output("pkg: cannot create package directory\n"); return 1; }
        struct stat info{};
        if (stat(physical.c_str(),&info) || !S_ISDIR(info.st_mode)) { output("pkg: package path is not directory\n"); return 1; }
        DIR* dir=opendir(physical.c_str()); if (!dir) return 1;
        std::size_t count=0;
        while (auto* entry=readdir(dir)) if (std::string_view(entry->d_name).ends_with(".pkg")) ++count;
        closedir(dir);
        if (count>=32) { output("pkg: 32 package limit\n"); return 1; }
        for (auto suffix : {".pkg",".lua"}) {
            if (!shell_.file_path(base_path(metadata.name)+suffix,true,physical,error)) { output(error+"\n"); return 1; }
            if (!stat(physical.c_str(),&info) || errno!=ENOENT) { output("pkg: already installed/partial; remove first\n"); return 1; }
        }
        // Publish metadata last. Interrupted install never executes a payload
        // without a complete, validated manifest and matching digest.
        if (!shell_.write_file(base_path(metadata.name)+".lua",source,error) ||
            !shell_.write_file(base_path(metadata.name)+".pkg",manifest,error)) {
            output("pkg: "+error+" (remove partial package before retry)\n"); return 1;
        }
        output("Installed "+metadata.name+" "+metadata.version+"\n"); return 0;
    }
    if (args.size()==2 && args[0]=="remove" && safe_name(args[1]) && !reserved(args[1])) {
        // Disable first, then remove only the named package's known files.
        for (auto suffix:{".pkg",".lua",".pkg.tab5-new",".pkg.tab5-prev",".lua.tab5-new",".lua.tab5-prev"}) {
            if (!shell_.file_path(base_path(args[1])+suffix,true,physical,error) ||
                (unlink(physical.c_str()) && errno!=ENOENT)) { output("pkg: remove failed; "+error+"\n"); return 1; }
        }
        output("Removed "+args[1]+"\n"); return 0;
    }
    output("usage: pkg install MANIFEST.pkg | pkg list | pkg remove NAME\n"); return 2;
}
} // namespace tab5_shell
