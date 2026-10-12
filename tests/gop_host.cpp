// Execute the real application and startup contract with fake firmware.
// Exact query allocations and deleted old Mode/Info objects make ASan useful.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../main.cpp"
#define _start gop_test_startup
#include "../startup.cpp"
#undef _start

namespace {
const char *case_name;
unsigned cases_run;
constexpr EFI_STATUS OP_FAILURE = (1ULL << 63) | 1;
constexpr EFI_STATUS TEXT_FAILURE = (1ULL << 63) | 2;
constexpr EFI_STATUS RESTORE_FAILURE = (1ULL << 63) | 9;

[[noreturn]] void fail(const char *condition, unsigned line)
{
    std::fprintf(stderr, "FAIL [%s] line %u: %s\n", case_name, line, condition);
    std::abort();
}
#define REQUIRE(condition) do { if (!(condition)) fail(#condition, __LINE__); } while (0)

struct QueryResult {
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info {};
    UINTN size = sizeof(info);
    EFI_STATUS status = EFI_SUCCESS;
    bool null_info = false;
};
struct Allocation {
    void *pointer;
    UINT32 mode;
    unsigned attempts = 0;
    bool alive = true;
};
enum class Corruption {
    none, null_mode, null_info, short_info, wrong_number, wrong_width,
    wrong_height, wrong_format, unsupported_version, zero_width
};
struct Fixture;
Fixture *active;

struct Fixture {
    EFI_GRAPHICS_OUTPUT_PROTOCOL gop {};
    EFI_BOOT_SERVICES boot {};
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console {};
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL keyboard {};
    EFI_SYSTEM_TABLE table {};
    std::unique_ptr<EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE> mode;
    std::unique_ptr<EFI_GRAPHICS_OUTPUT_MODE_INFORMATION> info;
    std::vector<QueryResult> modes;
    std::vector<Allocation> allocations;
    std::vector<std::string> events;
    std::vector<UINT32> queries, sets;
    std::string output;
    EFI_STATUS locate_status = EFI_SUCCESS;
    void *located_interface = &gop;
    EFI_STATUS free_status = EFI_SUCCESS, clear_status = EFI_SUCCESS;
    EFI_STATUS switch_status = EFI_SUCCESS, restore_status = EFI_SUCCESS;
    EFI_STATUS blt_status = OP_FAILURE, wait_status = EFI_SUCCESS, read_status = EFI_SUCCESS;
    Corruption switch_corruption = Corruption::none, restore_corruption = Corruption::none;
    Corruption console_corruption = Corruption::none;
    std::string fail_text, mutate_text;
    EFI_STATUS output_status = TEXT_FAILURE;
    bool error_text_fails = false, switch_failure_changes_mode = false;
    bool clear_changes_mode = false;
    unsigned fail_blt = 0, blts = 0, waits = 0, reads = 0, clears = 0;
    unsigned init_calls = 0, fini_calls = 0, successful_restores = 0, mutation_count = 0;
    UINT32 original = 0, draw_width = 0, draw_height = 0;

