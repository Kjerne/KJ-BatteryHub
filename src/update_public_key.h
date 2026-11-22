#pragma once
// Public keys allowed to sign this fork's releases (see update_signature.h). Written by
// scripts/new-release-key.ps1; the matching private key stays on the maintainer's PC.
// More than one key lets a new key be added before an old one is retired.
// Empty means no key yet: official builds then refuse to compile, and updates cannot be installed.
namespace updates {
inline constexpr const char* ReleaseKeys[] = {
    "RUNTMSAAAAALBItclwP+xy5rZUcFdyOgPuq7gcfeR6+frK4GkzeVmE9YN+/Eghr6qOm57vnMibXCHZOUX5G8ZGIX3e4XEBoI",
};
}
