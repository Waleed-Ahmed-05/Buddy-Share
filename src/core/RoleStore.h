#ifndef BUDDYSHARE_CORE_ROLESTORE_H
#define BUDDYSHARE_CORE_ROLESTORE_H

#include <string>

#include "core/RoleManager.h"

namespace buddyshare::core {

// Real IRoleStore backed by the hidden role-lock file at %APPDATA%\BuddyShare\.role
// (spec 001, "Data Model / API / CLI Surface").
class RoleStore : public IRoleStore {
public:
    RoleStore();

    std::optional<std::string> read() const override;
    void write(const std::string& role_text) override;

private:
    std::string path_;
};

}  // namespace buddyshare::core

#endif  // BUDDYSHARE_CORE_ROLESTORE_H
