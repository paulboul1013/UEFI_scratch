// Exercise the real boot module through firmware function tables only.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "../boot.hpp"

static_assert(sizeof(EFI_DEVICE_PATH_PROTOCOL) == 4);
static_assert(sizeof(EFI_MEMORY_TYPE) == 4);
static_assert(sizeof(EFI_LOADED_IMAGE_PROTOCOL) == 96);
static_assert(__builtin_offsetof(EFI_BOOT_SERVICES, LoadImage) == 200);
static_assert(__builtin_offsetof(EFI_BOOT_SERVICES, StartImage) == 208);
static_assert(__builtin_offsetof(EFI_BOOT_SERVICES, UnloadImage) == 224);
static_assert(__builtin_offsetof(EFI_BOOT_SERVICES, OpenProtocol) == 280);
static_assert(__builtin_offsetof(EFI_LOADED_IMAGE_PROTOCOL, DeviceHandle) == 24);
static_assert(__builtin_offsetof(EFI_LOADED_IMAGE_PROTOCOL, FilePath) == 32);
static_assert(__builtin_offsetof(EFI_LOADED_IMAGE_PROTOCOL, LoadOptionsSize) == 48);
static_assert(__builtin_offsetof(EFI_LOADED_IMAGE_PROTOCOL, LoadOptions) == 56);
static_assert(__builtin_offsetof(EFI_LOADED_IMAGE_PROTOCOL, ImageCodeType) == 80);
static_assert(__builtin_offsetof(EFI_LOADED_IMAGE_PROTOCOL, Unload) == 88);
static_assert(std::is_same<decltype(EFI_BOOT_SERVICES::LoadImage), EFI_LOAD_IMAGE>::value);
static_assert(std::is_same<decltype(EFI_BOOT_SERVICES::StartImage), EFI_START_IMAGE>::value);
static_assert(std::is_same<decltype(EFI_BOOT_SERVICES::UnloadImage), EFI_UNLOAD_IMAGE>::value);
static_assert(std::is_same<decltype(EFI_BOOT_SERVICES::OpenProtocol), EFI_OPEN_PROTOCOL>::value);

static_assert(!std::is_copy_constructible<BootLoader>::value);
static_assert(!std::is_move_constructible<BootLoader>::value);

namespace {
const char *case_name;
unsigned cases_run;
[[noreturn]] void fail(const char *condition, unsigned line)
{
    std::fprintf(stderr, "FAIL [%s] line %u: %s\n", case_name, line, condition);
    std::abort();
}
#define REQUIRE(condition) do { if (!(condition)) fail(#condition, __LINE__); } while (0)
bool error(EFI_STATUS status) { return (status >> 63) != 0; }
const BootEntry demo { L"GOP Demo", L"\\EFI\\TOOLS\\GOP.EFI" };
EFI_HANDLE const parent_handle = reinterpret_cast<EFI_HANDLE>(0x1000);
EFI_HANDLE const device_handle = reinterpret_cast<EFI_HANDLE>(0x2000);
EFI_HANDLE const child_handle = reinterpret_cast<EFI_HANDLE>(0x3000);
struct Fixture;
Fixture *active;

void header(UINT8 *bytes, UINT8 type, UINT8 subtype, UINTN length)
{
    bytes[0] = type;
    bytes[1] = subtype;
    bytes[2] = static_cast<UINT8>(length);
    bytes[3] = static_cast<UINT8>(length >> 8);
}
UINTN file_node(UINT8 *bytes, const CHAR16 *path)
{
    UINTN count = 0;
    while (path[count]) ++count;
    const UINTN length = 4 + 2 * (count + 1);
    header(bytes, 4, 4, length);
    for (UINTN index = 0; index <= count; ++index) {
        bytes[4 + index * 2] = static_cast<UINT8>(path[index]);
        bytes[5 + index * 2] = static_cast<UINT8>(path[index] >> 8);
    }
    return length;
}
void end_node(UINT8 *bytes) { header(bytes, 0x7f, 0xff, 4); }

struct Fixture {
    EFI_BOOT_SERVICES boot {};
    EFI_SYSTEM_TABLE table {};
    EFI_LOADED_IMAGE_PROTOCOL parent {};
    EFI_LOADED_IMAGE_PROTOCOL child {};
    // Padding permits bounded validation of malformed protocol data.
    UINT8 source_storage[1025] {};
    UINT8 parent_storage[1025] {};
    UINT8 *source = source_storage;
    UINT8 *parent_path = parent_storage;
    std::vector<std::string> events;
    std::vector<UINT8> passed_path;
    EFI_STATUS parent_status = EFI_SUCCESS, source_status = EFI_SUCCESS;
    EFI_STATUS child_status = EFI_SUCCESS, load_status = EFI_SUCCESS;
    EFI_STATUS start_status = EFI_SUCCESS, test_status = EFI_SUCCESS;
    EFI_STATUS unload_status = EFI_SUCCESS, arm_status = EFI_SUCCESS, off_status = EFI_SUCCESS;
    bool parent_null = false, source_null = false, child_null = false;
    bool deliver_handle = true, auto_release = true, child_live = false;
    bool override_test = false;
    unsigned loads = 0, starts = 0, unloads = 0, tests = 0, child_gets = 0;
    unsigned arms = 0, offs = 0;

