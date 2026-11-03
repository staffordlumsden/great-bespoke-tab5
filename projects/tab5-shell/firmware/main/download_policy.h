#pragma once
#include <algorithm>
#include <string_view>
namespace tab5_shell {
inline bool https_url_allowed(std::string_view url) {
    if (url.size()>2048 || !url.starts_with("https://")) return false;
    if (!std::all_of(url.begin(),url.end(),[](unsigned char c){return c>32&&c<127&&c!='\\'&&c!='#';})) return false;
    auto authority=url.substr(8); authority=authority.substr(0,authority.find_first_of("/?"));
    if (authority.ends_with(":443")) authority.remove_suffix(4);
    if (authority.empty() || authority.front()=='.' || authority.back()=='.') return false;
    return std::all_of(authority.begin(),authority.end(),[](unsigned char c) {
        return (c>='A'&&c<='Z') || (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='-' || c=='.';
    });
}
}
