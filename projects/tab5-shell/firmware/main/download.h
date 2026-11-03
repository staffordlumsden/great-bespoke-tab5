#pragma once
#include "download_policy.h"
#include "shell_core.h"
namespace tab5_shell {
bool https_download(Shell& shell,std::string_view url,std::string_view path,std::string& error);
bool ensure_shell_wifi();
}