    Fixture()
    {
        REQUIRE(active == nullptr);
        active = this;
        boot.OpenProtocol = open;
        boot.LoadImage = load;
        boot.StartImage = start;
        boot.UnloadImage = unload;
        boot.SetWatchdogTimer = watchdog;
        table.BootServices = &boot;
        parent.DeviceHandle = device_handle;
        set_parent(L"\\EFI\\BOOT\\BOOTX64.EFI");
        header(source, 1, 1, 8);
        source[4] = 0x42; source[5] = 0x17; source[6] = 0x81; source[7] = 0x95;
        end_node(source + 8);
        child.ImageCodeType = EfiLoaderCode;
        child.ImageDataType = EfiLoaderData;
        child.LoadOptionsSize = 123;
        child.LoadOptions = reinterpret_cast<void *>(0xfeed);
    }
    ~Fixture() { REQUIRE(active == this); active = nullptr; }
    void set_parent(const CHAR16 *path)
    {
        std::memset(parent_path, 0, 1024);
        const UINTN length = file_node(parent_path, path);
        end_node(parent_path + length);
        parent.FilePath = reinterpret_cast<EFI_DEVICE_PATH_PROTOCOL *>(parent_path);
    }
    void split_parent(const CHAR16 *first, const CHAR16 *second)
    {
        std::memset(parent_path, 0, 1024);
        UINTN length = file_node(parent_path, first);
        length += file_node(parent_path + length, second);
        end_node(parent_path + length);
        parent.FilePath = reinterpret_cast<EFI_DEVICE_PATH_PROTOCOL *>(parent_path);
    }
    static EFI_STATUS open(EFI_HANDLE handle, EFI_GUID *guid, void **interface,
        EFI_HANDLE agent, EFI_HANDLE controller, UINT32 attributes)
    {
        REQUIRE(agent == parent_handle && controller == nullptr && guid != nullptr);
        const bool loaded = std::memcmp(guid, &EFI_LOADED_IMAGE_PROTOCOL_GUID, 16) == 0;
        const bool device = std::memcmp(guid, &EFI_DEVICE_PATH_PROTOCOL_GUID, 16) == 0;
        if (handle == parent_handle) {
            REQUIRE(loaded && attributes == 2 && interface != nullptr && *interface == nullptr);
            active->events.emplace_back("parent");
            if (!error(active->parent_status) && !active->parent_null) *interface = &active->parent;
            return active->parent_status;
        }
        if (handle == device_handle) {
            REQUIRE(device && attributes == 2 && interface != nullptr && *interface == nullptr);
            active->events.emplace_back("source");
            if (!error(active->source_status) && !active->source_null) *interface = active->source;
            return active->source_status;
        }
        REQUIRE(handle == child_handle && loaded);
        if (attributes == 4) {
            ++active->tests;
            active->events.emplace_back("test");
            return active->override_test ? active->test_status :
                (active->child_live ? EFI_SUCCESS : EFI_INVALID_PARAMETER);
        }
        REQUIRE(attributes == 2 && interface != nullptr && *interface == nullptr);
        REQUIRE(active->child_live);
        ++active->child_gets;
        active->events.emplace_back("child");
        if (!error(active->child_status) && !active->child_null) *interface = &active->child;
        return active->child_status;
    }
    static EFI_STATUS load(BOOLEAN policy, EFI_HANDLE parent_image,
        EFI_DEVICE_PATH_PROTOCOL *path, void *buffer, UINTN size, EFI_HANDLE *image)
    {
        REQUIRE(policy == 0 && parent_image == parent_handle && path != nullptr);
        REQUIRE(buffer == nullptr && size == 0 && image != nullptr && *image == nullptr);
        ++active->loads;
        active->events.emplace_back("load");
        const auto *bytes = reinterpret_cast<const UINT8 *>(path);
        UINTN offset = 0;
        for (;;) {
            REQUIRE(offset <= 2044);
            const UINTN length = bytes[offset + 2] | (static_cast<UINTN>(bytes[offset + 3]) << 8);
            REQUIRE(length >= 4 && length <= 2048 - offset);
            offset += length;
            if (bytes[offset - length] == 0x7f) break;
        }
        active->passed_path.assign(bytes, bytes + offset);
        if (active->deliver_handle) {
            *image = child_handle;
            active->child_live = true;
        }
        return active->load_status;
    }
    static EFI_STATUS start(EFI_HANDLE image, UINTN *exit_size, CHAR16 **exit_data)
    {
        REQUIRE(image == child_handle && active->child_live);
        REQUIRE(exit_size == nullptr && exit_data == nullptr);
        REQUIRE(active->child.LoadOptionsSize == 0 && active->child.LoadOptions == nullptr);
        ++active->starts;
        active->events.emplace_back("start");
        if (active->auto_release) {
            active->child_live = false;
            // Firmware can invalidate the borrowed protocol when the app exits.
            active->child.LoadOptionsSize = 0xdeaddead;
            active->child.ImageCodeType = EfiRuntimeServicesCode;
        }
        return active->start_status;
    }
    static EFI_STATUS unload(EFI_HANDLE image)
    {
        REQUIRE(image == child_handle && active->child_live);
        ++active->unloads;
        active->events.emplace_back("unload");
        if (!error(active->unload_status)) active->child_live = false;
        return active->unload_status;
    }
    static EFI_STATUS watchdog(UINTN timeout, UINT64 code, UINTN size, CHAR16 *data)
    {
        REQUIRE(code == 0 && size == 0 && data == nullptr);
        REQUIRE(timeout == 0 || timeout == 300);
        if (timeout) {
            ++active->arms;
            active->events.emplace_back("arm");
            return active->arm_status;
        }
        ++active->offs;
        active->events.emplace_back("off");
        return active->off_status;
    }
};

template <class Function> void test(const char *name, Function body)
{
    case_name = name;
    body();
    ++cases_run;
}
void rejected(Fixture &fixture, const BootEntry &entry, EFI_STATUS expected)
{
    BootLoader loader(parent_handle, &fixture.table);
    REQUIRE(fixture.events.empty());
    const BootResult result = loader.run(entry);
    REQUIRE(result.stage == BootStage::Prepare && result.status == expected);
    REQUIRE(result.cleanup_status == EFI_SUCCESS);
    REQUIRE(fixture.loads == 0 && fixture.starts == 0);
    REQUIRE(!loader.has_pending_image() && loader.watchdog_disabled());
}
}

