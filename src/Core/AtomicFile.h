#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#ifdef _WIN32
#include <Windows.h>
#endif
namespace AtomicFile {
// Write beside the destination, flush/close, then replace atomically on its filesystem.
inline bool Write(const std::filesystem::path& path,const std::string& text) {
    std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);
    if(ec)return false;
    auto temp=path;temp+=".tmp";
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)return false;
     out.write(text.data(),static_cast<std::streamsize>(text.size()));out.flush();if(!out)return false;out.close();if(!out)return false;}
#ifdef _WIN32
    return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::filesystem::rename(temp,path,ec);return !ec;
#endif
}
}