    static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION information(UINT32 width, UINT32 height,
        EFI_GRAPHICS_PIXEL_FORMAT format = PixelBlueGreenRedReserved8BitPerColor)
    {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION value {};
        value.HorizontalResolution = width;
        value.VerticalResolution = height;
        value.PixelFormat = format;
        value.PixelsPerScanLine = width;
        return value;
    }
    Fixture()
    {
        REQUIRE(active == nullptr);
        active = this;
        modes.resize(3);
        modes[0].info = information(1280, 800);
        modes[1].info = information(800, 600);
        modes[2].info = information(1024, 768, PixelRedGreenBlueReserved8BitPerColor);
        install_mode(0);
        gop.QueryMode = query;
        gop.SetMode = set;
        gop.Blt = fill;
        boot.LocateProtocol = locate;
        boot.FreePool = free_pool;
        boot.WaitForEvent = wait;
        console.OutputString = write;
        console.ClearScreen = clear;
        keyboard.ReadKeyStroke = read;
        keyboard.WaitForKey = reinterpret_cast<EFI_EVENT>(1);
        table.ConOut = &console;
        table.ConIn = &keyboard;
        table.BootServices = &boot;
    }
    ~Fixture()
    {
        // Fixture cleanup after rejected FreePool is not an application free.
        for (auto &allocation : allocations) {
            if (allocation.alive) std::free(allocation.pointer);
        }
        active = nullptr;
    }
    void install_mode(UINT32 number)
    {
        REQUIRE(number < modes.size());
        // Allocate replacements before deleting old objects to force new addresses.
        auto new_info = std::make_unique<EFI_GRAPHICS_OUTPUT_MODE_INFORMATION>(modes[number].info);
        auto new_mode = std::make_unique<EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE>();
        new_mode->MaxMode = static_cast<UINT32>(modes.size());
        new_mode->Mode = number;
        new_mode->Info = new_info.get();
        new_mode->SizeOfInfo = sizeof(*new_info);
        REQUIRE(new_mode.get() != mode.get() && new_info.get() != info.get());
        mode = std::move(new_mode);
        info = std::move(new_info);
        gop.Mode = mode.get();
    }
    void fallback_only(UINT32 width = 1280, UINT32 height = 800)
    {
        modes.resize(1);
        modes[0].info = information(width, height);
        install_mode(0);
    }
    void corrupt(Corruption corruption)
    {
        switch (corruption) {
        case Corruption::none: break;
        case Corruption::null_mode: gop.Mode = nullptr; break;
        case Corruption::null_info: mode->Info = nullptr; break;
        case Corruption::short_info: mode->SizeOfInfo = sizeof(*info) - 1; break;
        case Corruption::wrong_number: mode->Mode = (mode->Mode + 1) % mode->MaxMode; break;
        case Corruption::wrong_width: ++info->HorizontalResolution; break;
        case Corruption::wrong_height: ++info->VerticalResolution; break;
        case Corruption::wrong_format: info->PixelFormat = PixelBltOnly; break;
        case Corruption::unsupported_version: info->Version = 1; break;
        case Corruption::zero_width: info->HorizontalResolution = 0; break;
        }
    }
    unsigned live_queries() const
    {
        return static_cast<unsigned>(std::count_if(allocations.begin(), allocations.end(),
            [](const Allocation &allocation) { return allocation.alive; }));
    }
    size_t event(const std::string &value) const
    {
        const auto found = std::find(events.begin(), events.end(), value);
        REQUIRE(found != events.end());
        return static_cast<size_t>(found - events.begin());
    }
    size_t text_event(const std::string &fragment) const
    {
        for (size_t i = 0; i < events.size(); ++i) {
            if (events[i].rfind("text:", 0) == 0 && events[i].find(fragment) != std::string::npos) return i;
        }
        REQUIRE(false);
        return 0;
    }
    void contains(const std::string &fragment) const
    {
        if (output.find(fragment) == std::string::npos) {
            std::fprintf(stderr, "Output for [%s]:\n%s\n", case_name, output.c_str());
            fail(fragment.c_str(), __LINE__);
        }
    }
    EFI_STATUS run(bool startup = false)
    {
        const EFI_STATUS result = startup ? gop_test_startup(nullptr, &table) : efi_main(nullptr, &table);
        for (const auto &allocation : allocations) REQUIRE(allocation.attempts == 1);
        if (free_status == EFI_SUCCESS) REQUIRE(live_queries() == 0);
        REQUIRE(event("locate") < text_event("Global constructor"));
        REQUIRE(output.find("Hello UEFI") == std::string::npos || event("locate") < text_event("Hello UEFI"));
        for (size_t i = 0; i < queries.size(); ++i) REQUIRE(queries[i] == i);
        if (startup) {
            REQUIRE(init_calls == 1 && fini_calls == 1);
            REQUIRE(events.front() == "init" && events.back() == "fini");
            if (!sets.empty()) REQUIRE(event("set:" + std::to_string(original)) < event("fini"));
        }
        return result;
    }
    void expect_no_work(EFI_STATUS status)
    {
        REQUIRE(run() == status);
        REQUIRE(queries.empty() && allocations.empty() && sets.empty());
        REQUIRE(blts == 0 && waits == 0 && reads == 0 && clears == 0);
        contains("[GOP] ERROR");
    }
    void expect_restored_failure(EFI_STATUS status, unsigned expected_blts = 0)
    {
        REQUIRE(run() == status);
        REQUIRE(sets == std::vector<UINT32>({1, original}));
        REQUIRE(blts == expected_blts && mode->Mode == original);
        REQUIRE(waits == 0 && reads == 0);
        contains("[GOP] ERROR");
    }
    void expect_success(UINT32 selected = 1, bool startup = false)
    {
        REQUIRE(run(startup) == EFI_SUCCESS);
        REQUIRE(queries.size() == modes.size());
        if (selected == original) REQUIRE(sets.empty());
        else REQUIRE(sets == std::vector<UINT32>({selected, original}));
        REQUIRE(blts == 3 && waits == 1 && reads == 1 && clears == 1 && mode->Mode == original);
        contains("[GOP] FOUND");
        contains("[GOP] ORIGINAL mode=" + std::to_string(original));
        contains("[GOP] ACTIVE mode=" + std::to_string(selected));
        contains("[GOP] RESOLUTION " + std::to_string(draw_width) + "x" + std::to_string(draw_height));
        REQUIRE(event("clear") < text_event("[GOP] ACTIVE"));
        if (selected != original) REQUIRE(event("clear") < event("set:" + std::to_string(selected)));
        REQUIRE(text_event("Press any key") < event("blt:1"));
        REQUIRE(event("blt:3") < text_event("[GOP] DRAW_OK"));
        REQUIRE(text_event("[GOP] DRAW_OK") < event("wait") && event("wait") < event("read"));
        if (selected != original) {
            REQUIRE(event("read") < text_event("[GOP] RESTORE"));
            REQUIRE(text_event("[GOP] RESTORE") < event("set:" + std::to_string(original)));
        }
        std::string screen;
        for (size_t i = text_event("[GOP] ACTIVE"); i <= text_event("[GOP] DRAW_OK"); ++i) {
            if (events[i].rfind("text:", 0) == 0) screen += events[i].substr(5);
        }
        REQUIRE(screen.find("\r\n[GOP] RESOLUTION") != std::string::npos);
        REQUIRE(screen.find("\r\nPress any key to exit...\r\n[GOP] DRAW_OK") != std::string::npos);
        REQUIRE(screen.size() >= 13 && screen.substr(screen.size() - 13) == "[GOP] DRAW_OK");
    }
    static EFI_STATUS EFIAPI locate(EFI_GUID *guid, void *registration, void **interface)
    {
        auto &f = *active;
        f.events.emplace_back("locate");
        const EFI_GUID expected = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
        REQUIRE(std::memcmp(guid, &expected, sizeof(expected)) == 0);
        REQUIRE(registration == nullptr && *interface == nullptr);
        *interface = f.located_interface;
        return f.locate_status;
    }
    static EFI_STATUS EFIAPI query(EFI_GRAPHICS_OUTPUT_PROTOCOL *self, UINT32 number,
        UINTN *size, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **information_out)
    {
        auto &f = *active;
        REQUIRE(self == &f.gop && number < f.modes.size());
        REQUIRE(*size == 0 && *information_out == nullptr && f.live_queries() == 0);
        f.events.emplace_back("query:" + std::to_string(number));
        f.queries.push_back(number);
        const auto &answer = f.modes[number];
        *size = answer.size;
        if (answer.status != EFI_SUCCESS) {
            *information_out = reinterpret_cast<EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *>(1);
            return answer.status;
        }
        if (answer.null_info) { *information_out = nullptr; return EFI_SUCCESS; }
        // A one-byte pool represents a nonempty allocation with reported size zero.
        const size_t bytes = static_cast<size_t>(answer.size == 0 ? 1 : answer.size);
        void *buffer = std::malloc(bytes);
        REQUIRE(buffer != nullptr);
        std::memset(buffer, 0xa5, bytes);
        std::memcpy(buffer, &answer.info, std::min(bytes, sizeof(answer.info)));
        f.allocations.push_back({buffer, number});
        *information_out = static_cast<EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *>(buffer);
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI free_pool(void *pointer)
    {
        auto &f = *active;
        REQUIRE(pointer != nullptr && pointer != f.mode.get() && pointer != f.info.get());
        auto found = std::find_if(f.allocations.rbegin(), f.allocations.rend(),
            [pointer](const Allocation &allocation) { return allocation.pointer == pointer && allocation.alive; });
        REQUIRE(found != f.allocations.rend() && found->attempts == 0);
        ++found->attempts;
        f.events.emplace_back("free:" + std::to_string(found->mode));
        if (f.free_status == EFI_SUCCESS) { std::free(pointer); found->alive = false; }
        return f.free_status;
    }
    static EFI_STATUS EFIAPI set(EFI_GRAPHICS_OUTPUT_PROTOCOL *self, UINT32 number)
    {
        auto &f = *active;
        const bool restoring = number == f.original;
        REQUIRE(self == &f.gop && number < f.modes.size());
        // Firmware can reject a free; that allocation must not prevent restoration.
        REQUIRE(f.live_queries() == 0 || (restoring && f.free_status != EFI_SUCCESS));
        f.events.emplace_back("set:" + std::to_string(number));
        f.sets.push_back(number);
        const EFI_STATUS status = restoring ? f.restore_status : f.switch_status;
        if (status == EFI_SUCCESS || (!restoring && f.switch_failure_changes_mode)) {
            f.install_mode(number);
            f.corrupt(restoring ? f.restore_corruption : f.switch_corruption);
        }
        if (restoring && status == EFI_SUCCESS && f.restore_corruption == Corruption::none) ++f.successful_restores;
        return status;
    }
    static EFI_STATUS EFIAPI write(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, CHAR16 *text)
    {
        auto &f = *active;
        REQUIRE(self == &f.console && f.successful_restores == 0);
        std::string message;
        while (*text) message += static_cast<char>(*text++);
        f.events.emplace_back("text:" + message);
        f.output += message;
        if (message.find("[GOP] MODE ") != std::string::npos || message.find("[GOP] WARN") != std::string::npos) {
            REQUIRE(f.live_queries() == 0 && !f.queries.empty());
            const UINT32 number = f.queries.back();
            const auto &answer = f.modes[number];
            if (answer.status == EFI_SUCCESS && !answer.null_info) {
                REQUIRE(f.event("query:" + std::to_string(number)) < f.event("free:" + std::to_string(number)));
                REQUIRE(f.event("free:" + std::to_string(number)) < f.events.size() - 1);
            }
        }
        if (!f.mutate_text.empty() && message.find(f.mutate_text) != std::string::npos) {
            ++f.mutation_count;
            if (f.console_corruption == Corruption::none) f.install_mode(2);
            else f.corrupt(f.console_corruption);
        }
        if (f.error_text_fails && message.find("[GOP] ERROR") != std::string::npos) return TEXT_FAILURE;
        if (!f.fail_text.empty() && message.find(f.fail_text) != std::string::npos) return f.output_status;
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI clear(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self)
    {
        auto &f = *active;
        REQUIRE(self == &f.console && f.gop.Mode != nullptr && f.gop.Mode->Mode == f.original);
        REQUIRE(f.live_queries() == 0);
        ++f.clears;
        f.events.emplace_back("clear");
        if (f.clear_changes_mode) f.install_mode(2);
        return f.clear_status;
    }
    static EFI_STATUS EFIAPI fill(EFI_GRAPHICS_OUTPUT_PROTOCOL *self,
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL *pixel, EFI_GRAPHICS_OUTPUT_BLT_OPERATION operation,
        UINTN sx, UINTN sy, UINTN x, UINTN y, UINTN width, UINTN height, UINTN delta)
    {
        auto &f = *active;
        REQUIRE(self == &f.gop && operation == EfiBltVideoFill && sx == 0 && sy == 0 && delta == 0);
        REQUIRE(f.gop.Mode != nullptr && f.gop.Mode->Info != nullptr);
        f.draw_width = f.gop.Mode->Info->HorizontalResolution;
        f.draw_height = f.gop.Mode->Info->VerticalResolution;
        const UINTN block_width = f.draw_width / 5, block_height = f.draw_height / 4;
        const UINTN gap = f.draw_width / 20;
        const UINTN left = (f.draw_width - (3 * block_width + 2 * gap)) / 2;
        REQUIRE(width == block_width && height == block_height && width != 0 && height != 0);
        REQUIRE(x == left + f.blts * (block_width + gap));
        REQUIRE(y == f.draw_height / 2 + f.draw_height / 8);
        REQUIRE(x + width <= f.draw_width && y + height <= f.draw_height);
        const auto *bytes = reinterpret_cast<const UINT8 *>(pixel);
        REQUIRE(bytes[0] == (f.blts == 2 ? 255 : 0));
        REQUIRE(bytes[1] == (f.blts == 1 ? 255 : 0));
        REQUIRE(bytes[2] == (f.blts == 0 ? 255 : 0));
        REQUIRE(bytes[3] == 0);
        f.events.emplace_back("blt:" + std::to_string(++f.blts));
        f.contains("Press any key to exit...");
        REQUIRE(f.output.find("[GOP] DRAW_OK") == std::string::npos);
        return f.blts == f.fail_blt ? f.blt_status : EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI wait(UINTN count, EFI_EVENT *events, UINTN *index)
    {
        auto &f = *active;
        REQUIRE(count == 1 && events[0] == reinterpret_cast<EFI_EVENT>(1) && f.blts == 3);
        f.contains("[GOP] DRAW_OK");
        ++f.waits;
        f.events.emplace_back("wait");
        *index = 0;
        return f.wait_status;
    }
    static EFI_STATUS EFIAPI read(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *self, EFI_INPUT_KEY *key)
    {
        auto &f = *active;
        REQUIRE(self == &f.keyboard && f.waits == 1);
        ++f.reads;
        f.events.emplace_back("read");
        key->UnicodeChar = L' ';
        return f.read_status;
    }
};
template <class Body> void check(const char *name, Body body)
{
    case_name = name;
    Fixture fixture;
    body(fixture);
    ++cases_run;
}
} // namespace

// Stubs prove startup call order and returned status, not real destructors.
void runtime_init() { ++active->init_calls; active->events.emplace_back("init"); }
void runtime_fini() { ++active->fini_calls; active->events.emplace_back("fini"); }

int main()
{
    check("target ownership replacement pointers and full order", [](Fixture &f) {
        f.expect_success();
        REQUIRE(f.allocations.size() == 3 && f.draw_width == 800 && f.draw_height == 600);
        f.contains("[GOP] MODE 0 1280x800 format=BGR\r\n");
        f.contains("[GOP] MODE 1 800x600 format=BGR\r\n");
        f.contains("[GOP] MODE 2 1024x768 format=RGB\r\n");
    });
    check("duplicate targets choose smallest number", [](Fixture &f) {
        f.modes[2].info = Fixture::information(800, 600, PixelRedGreenBlueReserved8BitPerColor);
        f.expect_success(1);
    });
    for (bool query_fails : {false, true}) {
        check("original target wins over smaller target even if original query fails", [query_fails](Fixture &f) {
            f.original = 2;
            f.modes[2].info = Fixture::information(800, 600);
            f.install_mode(2);
            if (query_fails) f.modes[2].status = OP_FAILURE;
            f.expect_success(2);
        });
    }
    check("complete enumeration has no target", [](Fixture &f) {
        f.modes[1].info = Fixture::information(640, 480);
        f.expect_success(0);
        f.contains("target-not-found");
        REQUIRE(f.output.find("enumeration-incomplete") == std::string::npos);
    });
    check("UINT32 maximum formatting and BLT geometry", [](Fixture &f) {
        f.fallback_only(4294967295U, 4294967295U);
        f.expect_success(0);
        f.contains("4294967295x4294967295");
    });
    check("LocateProtocol not found still prints constructor and Hello", [](Fixture &f) {
        f.locate_status = EFI_NOT_FOUND;
        f.located_interface = nullptr;
        f.expect_no_work(EFI_NOT_FOUND);
        f.contains("LocateProtocol status=0x800000000000000E");
    });
    check("LocateProtocol preserves device error", [](Fixture &f) {
        f.locate_status = EFI_DEVICE_ERROR; f.expect_no_work(EFI_DEVICE_ERROR);
    });
    check("null GOP", [](Fixture &f) { f.located_interface = nullptr; f.expect_no_work(EFI_DEVICE_ERROR); });
    check("null original Mode", [](Fixture &f) { f.gop.Mode = nullptr; f.expect_no_work(EFI_DEVICE_ERROR); });
    check("null original Info", [](Fixture &f) { f.mode->Info = nullptr; f.expect_no_work(EFI_DEVICE_ERROR); });
    check("short original Info", [](Fixture &f) { --f.mode->SizeOfInfo; f.expect_no_work(EFI_DEVICE_ERROR); });
    check("zero MaxMode", [](Fixture &f) { f.mode->MaxMode = 0; f.expect_no_work(EFI_DEVICE_ERROR); });
    check("original number out of range", [](Fixture &f) { f.mode->Mode = f.mode->MaxMode; f.expect_no_work(EFI_DEVICE_ERROR); });
    check("zero original width", [](Fixture &f) { f.info->HorizontalResolution = 0; f.expect_no_work(EFI_DEVICE_ERROR); });
    check("zero original height", [](Fixture &f) { f.info->VerticalResolution = 0; f.expect_no_work(EFI_DEVICE_ERROR); });
    check("unsupported original version", [](Fixture &f) { f.info->Version = 1; f.expect_no_work(EFI_UNSUPPORTED); });
    check("invalid original pixel format", [](Fixture &f) { f.info->PixelFormat = PixelFormatMax; f.expect_no_work(EFI_UNSUPPORTED); });
    check("missing QueryMode", [](Fixture &f) { f.gop.QueryMode = nullptr; f.expect_no_work(EFI_UNSUPPORTED); });
    check("missing SetMode", [](Fixture &f) { f.gop.SetMode = nullptr; f.expect_no_work(EFI_UNSUPPORTED); });
    check("missing Blt", [](Fixture &f) { f.gop.Blt = nullptr; f.expect_no_work(EFI_UNSUPPORTED); });
    check("missing FreePool", [](Fixture &f) { f.boot.FreePool = nullptr; f.expect_no_work(EFI_UNSUPPORTED); });
    check("missing ClearScreen", [](Fixture &f) { f.console.ClearScreen = nullptr; f.expect_no_work(EFI_UNSUPPORTED); });
    for (UINTN size : {0ULL, 1ULL, 4ULL, 12ULL, 35ULL}) {
        check("actual short query allocation is freed without field reads", [size](Fixture &f) {
            f.modes[1].size = size;
            f.expect_success(0);
            f.contains("[GOP] WARN"); f.contains("enumeration-incomplete");
            REQUIRE(f.output.find("target-not-found") == std::string::npos);
        });
    }
    check("larger query allocation retains valid prefix", [](Fixture &f) {
        f.modes[1].size = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION) + 128;
        f.expect_success();
    });
    check("successful null query buffer is not freed", [](Fixture &f) {
        f.modes[1].null_info = true; f.expect_success(0);
        REQUIRE(f.allocations.size() == 2); f.contains("enumeration-incomplete");
    });
    for (UINT32 number : {0U, 1U, 2U}) {
        check("failed query poison OUT is not read or freed", [number](Fixture &f) {
            f.modes[number].status = OP_FAILURE;
            f.expect_success(number == 1 ? 0 : 1);
            REQUIRE(f.allocations.size() == 2); f.contains("[GOP] WARN"); f.contains("0x8000000000000001");
        });
    }
    check("all queries fail and reliable original remains usable", [](Fixture &f) {
        for (auto &answer : f.modes) answer.status = OP_FAILURE;
        f.expect_success(0); REQUIRE(f.allocations.empty()); f.contains("enumeration-incomplete");
        REQUIRE(f.output.find("target-not-found") == std::string::npos);
    });
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        check("invalid query version format or resolution is freed and skipped", [invalid](Fixture &f) {
            if (invalid == 0) f.modes[1].info.Version = 1;
            if (invalid == 1) f.modes[1].info.PixelFormat = PixelFormatMax;
            if (invalid == 2) f.modes[1].info.HorizontalResolution = 0;
            if (invalid == 3) f.modes[1].info.VerticalResolution = 0;
            f.expect_success(0); f.contains("enumeration-incomplete");
        });
    }
    for (unsigned format = 0; format < PixelFormatMax; ++format) {
        check("each GOP format uses same BLT bytes without framebuffer", [format](Fixture &f) {
            f.modes[1].info.PixelFormat = static_cast<EFI_GRAPHICS_PIXEL_FORMAT>(format);
            f.modes[1].info.PixelsPerScanLine = 0; f.expect_success();
            const char *labels[] = {"RGB", "BGR", "BITMASK", "BLT_ONLY"};
            f.contains(std::string("format=") + labels[format]);
        });
    }
    check("128 modes enumerate with one live allocation", [](Fixture &f) {
        f.modes.resize(128);
        for (UINT32 i = 3; i < f.modes.size(); ++i) f.modes[i].info = Fixture::information(1024 + i, 768);
        f.install_mode(0); f.expect_success();
        REQUIRE(f.allocations.size() == 128 && f.event("free:127") < f.event("clear"));
    });
    check("FreePool failure stops and attempts exactly once", [](Fixture &f) {
        f.free_status = OP_FAILURE; REQUIRE(f.run() == OP_FAILURE);
        REQUIRE(f.queries == std::vector<UINT32>({0}));
        REQUIRE(f.allocations.size() == 1 && f.allocations[0].attempts == 1);
        REQUIRE(f.sets.empty() && f.blts == 0 && f.waits == 0);
        f.contains("[GOP] ERROR"); f.contains("0x8000000000000001");
    });
    check("FreePool error survives error reporter failure", [](Fixture &f) {
        f.free_status = OP_FAILURE; f.error_text_fails = true;
        REQUIRE(f.run() == OP_FAILURE && f.queries == std::vector<UINT32>({0}));
        f.contains("FreePool mode=0 status=0x8000000000000001");
    });
    check("rejected free plus error console drift restores before startup fini", [](Fixture &f) {
        f.free_status = OP_FAILURE; f.mutate_text = "FreePool mode=0";
        REQUIRE(f.run(true) == OP_FAILURE && f.sets == std::vector<UINT32>({0}));
        REQUIRE(f.allocations.size() == 1 && f.allocations[0].attempts == 1 && f.allocations[0].alive);
        REQUIRE(f.mode->Mode == 0 && f.blts == 0 && f.waits == 0);
    });
    check("mode output failure occurs after FreePool and stops queries", [](Fixture &f) {
        f.fail_text = "[GOP] MODE 1 "; REQUIRE(f.run() == TEXT_FAILURE);
        REQUIRE(f.queries == std::vector<UINT32>({0, 1}));
        REQUIRE(f.event("free:1") < f.text_event(f.fail_text));
        REQUIRE(f.sets.empty() && f.blts == 0 && f.waits == 0);
    });
    check("warning output failure terminates enumeration", [](Fixture &f) {
        f.modes[0].status = OP_FAILURE; f.fail_text = "[GOP] WARN";
        REQUIRE(f.run() == TEXT_FAILURE && f.queries == std::vector<UINT32>({0}));
        REQUIRE(f.sets.empty() && f.waits == 0);
    });
    for (bool changes : {false, true}) {
        check("failed switch restores whether or not firmware changed mode", [changes](Fixture &f) {
            f.switch_status = OP_FAILURE; f.switch_failure_changes_mode = changes;
            f.expect_restored_failure(OP_FAILURE);
        });
    }
    for (Corruption corruption : {Corruption::null_mode, Corruption::null_info, Corruption::short_info,
        Corruption::wrong_number, Corruption::wrong_width, Corruption::wrong_height,
        Corruption::wrong_format, Corruption::unsupported_version, Corruption::zero_width}) {
        check("switch success with invalid replacement restores before drawing", [corruption](Fixture &f) {
            f.switch_corruption = corruption; f.expect_restored_failure(EFI_DEVICE_ERROR);
        });
    }
    check("ClearScreen failure prevents switch drawing and wait", [](Fixture &f) {
        f.clear_status = OP_FAILURE; REQUIRE(f.run() == OP_FAILURE);
        REQUIRE(f.clears == 1 && f.sets.empty() && f.blts == 0 && f.waits == 0);
    });
    check("ClearScreen mode drift is detected and restored", [](Fixture &f) {
        f.clear_changes_mode = true; REQUIRE(f.run() == EFI_DEVICE_ERROR);
        REQUIRE(f.sets == std::vector<UINT32>({0}) && f.blts == 0 && f.waits == 0);
        f.contains("ConsoleMode");
    });
    for (const char *message : {"Global constructor", "Hello UEFI", "[GOP] FOUND", "[GOP] ORIGINAL"}) {
        check("early normal text failure preserves status", [message](Fixture &f) {
            f.fail_text = message; REQUIRE(f.run() == TEXT_FAILURE);
            REQUIRE(f.sets.empty() && f.queries.empty() && f.blts == 0 && f.waits == 0);
        });
    }
    for (const char *message : {"[GOP] ACTIVE", "[GOP] RESOLUTION", "Press any key", "[GOP] DRAW_OK"}) {
        check("active summary prompt or marker failure restores", [message](Fixture &f) {
            f.fail_text = message;
            f.expect_restored_failure(TEXT_FAILURE, std::string(message) == "[GOP] DRAW_OK" ? 3 : 0);
        });
    }
    for (const char *message : {"Global constructor", "[GOP] ORIGINAL", "[GOP] MODE 0 "}) {
        check("original console drift stops before clear", [message](Fixture &f) {
            f.mutate_text = message; REQUIRE(f.run() == EFI_DEVICE_ERROR);
            REQUIRE(f.mutation_count == 1 && f.sets == std::vector<UINT32>({0}));
            REQUIRE(f.clears == 0 && f.blts == 0 && f.waits == 0); f.contains("ConsoleMode");
        });
    }
    for (const char *message : {"[GOP] ACTIVE", "[GOP] RESOLUTION", "Press any key", "[GOP] DRAW_OK"}) {
        check("active console drift stops next dependent operation", [message](Fixture &f) {
            f.mutate_text = message;
            f.expect_restored_failure(EFI_DEVICE_ERROR, std::string(message) == "[GOP] DRAW_OK" ? 3 : 0);
            REQUIRE(f.mutation_count == 1); f.contains("ConsoleMode");
        });
    }
    check("early text failure plus drift restores saved original", [](Fixture &f) {
        f.fail_text = "Hello UEFI"; f.mutate_text = "Hello UEFI";
        REQUIRE(f.run() == TEXT_FAILURE && f.sets == std::vector<UINT32>({0}));
    });
    for (const char *message : {"[GOP] ACTIVE", "[GOP] DRAW_OK"}) {
        check("fallback console drift restores without a preceding switch", [message](Fixture &f) {
            f.modes[1].info = Fixture::information(640, 480);
            f.mutate_text = message;
            REQUIRE(f.run() == EFI_DEVICE_ERROR && f.sets == std::vector<UINT32>({0}));
            REQUIRE(f.blts == (std::string(message) == "[GOP] DRAW_OK" ? 3U : 0U));
            REQUIRE(f.waits == 0 && f.reads == 0 && f.mode->Mode == 0);
            f.contains("ConsoleMode");
        });
    }
    for (Corruption corruption : {Corruption::null_mode, Corruption::null_info}) {
        for (const char *message : {"Global constructor", "[GOP] ACTIVE", "[GOP] DRAW_OK"}) {
            check("console invalidates Mode or Info and saved original is restored", [corruption, message](Fixture &f) {
                f.console_corruption = corruption; f.mutate_text = message;
                REQUIRE(f.run() == EFI_DEVICE_ERROR);
                if (std::string(message) == "Global constructor") REQUIRE(f.sets == std::vector<UINT32>({0}));
                else REQUIRE(f.sets == std::vector<UINT32>({1, 0}));
                REQUIRE(f.blts == (std::string(message) == "[GOP] DRAW_OK" ? 3U : 0U));
                REQUIRE(f.waits == 0 && f.reads == 0 && f.mode->Mode == 0);
                f.contains("ConsoleMode");
            });
        }
    }
    for (unsigned index = 1; index <= 3; ++index) {
        check("each BLT failure stops fills and restores", [index](Fixture &f) {
            f.fail_blt = index; f.expect_restored_failure(OP_FAILURE, index);
            REQUIRE(f.output.find("[GOP] DRAW_OK") == std::string::npos);
        });
    }
    for (bool read_failure : {false, true}) {
        check("keyboard failure preserves status and restores", [read_failure](Fixture &f) {
            if (read_failure) f.read_status = OP_FAILURE; else f.wait_status = OP_FAILURE;
            REQUIRE(f.run() == OP_FAILURE && f.sets == std::vector<UINT32>({1, 0}));
            REQUIRE(f.waits == 1 && f.reads == (read_failure ? 1U : 0U) && f.blts == 3);
        });
    }
    for (bool width_small : {false, true}) {
        check("tiny original geometry with no target rejects BLT", [width_small](Fixture &f) {
            f.fallback_only(width_small ? 4 : 1280, width_small ? 800 : 3);
            REQUIRE(f.run() == EFI_UNSUPPORTED && f.sets.empty() && f.blts == 0 && f.waits == 0);
            f.contains("Geometry");
        });
    }
    check("restore failure after main success supplies final status", [](Fixture &f) {
        f.restore_status = RESTORE_FAILURE; REQUIRE(f.run() == RESTORE_FAILURE);
        REQUIRE(f.sets == std::vector<UINT32>({1, 0}) && f.blts == 3 && f.waits == 1 && f.reads == 1);
        f.contains("RestoreMode"); f.contains("0x8000000000000009");
    });
    for (Corruption corruption : {Corruption::null_mode, Corruption::null_info, Corruption::short_info,
        Corruption::wrong_number, Corruption::wrong_width, Corruption::wrong_height, Corruption::wrong_format,
        Corruption::unsupported_version, Corruption::zero_width}) {
        check("restore success with invalid final state is device error", [corruption](Fixture &f) {
            f.restore_corruption = corruption; REQUIRE(f.run() == EFI_DEVICE_ERROR);
            REQUIRE(f.sets == std::vector<UINT32>({1, 0})); f.contains("RestoreMode");
        });
    }
    check("main error wins over distinct restore error and reports both", [](Fixture &f) {
        f.fail_blt = 2; f.restore_status = RESTORE_FAILURE; REQUIRE(f.run() == OP_FAILURE);
        REQUIRE(f.sets == std::vector<UINT32>({1, 0})); f.contains("Blt(green)"); f.contains("RestoreMode");
        f.contains("0x8000000000000001"); f.contains("0x8000000000000009");
    });
    check("main error wins over restoration validation error", [](Fixture &f) {
        f.fail_blt = 1; f.restore_corruption = Corruption::wrong_width;
        REQUIRE(f.run() == OP_FAILURE); f.contains("RestoreMode");
    });
    check("restore announcement failure still restores", [](Fixture &f) {
        f.fail_text = "[GOP] RESTORE"; REQUIRE(f.run() == TEXT_FAILURE);
        REQUIRE(f.sets == std::vector<UINT32>({1, 0}) && f.mode->Mode == 0 && f.successful_restores == 1);
    });
    check("main error wins over restore announcement failure", [](Fixture &f) {
        f.fail_blt = 1; f.fail_text = "[GOP] RESTORE";
        REQUIRE(f.run() == OP_FAILURE && f.sets == std::vector<UINT32>({1, 0}));
    });
    check("announcement error wins over failing restore", [](Fixture &f) {
        f.fail_text = "[GOP] RESTORE"; f.restore_status = RESTORE_FAILURE;
        REQUIRE(f.run() == TEXT_FAILURE); f.contains("RestoreMode");
    });
    check("error reporter failure cannot overwrite operation status", [](Fixture &f) {
        f.fail_blt = 1; f.restore_status = RESTORE_FAILURE; f.error_text_fails = true;
        REQUIRE(f.run() == OP_FAILURE); f.contains("Blt(red)"); f.contains("RestoreMode");
    });
    check("startup success restores before fini", [](Fixture &f) { f.expect_success(1, true); });
    check("startup no GOP preserves status and calls fini", [](Fixture &f) {
        f.locate_status = EFI_NOT_FOUND; REQUIRE(f.run(true) == EFI_NOT_FOUND && f.sets.empty());
    });
    check("startup fatal enumeration calls fini", [](Fixture &f) {
        f.free_status = OP_FAILURE; REQUIRE(f.run(true) == OP_FAILURE && f.sets.empty());
    });
    check("startup BLT error restores before fini", [](Fixture &f) { f.fail_blt = 2; REQUIRE(f.run(true) == OP_FAILURE); });
    check("startup keyboard error restores before fini", [](Fixture &f) { f.read_status = OP_FAILURE; REQUIRE(f.run(true) == OP_FAILURE); });
    check("startup restoration error preserves status before fini", [](Fixture &f) {
        f.restore_status = RESTORE_FAILURE; REQUIRE(f.run(true) == RESTORE_FAILURE);
    });
    std::printf("GOP host checks passed: %u cases (real application and startup contract).\n", cases_run);
}
