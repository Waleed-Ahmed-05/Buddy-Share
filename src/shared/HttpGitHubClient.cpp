#include "shared/HttpGitHubClient.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <cctype>

#include "shared/CryptoProvider.h"

namespace buddyshare::shared {

namespace {

constexpr const char* kRateLimitMessage =
    "GitHub's request limit has been reached (0/60 remaining this hour). Wait for it to reset, "
    "or connect to a VPN for a new IP address.";

constexpr wchar_t kHost[] = L"api.github.com";
constexpr wchar_t kUserAgent[] = L"BuddyShare";

std::wstring to_wide(const std::string& text) { return std::wstring(text.begin(), text.end()); }

// Wraps a WinHTTP handle so it always gets closed when it goes out of scope -- including on
// every early return below -- instead of relying on a matching WinHttpCloseHandle() call being
// remembered on each path.
class ScopedHandle {
public:
    explicit ScopedHandle(HINTERNET handle) : handle_(handle) {}
    ~ScopedHandle() {
        if (handle_) WinHttpCloseHandle(handle_);
    }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    HINTERNET get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HINTERNET handle_;
};

// Extracts the value of a top-level "field": in a flat JSON object. Handles exactly the two
// shapes this client needs from GitHub's repo response -- a quoted string (default_branch)
// or a bare number (size) -- which is all extract_json_field is ever asked to parse.
std::string extract_json_field(const std::string& json, const std::string& field) {
    const std::string needle = "\"" + field + "\":";
    const std::size_t key_pos = json.find(needle);
    if (key_pos == std::string::npos) return "";

    std::size_t value_pos = key_pos + needle.size();
    while (value_pos < json.size() && json[value_pos] == ' ') ++value_pos;
    if (value_pos >= json.size()) return "";

    if (json[value_pos] == '"') {
        const std::size_t end_quote = json.find('"', value_pos + 1);
        if (end_quote == std::string::npos) return "";
        return json.substr(value_pos + 1, end_quote - value_pos - 1);
    }

    const std::size_t end_pos = json.find_first_of(",}", value_pos);
    return json.substr(value_pos,
                        (end_pos == std::string::npos ? json.size() : end_pos) - value_pos);
}

// True if body's first non-whitespace character is '[' -- the Contents API returns a JSON
// array for a directory listing and a JSON object for a single file (spec 004 Behavior steps
// 3/5/6 both go through the same endpoint).
bool looks_like_json_array(const std::string& body) {
    for (const char c : body) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        return c == '[';
    }
    return false;
}

// GitHub's file content comes back as base64 text with literal "\n" escape sequences (JSON
// strings can't contain a raw newline) breaking the base64 into 60-character lines. Those two
// literal characters (backslash, 'n') aren't valid base64 and must be dropped before decoding --
// left in, the 'n' would otherwise be misread as encoded data.
std::string strip_base64_newline_escapes(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n') {
            ++i;
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

// Parses a Contents API directory listing into (name, is_directory) pairs. Rather than a full
// JSON array parser, this pairs each "name" field with the next "type" field that follows it --
// GitHub always emits "type" once per entry, after "name", so this is enough to recover both
// without hand-rolling a general parser (see ChapterFile.cpp's fuller parser for why this
// project doesn't pull in a JSON library).
std::vector<ContentsEntry> parse_contents_directory_entries(const std::string& body) {
    std::vector<ContentsEntry> entries;
    std::size_t pos = 0;
    while (true) {
        const std::size_t name_pos = body.find("\"name\":", pos);
        if (name_pos == std::string::npos) break;
        const std::size_t next_name_pos = body.find("\"name\":", name_pos + 1);
        const std::size_t element_end =
            next_name_pos == std::string::npos ? body.size() : next_name_pos;
        const std::string element = body.substr(name_pos, element_end - name_pos);

        const std::string name = extract_json_field(element, "name");
        if (!name.empty()) {
            entries.push_back({name, extract_json_field(element, "type") == "dir"});
        }
        pos = element_end;
    }
    return entries;
}

// Plain result of one GET request against api.github.com -- everything both get_repo() and
// get_contents() need before they each interpret the status code/body their own way.
struct RawGetResponse {
    bool network_error{false};
    std::string error_message;  // only meaningful when network_error is true.
    DWORD status_code{0};
    std::string body;
};

// Does the actual WinHTTP round trip (open session -> connect -> build request -> send/receive
// -> read the whole body) that get_repo() and get_contents() both need. token is sent as an
// "Authorization: token ..." header when non-empty (get_repo only -- get_contents always passes
// an empty token, per this file's "no Authorization header, ever" rule for reader-role calls).
RawGetResponse perform_get(const std::wstring& request_path, const std::string& token) {
    RawGetResponse response;

    ScopedHandle session(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        response.network_error = true;
        response.error_message = "Failed to initialize the HTTP session.";
        return response;
    }

    ScopedHandle connection(WinHttpConnect(session.get(), kHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) {
        response.network_error = true;
        response.error_message = "Failed to connect to api.github.com.";
        return response;
    }

    ScopedHandle request(WinHttpOpenRequest(connection.get(), L"GET", request_path.c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE));
    if (!request) {
        response.network_error = true;
        response.error_message = "Failed to create the HTTP request.";
        return response;
    }

    // Authorization header carries the token only transiently, over HTTPS, for this one
    // request; it is never logged, stored, or reused (spec 001 security requirements).
    if (!token.empty()) {
        const std::wstring header = L"Authorization: token " + to_wide(token);
        WinHttpAddRequestHeaders(request.get(), header.c_str(), static_cast<DWORD>(-1),
                                  WINHTTP_ADDREQ_FLAG_ADD);
    }

    const BOOL sent = WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    const BOOL received = sent && WinHttpReceiveResponse(request.get(), nullptr);
    if (!sent || !received) {
        response.network_error = true;
        response.error_message =
            "Network error contacting GitHub (WinHTTP error " + std::to_string(GetLastError()) +
            ").";
        return response;
    }

    DWORD status_size = sizeof(response.status_code);
    WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &response.status_code, &status_size,
                        WINHTTP_NO_HEADER_INDEX);

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available) || available == 0) break;
        std::string chunk(available, '\0');
        DWORD read_bytes = 0;
        if (!WinHttpReadData(request.get(), chunk.data(), available, &read_bytes)) break;
        chunk.resize(read_bytes);
        response.body += chunk;
    }

    return response;
}

}  // namespace