int main()
{
    test("read-only entries and inert constructor", [] {
        Fixture f;
        UINTN count = 0;
        const BootEntry *entries = boot_entries(&count);
        REQUIRE(count == 1 && entries && entries[0].label && entries[0].path);
        REQUIRE(boot_entries(nullptr) == entries);
        BootLoader loader(parent_handle, &f.table);
        REQUIRE(!loader.has_pending_image() && loader.watchdog_disabled());
        REQUIRE(loader.retry_cleanup() == EFI_SUCCESS && f.events.empty());
    });
    test("application round trip and exact device path", [] {
        Fixture f;
        BootLoader loader(parent_handle, &f.table);
        const BootResult result = loader.run(demo);
        REQUIRE(result.stage == BootStage::Start && result.status == EFI_SUCCESS);
        REQUIRE(result.cleanup_status == EFI_SUCCESS && !loader.has_pending_image());
        REQUIRE(loader.watchdog_disabled() && f.starts == 1 && f.unloads == 0 && f.tests == 1);
        const std::vector<std::string> order { "parent", "source", "load", "child", "arm", "start", "off", "test" };
        REQUIRE(f.events == order);
        UINT8 expected[128] {};
        std::memcpy(expected, f.source, 8);
        const UINTN length = 8 + file_node(expected + 8, demo.path);
        end_node(expected + length);
        REQUIRE(f.passed_path == std::vector<UINT8>(expected, expected + length + 4));
    });
    test("unaligned protocols and segmented self-load prevention", [] {
        Fixture f;
        f.source = f.source_storage + 1;
        header(f.source, 1, 1, 8); end_node(f.source + 8);
        f.parent_path = f.parent_storage + 1;
        f.split_parent(L"efi\\\\BoOt\\", L"bOoTx64.EfI");
        rejected(f, { L"Self", L"\\\\EFI\\BOOT\\BOOTX64.EFI" }, EFI_ACCESS_DENIED);
    });
    test("different directory same filename is allowed", [] {
        Fixture f;
        BootLoader loader(parent_handle, &f.table);
        REQUIRE(loader.run({ L"Other", L"\\COPY\\BOOTX64.EFI" }).status == EFI_SUCCESS);
        REQUIRE(f.starts == 1);
    });
    test("missing methods and parent handle", [] {
        Fixture f;
        f.boot.LoadImage = nullptr;
        rejected(f, demo, EFI_UNSUPPORTED);
        f.boot.LoadImage = Fixture::load;
        BootLoader loader(nullptr, &f.table);
        REQUIRE(loader.run(demo).status == EFI_INVALID_PARAMETER && f.events.empty());
        BootLoader missing(parent_handle, nullptr);
        REQUIRE(missing.run(demo).status == EFI_UNSUPPORTED);
    });
    for (unsigned variation = 0; variation < 6; ++variation) {
        test("missing source protocol or source location", [variation] {
            Fixture f;
            EFI_STATUS expected = EFI_UNSUPPORTED;
            if (variation == 0) { f.parent_status = EFI_NOT_FOUND; expected = EFI_NOT_FOUND; }
            if (variation == 1) f.parent_null = true;
            if (variation == 2) f.parent.DeviceHandle = nullptr;
            if (variation == 3) f.parent.FilePath = nullptr;
            if (variation == 4) { f.source_status = EFI_DEVICE_ERROR; expected = EFI_DEVICE_ERROR; }
            if (variation == 5) f.source_null = true;
            rejected(f, demo, expected);
        });
    }
    const CHAR16 *invalid_paths[] = {
        nullptr, L"", L"EFI\\TOOLS\\GOP.EFI", L"\\EFI\\.\\GOP.EFI",
        L"\\EFI\\..\\GOP.EFI", L"\\EFI\\TOOLS\\GOP\n.EFI", L"\\EFI\\\u0100.EFI"
    };
    for (const CHAR16 *path : invalid_paths) test("invalid target path", [path] {
        Fixture f; rejected(f, { L"Invalid", path }, EFI_INVALID_PARAMETER);
    });
    test("target length boundary", [] {
        CHAR16 path[261] {};
        path[0] = L'\\';
        for (UINTN index = 1; index < 260; ++index) path[index] = L'a';
        {
            Fixture f;
            rejected(f, { L"Long", path }, EFI_INVALID_PARAMETER);
        }
        path[259] = 0;
        Fixture f;
        BootLoader loader(parent_handle, &f.table);
        REQUIRE(loader.run({ L"Max", path }).status == EFI_SUCCESS);
    });
    for (unsigned variation = 0; variation < 6; ++variation) test("malformed source path", [variation] {
        Fixture f;
        if (variation == 0) header(f.source, 1, 1, 3);
        if (variation == 1) header(f.source, 1, 1, 1025);
        if (variation == 2) header(f.source + 8, 0x7f, 1, 4);
        if (variation == 3) header(f.source + 8, 0x7f, 0xff, 5);
        if (variation == 4) header(f.source + 8, 0x7f, 2, 4);
        if (variation == 5) {
            for (UINTN offset = 0; offset < 1024; offset += 4) header(f.source + offset, 1, 1, 4);
        }
        rejected(f, demo, EFI_INVALID_PARAMETER);
    });
    test("source byte and node boundaries", [] {
        {
            Fixture f;
            header(f.source, 1, 1, 1020); end_node(f.source + 1020);
            BootLoader loader(parent_handle, &f.table);
            REQUIRE(loader.run(demo).status == EFI_SUCCESS);
        }
        {
            Fixture f;
            for (UINTN node = 0; node < 63; ++node) header(f.source + 4 * node, 1, 1, 4);
            end_node(f.source + 252);
            BootLoader loader(parent_handle, &f.table);
            REQUIRE(loader.run(demo).status == EFI_SUCCESS);
        }
        Fixture f;
        for (UINTN node = 0; node < 64; ++node) header(f.source + 4 * node, 1, 1, 4);
        end_node(f.source + 256);
        rejected(f, demo, EFI_INVALID_PARAMETER);
    });
    for (unsigned variation = 0; variation < 7; ++variation) test("unreadable parent filepath", [variation] {
        Fixture f;
        if (variation == 0) header(f.parent_path, 1, 1, 4);
        if (variation == 1) header(f.parent_path, 4, 4, 5);
        if (variation == 2) header(f.parent_path, 4, 4, 6);
        if (variation == 3) f.parent_path[4] = 0;
        if (variation == 4) f.set_parent(L"\\EFI\\..\\BOOTX64.EFI");
        if (variation == 5) f.set_parent(L"\\EFI\\\u0100.EFI");
        if (variation == 6) {
            f.set_parent(L"\\SELF.EFI");
            const UINTN length = file_node(f.parent_path, L"\\SELF.EFI");
            header(f.parent_path + length, 0x7f, 1, 4);
        }
        rejected(f, demo, EFI_INVALID_PARAMETER);
    });
    test("load error without image", [] {
        Fixture f; f.load_status = EFI_NOT_FOUND; f.deliver_handle = false;
        BootLoader loader(parent_handle, &f.table);
        const BootResult result = loader.run(demo);
        REQUIRE(result.stage == BootStage::Load && result.status == EFI_NOT_FOUND);
        REQUIRE(f.starts == 0 && f.tests == 0 && f.arms == 0 && !loader.has_pending_image());
    });
    test("security violation with a delivered image", [] {
        Fixture f; f.load_status = EFI_SECURITY_VIOLATION;
        BootLoader loader(parent_handle, &f.table);
        const BootResult result = loader.run(demo);
        REQUIRE(result.stage == BootStage::Load && result.status == EFI_SECURITY_VIOLATION);
        REQUIRE(f.starts == 0 && f.unloads == 1 && !loader.has_pending_image());
    });
    test("success with an empty image handle", [] {
        Fixture f; f.deliver_handle = false;
        BootLoader loader(parent_handle, &f.table);
        const BootResult result = loader.run(demo);
        REQUIRE(result.stage == BootStage::Load && result.status == EFI_DEVICE_ERROR);
        REQUIRE(f.starts == 0 && f.tests == 0);
    });
    for (unsigned variation = 0; variation < 4; ++variation) test("unsupported or absent loaded image metadata", [variation] {
        Fixture f;
        EFI_STATUS expected = EFI_UNSUPPORTED;
        if (variation == 0) f.child.ImageCodeType = EfiBootServicesCode;
        if (variation == 1) f.child.ImageDataType = EfiRuntimeServicesData;
        if (variation == 2) f.child_null = true;
        if (variation == 3) { f.child_status = EFI_DEVICE_ERROR; expected = EFI_DEVICE_ERROR; }
        BootLoader loader(parent_handle, &f.table);
        const BootResult result = loader.run(demo);
        REQUIRE(result.stage == BootStage::CheckImage && result.status == expected);
        REQUIRE(f.starts == 0 && f.unloads == 1 && f.arms == 0);
    });
    for (bool released : { true, false }) test("error return queries liveness before cleanup", [released] {
        Fixture f; f.start_status = EFI_ABORTED; f.auto_release = released;
        BootLoader loader(parent_handle, &f.table);
        const BootResult result = loader.run(demo);
        REQUIRE(result.stage == BootStage::Start && result.status == EFI_ABORTED);
        REQUIRE(f.tests == 1 && f.unloads == (released ? 0U : 1U));
        REQUIRE(!loader.has_pending_image() && f.child_gets == 1);
    });
    for (EFI_STATUS gone : { EFI_UNSUPPORTED, EFI_INVALID_PARAMETER }) test("missing child protocol proves image gone", [gone] {
        Fixture f; f.override_test = true; f.test_status = gone;
        BootLoader loader(parent_handle, &f.table);
        REQUIRE(loader.run(demo).status == EFI_SUCCESS && f.unloads == 0 && !loader.has_pending_image());
    });
    test("query failure retains ownership and blocks new load", [] {
        Fixture f; f.auto_release = false; f.override_test = true; f.test_status = EFI_DEVICE_ERROR;
        BootLoader loader(parent_handle, &f.table);
        const BootResult result = loader.run(demo);
        REQUIRE(result.stage == BootStage::Cleanup && result.status == EFI_DEVICE_ERROR);
        REQUIRE(result.cleanup_status == EFI_DEVICE_ERROR && loader.has_pending_image());
        REQUIRE(loader.run(demo).status == EFI_ALREADY_STARTED && f.loads == 1 && f.unloads == 0);
        f.override_test = false;
        REQUIRE(loader.retry_cleanup() == EFI_SUCCESS && f.unloads == 1 && !loader.has_pending_image());
    });
    test("failed unload preserves primary status and explicit retry", [] {
        Fixture f; f.load_status = EFI_SECURITY_VIOLATION; f.unload_status = EFI_ACCESS_DENIED;
        BootLoader loader(parent_handle, &f.table);
        const BootResult result = loader.run(demo);
        REQUIRE(result.stage == BootStage::Load && result.status == EFI_SECURITY_VIOLATION);
        REQUIRE(result.cleanup_status == EFI_ACCESS_DENIED && loader.has_pending_image());
        REQUIRE(loader.run(demo).status == EFI_ALREADY_STARTED && f.loads == 1);
        f.unload_status = EFI_SUCCESS;
        REQUIRE(loader.retry_cleanup() == EFI_SUCCESS && f.unloads == 2 && !loader.has_pending_image());
        REQUIRE(loader.retry_cleanup() == EFI_SUCCESS && f.unloads == 2);
    });
    test("unsupported watchdog permits start", [] {
        Fixture f; f.arm_status = EFI_UNSUPPORTED; f.off_status = EFI_UNSUPPORTED;
        BootLoader loader(parent_handle, &f.table);
        REQUIRE(loader.run(demo).status == EFI_SUCCESS && f.starts == 1 && f.offs == 1);
        REQUIRE(loader.watchdog_disabled());
    });
    test("failed watchdog arm cancels then unloads without start", [] {
        Fixture f; f.arm_status = EFI_DEVICE_ERROR;
        BootLoader loader(parent_handle, &f.table);
        const BootResult result = loader.run(demo);
        REQUIRE(result.stage == BootStage::Prepare && result.status == EFI_DEVICE_ERROR);
        REQUIRE(f.starts == 0 && f.offs == 1 && f.unloads == 1 && loader.watchdog_disabled());
        REQUIRE(f.events[f.events.size() - 3] == "off");
    });
    for (EFI_STATUS off : { EFI_DEVICE_ERROR, EFI_UNSUPPORTED }) test("watchdog cancellation failure prevents another start", [off] {
        Fixture f; f.off_status = off;
        BootLoader loader(parent_handle, &f.table);
        const BootResult result = loader.run(demo);
        REQUIRE(result.stage == BootStage::Cleanup && result.status == off && result.cleanup_status == off);
        REQUIRE(!loader.has_pending_image() && !loader.watchdog_disabled());
        REQUIRE(error(loader.run(demo).status) && f.loads == 1);
        f.off_status = EFI_SUCCESS;
        REQUIRE(loader.retry_cleanup() == EFI_SUCCESS && loader.watchdog_disabled());
    });
    test("cleanup errors never overwrite a prior start error", [] {
        Fixture f; f.auto_release = false; f.start_status = EFI_ABORTED;
        f.off_status = EFI_DEVICE_ERROR; f.unload_status = EFI_ACCESS_DENIED;
        BootLoader loader(parent_handle, &f.table);
        const BootResult result = loader.run(demo);
        REQUIRE(result.stage == BootStage::Start && result.status == EFI_ABORTED);
        REQUIRE(result.cleanup_status == EFI_DEVICE_ERROR && loader.has_pending_image());
        f.off_status = EFI_SUCCESS; f.unload_status = EFI_SUCCESS;
        const unsigned begin = static_cast<unsigned>(f.events.size());
        REQUIRE(loader.retry_cleanup() == EFI_SUCCESS);
        REQUIRE(f.events[begin] == "off" && f.events[begin + 1] == "test" && f.events[begin + 2] == "unload");
    });
    test("warnings do not skip handle and metadata validation", [] {
        Fixture f; f.parent_status = 1; f.source_status = 1; f.child_status = 1;
        f.load_status = 1; f.start_status = 1; f.arm_status = 1; f.off_status = 1;
        BootLoader loader(parent_handle, &f.table);
        const BootResult result = loader.run(demo);
        REQUIRE(result.stage == BootStage::Start && result.status == 1 && result.cleanup_status == EFI_SUCCESS);
        REQUIRE(f.starts == 1 && loader.watchdog_disabled() && !loader.has_pending_image());
    });
    test("successful run can execute the configured image again", [] {
        Fixture f;
        BootLoader loader(parent_handle, &f.table);
        REQUIRE(loader.run(demo).status == EFI_SUCCESS);
        f.child.ImageCodeType = EfiLoaderCode;
        REQUIRE(loader.run(demo).status == EFI_SUCCESS && f.starts == 2 && f.unloads == 0);
    });
    std::printf("boot host: %u cases passed\n", cases_run);
}
