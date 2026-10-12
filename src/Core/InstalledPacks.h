#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace OnlinePacks { struct Entry; }
namespace InstalledPacks {
struct Record {
    std::string name, author, repository, filename, version, lastUpdate, downloadUrl;
    std::vector<std::string> aliases;
};
std::vector<Record> Load(const std::filesystem::path& manifest);
bool IsValid(const std::filesystem::path& manifest);
bool Save(const std::filesystem::path& manifest, const std::vector<Record>& records);
Record FromCatalog(const OnlinePacks::Entry& entry);
// Return only an unambiguous catalog match. Never guess which user file to replace.
const OnlinePacks::Entry* Match(const Record& record, const std::vector<OnlinePacks::Entry>& entries);
bool HasNewerRelease(const Record& installed, const OnlinePacks::Entry& available);
}
