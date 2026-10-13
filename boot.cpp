#include "boot.hpp"

namespace {
constexpr BootEntry entries[] = {{ L"GOP Demo", L"\\EFI\\TOOLS\\GOP.EFI" }};
constexpr UINTN max_path_chars = 259;
constexpr UINTN max_source_bytes = 1024;
constexpr UINTN max_source_nodes = 64;

bool is_error(EFI_STATUS status) { return (status >> 63) != 0; }

UINTN node_length(const UINT8 *node)
{
    return node[2] | (static_cast<UINTN>(node[3]) << 8);
}

// Borrowed firmware paths have no separate byte count. Limit every node read
// to the accepted protocol extent and reject incomplete or multiple instances.
EFI_STATUS source_prefix(const UINT8 *path, UINTN *size)
{
    UINTN offset = 0;
    for (UINTN nodes = 0; nodes < max_source_nodes; ++nodes) {
        if (offset > max_source_bytes - 4) return EFI_INVALID_PARAMETER;
        const UINT8 *node = path + offset;
        const UINTN length = node_length(node);
        if (length < 4 || length > max_source_bytes - offset) return EFI_INVALID_PARAMETER;
        if (node[0] == END_DEVICE_PATH_TYPE) {
            if (node[1] != END_ENTIRE_DEVICE_PATH_SUBTYPE || length != 4) return EFI_INVALID_PARAMETER;
            *size = offset;
            return EFI_SUCCESS;
        }
        offset += length;
    }
    return EFI_INVALID_PARAMETER;
}

// A small ASCII canonical form uses the existing device-path buffer. Parent
// File Path strings can span nodes; normalization consumes them as one string.
struct PathNormalizer {
    UINT8 *output;
    UINTN size = 1;
    UINTN segment = 1;

    explicit PathNormalizer(UINT8 *bytes) : output(bytes) { output[0] = '\\'; }

    bool valid_segment() const
    {
        const UINTN count = size - segment;
        return !(count == 1 && output[segment] == '.') &&
            !(count == 2 && output[segment] == '.' && output[segment + 1] == '.');
    }
    bool append(UINT16 ch)
    {
        if (ch < 32 || ch > 126 || ch == '/') return false;
        if (ch == '\\') {
            if (!valid_segment()) return false;
            if (output[size - 1] == '\\') return true;
        }
        if (size >= max_path_chars) return false;
        output[size++] = static_cast<UINT8>(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch);
        if (ch == '\\') segment = size;
        return true;
    }
    bool finish()
    {
        if (size == 1 || !valid_segment()) return false;
        output[size] = 0;
        return true;
    }
};

EFI_STATUS normalize_target(const CHAR16 *path, UINT8 *output, UINTN *chars)
{
    if (!path || path[0] != L'\\') return EFI_INVALID_PARAMETER;
    PathNormalizer normalized(output);
    for (UINTN index = 0; index <= max_path_chars; ++index) {
        if (path[index] == 0) {
            if (!normalized.finish()) return EFI_INVALID_PARAMETER;
            *chars = index;
            return EFI_SUCCESS;
        }
        if (index == max_path_chars || !normalized.append(static_cast<UINT16>(path[index])))
            return EFI_INVALID_PARAMETER;
    }
    return EFI_INVALID_PARAMETER;
}

EFI_STATUS normalize_parent(const EFI_DEVICE_PATH_PROTOCOL *path, UINT8 *output)
{
    const auto *bytes = reinterpret_cast<const UINT8 *>(path);
    PathNormalizer normalized(output);
    UINTN offset = 0;
    bool file_found = false;
    for (UINTN nodes = 0; nodes < max_source_nodes; ++nodes) {
        if (offset > max_source_bytes - 4) return EFI_INVALID_PARAMETER;
        const UINT8 *node = bytes + offset;
        const UINTN length = node_length(node);
        if (length < 4 || length > max_source_bytes - offset) return EFI_INVALID_PARAMETER;
        if (node[0] == END_DEVICE_PATH_TYPE) {
            if (node[1] != END_ENTIRE_DEVICE_PATH_SUBTYPE || length != 4 || !file_found || !normalized.finish())
                return EFI_INVALID_PARAMETER;
            return EFI_SUCCESS;
        }
        if (node[0] != MEDIA_DEVICE_PATH || node[1] != MEDIA_FILEPATH_DP || length < 6 || (length & 1))
            return EFI_INVALID_PARAMETER;
        file_found = true;
        for (UINTN index = 4; index < length; index += 2) {
            const UINT16 ch = static_cast<UINT16>(node[index] | (static_cast<UINT16>(node[index + 1]) << 8));
            if (index == length - 2) {
                if (ch != 0) return EFI_INVALID_PARAMETER;
            } else if (ch == 0 || !normalized.append(ch)) {
                return EFI_INVALID_PARAMETER;
            }
        }
        offset += length;
    }
    return EFI_INVALID_PARAMETER;
}

void write_header(UINT8 *node, UINT8 type, UINT8 subtype, UINTN length)
{
    node[0] = type;
    node[1] = subtype;
    node[2] = static_cast<UINT8>(length);
    node[3] = static_cast<UINT8>(length >> 8);
}

void record_cleanup(BootResult &result, EFI_STATUS status)
{
    if (is_error(status) && !is_error(result.cleanup_status)) result.cleanup_status = status;
}
}

