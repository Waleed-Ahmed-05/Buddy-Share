#ifndef BUDDYSHARE_READER_DPAPIPRIVATEKEYSTORE_H
#define BUDDYSHARE_READER_DPAPIPRIVATEKEYSTORE_H

#include "reader/ReaderViewerService.h"

namespace buddyshare::reader {

// Real IPrivateKeyStore: stores each username's private key PEM at
// %APPDATA%\BuddyShare\keys\<username>_private.pem, encrypted at rest via Windows DPAPI
// (CryptProtectData/CryptUnprotectData) -- see specs/004-reader-viewer.md, Data Model / API /
// CLI Surface. Never transmits the key anywhere; DPAPI ties the ciphertext to this Windows
// user account.
class DpapiPrivateKeyStore : public IPrivateKeyStore {
public:
    std::optional<std::string> read_private_key_pem(const std::string& username) const override;
    void write_private_key_pem(const std::string& username,
                                const std::string& private_key_pem) override;
};

}  // namespace buddyshare::reader

#endif  // BUDDYSHARE_READER_DPAPIPRIVATEKEYSTORE_H