RepoInfo HttpGitHubClient::get_repo(const std::string& owner, const std::string& repo,
                                     const std::string& token) {
    RepoInfo info;

    const std::wstring path = L"/repos/" + to_wide(owner) + L"/" + to_wide(repo);
    const RawGetResponse response = perform_get(path, token);
    if (response.network_error) {
        info.network_error = true;
        info.error_message = response.error_message;
        return info;
    }

    if (response.status_code == 200) {
        info.exists = true;
        info.default_branch = extract_json_field(response.body, "default_branch");
        // GitHub reports a freshly created, commit-less repo as size 0 (kilobytes); any
        // larger value means it has content, i.e. at least one commit.
        const std::string size_text = extract_json_field(response.body, "size");
        info.has_commits = !size_text.empty() && size_text != "0";
    } else if (response.status_code == 401) {
        info.unauthorized = true;
    } else if (response.status_code == 404) {
        info.ambiguous_not_found = true;
    } else if (response.status_code == 403) {
        info.rate_limited = true;
        info.network_error = true;
        info.error_message = kRateLimitMessage;
    } else {
        info.network_error = true;
        info.error_message =
            "GitHub API returned unexpected status " + std::to_string(response.status_code) + ".";
    }

    return info;
}

ContentsResult HttpGitHubClient::get_contents(const std::string& owner, const std::string& repo,
                                                const std::string& path) {
    ContentsResult result;

    // No Authorization header, ever -- see this method's declaration comment. Passing an empty
    // token to perform_get() is what makes that guarantee here.
    const std::wstring request_path =
        L"/repos/" + to_wide(owner) + L"/" + to_wide(repo) + L"/contents/" + to_wide(path);
    const RawGetResponse response = perform_get(request_path, "");
    if (response.network_error) {
        result.network_error = true;
        result.error_message = response.error_message;
        return result;
    }

    if (response.status_code == 200) {
        result.exists = true;
        if (looks_like_json_array(response.body)) {
            result.entries = parse_contents_directory_entries(response.body);
        } else {
            const std::string content_field =
                strip_base64_newline_escapes(extract_json_field(response.body, "content"));
            const Bytes decoded_bytes = CryptoProvider::base64_decode(content_field);
            result.decoded_content.assign(decoded_bytes.begin(), decoded_bytes.end());
        }
    } else if (response.status_code == 404) {
        result.not_found = true;
    } else if (response.status_code == 403) {
        result.rate_limited = true;
        result.network_error = true;
        result.error_message = kRateLimitMessage;
    } else {
        result.network_error = true;
        result.error_message =
            "GitHub API returned unexpected status " + std::to_string(response.status_code) + ".";
    }

    return result;
}

}  // namespace buddyshare::shared