const BootEntry *boot_entries(UINTN *count)
{
    if (count) *count = sizeof(entries) / sizeof(entries[0]);
    return entries;
}

BootLoader::BootLoader(EFI_HANDLE parent, EFI_SYSTEM_TABLE *table)
    : parent_(parent), table_(table), pending_image_(nullptr), watchdog_disabled_(true)
{
}

bool BootLoader::has_pending_image() const { return pending_image_ != nullptr; }
bool BootLoader::watchdog_disabled() const { return watchdog_disabled_; }

EFI_STATUS BootLoader::prepare_path(const BootEntry &entry)
{
    UINTN chars = 0;
    EFI_STATUS status = normalize_target(entry.path, device_path_, &chars);
    if (is_error(status)) return status;

    EFI_BOOT_SERVICES *boot = table_->BootServices;
    EFI_GUID guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    void *interface = nullptr;
    status = boot->OpenProtocol(parent_, &guid, &interface, parent_, nullptr, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (is_error(status)) return status;
    const auto *parent = static_cast<const EFI_LOADED_IMAGE_PROTOCOL *>(interface);
    if (!parent || !parent->DeviceHandle || !parent->FilePath) return EFI_UNSUPPORTED;

    guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
    interface = nullptr;
    status = boot->OpenProtocol(parent->DeviceHandle, &guid, &interface, parent_, nullptr, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (is_error(status)) return status;
    if (!interface) return EFI_UNSUPPORTED;
    const auto *source = static_cast<const UINT8 *>(interface);
    UINTN prefix = 0;
    status = source_prefix(source, &prefix);
    if (is_error(status)) return status;
    status = normalize_parent(parent->FilePath, device_path_ + max_path_chars + 1);
    if (is_error(status)) return status;
    const UINT8 *parent_name = device_path_ + max_path_chars + 1;
    UINTN index = 0;
    while (device_path_[index] && device_path_[index] == parent_name[index]) ++index;
    if (device_path_[index] == parent_name[index]) return EFI_ACCESS_DENIED;

    const UINTN file_size = 4 + 2 * (chars + 1);
    if (prefix > sizeof(device_path_) - 4 || file_size > sizeof(device_path_) - 4 - prefix)
        return EFI_INVALID_PARAMETER;
    for (UINTN byte = 0; byte < prefix; ++byte) device_path_[byte] = source[byte];
    write_header(device_path_ + prefix, MEDIA_DEVICE_PATH, MEDIA_FILEPATH_DP, file_size);
    for (UINTN character = 0; character <= chars; ++character) {
        const UINT16 value = static_cast<UINT16>(entry.path[character]);
        device_path_[prefix + 4 + 2 * character] = static_cast<UINT8>(value);
        device_path_[prefix + 5 + 2 * character] = static_cast<UINT8>(value >> 8);
    }
    write_header(device_path_ + prefix + file_size, END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, 4);
    return EFI_SUCCESS;
}

EFI_STATUS BootLoader::cleanup_image()
{
    if (!pending_image_) return EFI_SUCCESS;
    if (!table_ || !table_->BootServices || !table_->BootServices->OpenProtocol || !table_->BootServices->UnloadImage)
        return EFI_UNSUPPORTED;
    EFI_BOOT_SERVICES *boot = table_->BootServices;
    EFI_GUID guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    void *interface = nullptr;
    const EFI_STATUS present = boot->OpenProtocol(pending_image_, &guid, &interface, parent_, nullptr, EFI_OPEN_PROTOCOL_TEST_PROTOCOL);
    if (present == EFI_UNSUPPORTED || present == EFI_INVALID_PARAMETER) {
        pending_image_ = nullptr;
        return EFI_SUCCESS;
    }
    if (is_error(present)) return present;
    const EFI_STATUS status = boot->UnloadImage(pending_image_);
    if (!is_error(status)) pending_image_ = nullptr;
    return status;
}

EFI_STATUS BootLoader::disable_watchdog()
{
    if (!table_ || !table_->BootServices || !table_->BootServices->SetWatchdogTimer)
        return EFI_UNSUPPORTED;
    const EFI_STATUS status = table_->BootServices->SetWatchdogTimer(0, 0, 0, nullptr);
    // An unsupported arm cannot leave a timer running. Once an arm could have
    // succeeded, every cancellation error must block ordinary menu waiting.
    if (status == EFI_UNSUPPORTED && watchdog_disabled_) return EFI_SUCCESS;
    watchdog_disabled_ = !is_error(status);
    return status;
}

EFI_STATUS BootLoader::retry_cleanup()
{
    EFI_STATUS first = EFI_SUCCESS;
    if (!watchdog_disabled_) first = disable_watchdog();
    const EFI_STATUS cleanup = cleanup_image();
    return is_error(first) ? first : cleanup;
}

BootResult BootLoader::run(const BootEntry &entry)
{
    BootResult result { BootStage::Prepare, EFI_SUCCESS, EFI_SUCCESS };
    if (pending_image_) {
        result.status = EFI_ALREADY_STARTED;
        return result;
    }
    if (!watchdog_disabled_) {
        result.status = EFI_NOT_READY;
        return result;
    }
    if (!parent_) {
        result.status = EFI_INVALID_PARAMETER;
        return result;
    }
    EFI_BOOT_SERVICES *boot = table_ ? table_->BootServices : nullptr;
    if (!boot || !boot->OpenProtocol || !boot->LoadImage || !boot->StartImage || !boot->UnloadImage || !boot->SetWatchdogTimer) {
        result.status = EFI_UNSUPPORTED;
        return result;
    }
    result.status = prepare_path(entry);
    if (is_error(result.status)) return result;

    result.stage = BootStage::Load;
    EFI_HANDLE child = nullptr;
    result.status = boot->LoadImage(0, parent_, reinterpret_cast<EFI_DEVICE_PATH_PROTOCOL *>(device_path_), nullptr, 0, &child);
    pending_image_ = child;
    if (!is_error(result.status) && !child) result.status = EFI_DEVICE_ERROR;
    if (!is_error(result.status)) {
        result.stage = BootStage::CheckImage;
        EFI_GUID guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
        void *interface = nullptr;
        result.status = boot->OpenProtocol(child, &guid, &interface, parent_, nullptr, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
        if (!is_error(result.status)) {
            auto *loaded = static_cast<EFI_LOADED_IMAGE_PROTOCOL *>(interface);
            if (!loaded || loaded->ImageCodeType != EfiLoaderCode || loaded->ImageDataType != EfiLoaderData) {
                result.status = EFI_UNSUPPORTED;
            } else {
                loaded->LoadOptionsSize = 0;
                loaded->LoadOptions = nullptr;
                result.stage = BootStage::Prepare;
                watchdog_disabled_ = false;
                result.status = boot->SetWatchdogTimer(300, 0, 0, nullptr);
                if (result.status == EFI_UNSUPPORTED) {
                    watchdog_disabled_ = true;
                    result.status = EFI_SUCCESS;
                }
                if (!is_error(result.status)) {
                    result.stage = BootStage::Start;
                    result.status = boot->StartImage(child, nullptr, nullptr);
                }
                record_cleanup(result, disable_watchdog());
            }
        }
    }
    record_cleanup(result, cleanup_image());
    if (!is_error(result.status) && is_error(result.cleanup_status)) {
        result.stage = BootStage::Cleanup;
        result.status = result.cleanup_status;
    }
    return result;
}
