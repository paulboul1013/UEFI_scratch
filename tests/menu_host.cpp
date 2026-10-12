// Run the real menu and station entry with a cell-based fake text console.
// No libc wchar routines: the EFI target and these tests use 16-bit wchar_t.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../menu.hpp"
#define _start menu_test_startup
#include "../startup.cpp"
#undef _start

namespace {
const char *case_name;
unsigned cases_run;
constexpr EFI_STATUS OP_FAILURE = (1ULL << 63) | 1;
constexpr EFI_STATUS TEXT_FAILURE = (1ULL << 63) | 5;
constexpr EFI_STATUS RESTORE_FAILURE = (1ULL << 63) | 9;
constexpr INT32 ORIGINAL_ATTRIBUTE = 0x4e;
constexpr INT32 ORIGINAL_COLUMN = 3;
constexpr INT32 ORIGINAL_ROW = 2;

[[noreturn]] void fail(const char *condition, unsigned line)
{
    std::fprintf(stderr, "FAIL [%s] line %u: %s\n", case_name, line, condition);
    std::abort();
}
#define REQUIRE(condition) do { if (!(condition)) fail(#condition, __LINE__); } while (0)

const char *const items[] = {
    "Select Language", "Device Manager", "Boot Manager", "Boot Maintenance Manager",
    "Continue", "Reset", "Return to Firmware"
};

std::string ascii(const CHAR16 *text)
{
    std::string result;
    if (text) for (; *text; ++text) result += *text < 128 ? static_cast<char>(*text) : '?';
    return result;
}

std::string without_spaces(const std::string &text)
{
    std::string result;
    for (char ch : text) if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t' && ch != '|') result += ch;
    return result;
}

struct Snapshot {
    UINTN columns;
    std::string cells;
    std::vector<UINTN> attributes;
    unsigned output_calls;
    unsigned clears;

    std::string text() const
    {
        std::string result;
        for (size_t offset = 0; offset < cells.size(); offset += static_cast<size_t>(columns)) {
            result += cells.substr(offset, static_cast<size_t>(columns));
            result += '\n';
        }
        return result;
    }
    void contains(const std::string &fragment) const
    {
        if (without_spaces(text()).find(without_spaces(fragment)) == std::string::npos) {
            std::fprintf(stderr, "Screen for [%s]:\n%s\n", case_name, text().c_str());
            fail(fragment.c_str(), __LINE__);
        }
    }
    void excludes(const std::string &fragment) const
    {
        REQUIRE(without_spaces(text()).find(without_spaces(fragment)) == std::string::npos);
    }
    unsigned selected() const
    {
        unsigned selected_item = 7;
        for (unsigned item = 0; item < 7; ++item) {
            size_t offset = cells.find(items[item]);
            while (offset != std::string::npos) {
                const size_t end = offset + std::strlen(items[item]);
                if (end <= attributes.size() && std::all_of(attributes.begin() + static_cast<long>(offset),
                        attributes.begin() + static_cast<long>(end), [](UINTN attr) { return attr == EFI_WHITE; })) {
                    REQUIRE(selected_item == 7);
                    selected_item = item;
                }
                offset = cells.find(items[item], offset + 1);
            }
        }
        REQUIRE(selected_item < 7);
        return selected_item;
    }
};

struct Input {
    EFI_INPUT_KEY key;
    EFI_STATUS status = EFI_SUCCESS;
};
struct Fixture;
Fixture *active;

struct Fixture {
    EFI_SIMPLE_TEXT_OUTPUT_MODE text_mode {};
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console {};
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL keyboard {};
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info {};
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE graphics_mode {};
    EFI_GRAPHICS_OUTPUT_PROTOCOL gop {};
    EFI_BOOT_SERVICES boot {};
    EFI_RUNTIME_SERVICES runtime {};
    EFI_SYSTEM_TABLE table {};
    UINTN columns = 80, rows = 25;
    std::string cells;
    std::vector<UINTN> attributes;
    std::vector<Input> inputs;
    std::vector<Snapshot> snapshots;
    std::vector<std::string> events;
    std::string output;
    EFI_STATUS query_status = EFI_SUCCESS, locate_status = EFI_SUCCESS;
    EFI_STATUS watchdog_status = EFI_SUCCESS;
    void *located_interface = &gop;
    unsigned waits = 0, reads = 0, clears = 0, output_calls = 0;
    unsigned cursor_calls = 0, attribute_calls = 0, visibility_calls = 0;
    unsigned fail_wait_at = 0, fail_output_at = 0, fail_clear_at = 0;
    unsigned fail_outputs_from = 0;
    unsigned fail_cursor_at = 0, fail_attribute_at = 0, fail_visibility_at = 0;
    unsigned reset_calls = 0, init_calls = 0, fini_calls = 0;
    unsigned watchdog_calls = 0;
    unsigned restored_attributes = 0, restored_positions = 0, restored_visibility = 0;
    bool fail_restore_attribute = false, fail_restore_position = false, fail_restore_visibility = false;
    bool cursor_unsupported = false, failure_issued = false, last_key_read = false;
    bool mutate_before_failure = false;
    bool fail_output_after_input = false;
    bool unexpected_wait_index = false;

    Fixture()
    {
        REQUIRE(active == nullptr);
        active = this;
        geometry(80, 25);
        text_mode.MaxMode = 1;
        text_mode.Mode = 0;
        text_mode.Attribute = ORIGINAL_ATTRIBUTE;
        text_mode.CursorColumn = ORIGINAL_COLUMN;
        text_mode.CursorRow = ORIGINAL_ROW;
        text_mode.CursorVisible = 1;
        console.Mode = &text_mode;
        console.OutputString = write;
        console.QueryMode = query;
        console.SetAttribute = set_attribute;
        console.SetCursorPosition = set_cursor;
        console.EnableCursor = enable_cursor;
        console.ClearScreen = clear;
        console.SetMode = forbidden_text_mode;
        keyboard.ReadKeyStroke = read;
        keyboard.WaitForKey = reinterpret_cast<EFI_EVENT>(0x1234);
        boot.WaitForEvent = wait;
        boot.LocateProtocol = locate;
        boot.SetWatchdogTimer = watchdog;
        boot.LoadImage = forbidden;
        boot.StartImage = forbidden;
        boot.ExitBootServices = forbidden;
        runtime.GetVariable = forbidden;
        runtime.SetVariable = forbidden;
        runtime.ResetSystem = reset;
        info.HorizontalResolution = 1366;
        info.VerticalResolution = 768;
        info.PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
        info.PixelsPerScanLine = 1408;
        graphics_mode.MaxMode = 5;
        graphics_mode.Mode = 3;
        graphics_mode.Info = &info;
        graphics_mode.SizeOfInfo = sizeof(info);
        graphics_mode.FrameBufferBase = 0x12345678;
        graphics_mode.FrameBufferSize = 1408 * 768 * 4;
        gop.Mode = &graphics_mode;
        gop.QueryMode = forbidden_gop_query;
        gop.SetMode = forbidden_gop_set;
        gop.Blt = forbidden_gop_blt;
        table.FirmwareVendor = const_cast<CHAR16 *>(L"FixtureVendor-X");
        table.FirmwareRevision = 0xa1b2c3d4;
        table.Hdr.Revision = 0x00020050;
        table.ConOut = &console;
        table.ConIn = &keyboard;
        table.BootServices = &boot;
        table.RuntimeServices = &runtime;
    }
    ~Fixture() { active = nullptr; }
    void geometry(UINTN width, UINTN height)
    {
        columns = width;
        rows = height;
        cells.assign(static_cast<size_t>(width * height), ' ');
        attributes.assign(cells.size(), ORIGINAL_ATTRIBUTE);
    }
    void key(UINT16 scan, CHAR16 character = 0, EFI_STATUS status = EFI_SUCCESS)
    {
        inputs.push_back({{scan, character}, status});
    }
    void down(unsigned count = 1) { while (count--) key(SCAN_DOWN); }
    void up(unsigned count = 1) { while (count--) key(SCAN_UP); }
    void enter() { key(0, L'\r'); }
    void escape() { key(SCAN_ESC); }
    EFI_STATUS run(bool startup = false)
    {
        if (inputs.empty()) escape();
        const EFI_STATUS result = startup ? menu_test_startup(nullptr, &table) : menu_run(&table);
        if (startup) {
            REQUIRE(init_calls == 1 && fini_calls == 1);
            REQUIRE(events.front() == "init" && events.back() == "fini");
            const auto restored = std::find(events.begin(), events.end(), "restore-attribute");
            REQUIRE(restored != events.end() && restored < events.end() - 1);
        }
        return result;
    }
    void restored() const
    {
        REQUIRE(text_mode.Attribute == ORIGINAL_ATTRIBUTE);
        REQUIRE(text_mode.CursorColumn == ORIGINAL_COLUMN && text_mode.CursorRow == ORIGINAL_ROW);
        REQUIRE(text_mode.CursorVisible == 1);
    }
    void contains_output(const std::string &fragment) const
    {
        if (without_spaces(output).find(without_spaces(fragment)) == std::string::npos) {
            std::fprintf(stderr, "Output for [%s]:\n%s\n", case_name, output.c_str());
            fail(fragment.c_str(), __LINE__);
        }
    }
    static void EFIAPI forbidden() { REQUIRE(false); }
    static EFI_STATUS EFIAPI forbidden_text_mode(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN)
    { REQUIRE(false); return EFI_UNSUPPORTED; }
    static EFI_STATUS EFIAPI forbidden_gop_query(EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32, UINTN *,
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **)
    { REQUIRE(false); return EFI_UNSUPPORTED; }
    static EFI_STATUS EFIAPI forbidden_gop_set(EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32)
    { REQUIRE(false); return EFI_UNSUPPORTED; }
    static EFI_STATUS EFIAPI forbidden_gop_blt(EFI_GRAPHICS_OUTPUT_PROTOCOL *, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *,
        EFI_GRAPHICS_OUTPUT_BLT_OPERATION, UINTN, UINTN, UINTN, UINTN, UINTN, UINTN, UINTN)
    { REQUIRE(false); return EFI_UNSUPPORTED; }
    static EFI_STATUS EFIAPI locate(EFI_GUID *guid, void *registration, void **interface)
    {
        auto &f = *active;
        const EFI_GUID expected = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
        REQUIRE(guid && std::memcmp(guid, &expected, sizeof(expected)) == 0);
        REQUIRE(registration == nullptr && interface && *interface == nullptr);
        *interface = f.located_interface;
        return f.locate_status;
    }
    static EFI_STATUS EFIAPI watchdog(UINTN timeout, UINT64 code, UINTN size, CHAR16 *data)
    {
        auto &f = *active;
        REQUIRE(timeout == 0 && code == 0 && size == 0 && data == nullptr);
        REQUIRE(f.watchdog_calls == 0 && f.waits == 0);
        ++f.watchdog_calls;
        f.events.emplace_back("disable-watchdog");
        if ((f.watchdog_status & (1ULL << 63)) && f.watchdog_status != EFI_UNSUPPORTED)
            f.failure_issued = true;
        return f.watchdog_status;
    }
    static EFI_STATUS EFIAPI query(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, UINTN mode, UINTN *width, UINTN *height)
    {
        auto &f = *active;
        REQUIRE(self == &f.console && mode == 0 && width && height);
        *width = f.columns;
        *height = f.rows;
        return f.query_status;
    }
    static EFI_STATUS EFIAPI set_attribute(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, UINTN attribute)
    {
        auto &f = *active;
        REQUIRE(self == &f.console);
        ++f.attribute_calls;
        if (attribute == ORIGINAL_ATTRIBUTE) {
            ++f.restored_attributes;
            f.events.emplace_back("restore-attribute");
            if (f.fail_restore_attribute) return RESTORE_FAILURE;
        }
        if (f.attribute_calls == f.fail_attribute_at) {
            if (f.mutate_before_failure) f.text_mode.Attribute = static_cast<INT32>(attribute);
            f.failure_issued = true;
            return OP_FAILURE;
        }
        f.text_mode.Attribute = static_cast<INT32>(attribute);
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI set_cursor(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, UINTN column, UINTN row)
    {
        auto &f = *active;
        REQUIRE(self == &f.console && column < f.columns && row < f.rows);
        ++f.cursor_calls;
        if ((f.last_key_read || f.failure_issued) && column == ORIGINAL_COLUMN && row == ORIGINAL_ROW) {
            ++f.restored_positions;
            if (f.fail_restore_position) return RESTORE_FAILURE;
        }
        if (f.cursor_calls == f.fail_cursor_at) {
            if (f.mutate_before_failure) {
                f.text_mode.CursorColumn = static_cast<INT32>(column);
                f.text_mode.CursorRow = static_cast<INT32>(row);
            }
            f.failure_issued = true;
            return OP_FAILURE;
        }
        f.text_mode.CursorColumn = static_cast<INT32>(column);
        f.text_mode.CursorRow = static_cast<INT32>(row);
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI enable_cursor(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, BOOLEAN visible)
    {
        auto &f = *active;
        REQUIRE(self == &f.console && visible <= 1);
        ++f.visibility_calls;
        if (f.cursor_unsupported) return EFI_UNSUPPORTED;
        if (visible == 1) {
            ++f.restored_visibility;
            if (f.fail_restore_visibility) return RESTORE_FAILURE;
        }
        if (f.visibility_calls == f.fail_visibility_at) {
            if (f.mutate_before_failure) f.text_mode.CursorVisible = visible;
            f.failure_issued = true;
            return OP_FAILURE;
        }
        f.text_mode.CursorVisible = visible;
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI clear(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self)
    {
        auto &f = *active;
        REQUIRE(self == &f.console);
        ++f.clears;
        if (f.clears == f.fail_clear_at) {
            if (f.mutate_before_failure) {
                f.text_mode.CursorColumn = 0;
                f.text_mode.CursorRow = 0;
            }
            f.failure_issued = true;
            return OP_FAILURE;
        }
        f.cells.assign(f.cells.size(), ' ');
        f.attributes.assign(f.attributes.size(), static_cast<UINTN>(f.text_mode.Attribute));
        f.text_mode.CursorColumn = 0;
        f.text_mode.CursorRow = 0;
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI write(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, CHAR16 *text)
    {
        auto &f = *active;
        REQUIRE(self == &f.console && text);
        ++f.output_calls;
        f.output += ascii(text);
        if (f.output_calls == f.fail_output_at || (f.fail_outputs_from && f.output_calls >= f.fail_outputs_from)) {
            if (f.mutate_before_failure) {
                f.text_mode.CursorColumn = 0;
                f.text_mode.CursorRow = 0;
            }
            f.failure_issued = true;
            return TEXT_FAILURE;
        }
        for (; *text; ++text) {
            if (*text == L'\r') { f.text_mode.CursorColumn = 0; continue; }
            if (*text == L'\n') {
                REQUIRE(static_cast<UINTN>(f.text_mode.CursorRow + 1) < f.rows);
                ++f.text_mode.CursorRow;
                continue;
            }
            REQUIRE(f.text_mode.CursorColumn >= 0 && f.text_mode.CursorRow >= 0);
            const UINTN column = static_cast<UINTN>(f.text_mode.CursorColumn);
            const UINTN row = static_cast<UINTN>(f.text_mode.CursorRow);
            REQUIRE(column < f.columns && row < f.rows);
            const size_t cell = static_cast<size_t>(row * f.columns + column);
            // Writing this cell causes automatic scrolling on some firmware.
            REQUIRE(cell + 1 < f.cells.size());
            f.cells[cell] = *text < 128 ? static_cast<char>(*text) : '?';
            f.attributes[cell] = static_cast<UINTN>(f.text_mode.Attribute);
            ++f.text_mode.CursorColumn;
            if (static_cast<UINTN>(f.text_mode.CursorColumn) == f.columns) {
                f.text_mode.CursorColumn = 0;
                ++f.text_mode.CursorRow;
            }
        }
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI wait(UINTN count, EFI_EVENT *events, UINTN *index)
    {
        auto &f = *active;
        REQUIRE(count == 1 && events && events[0] == f.keyboard.WaitForKey && index);
        REQUIRE(f.watchdog_calls == 1);
        ++f.waits;
        f.snapshots.push_back({f.columns, f.cells, f.attributes, f.output_calls, f.clears});
        REQUIRE(f.waits <= f.inputs.size() + 1); // An unhandled key cannot hang the host test.
        if (f.waits == f.fail_wait_at) { f.failure_issued = true; return OP_FAILURE; }
        *index = f.unexpected_wait_index ? 1 : 0;
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI read(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *self, EFI_INPUT_KEY *key)
    {
        auto &f = *active;
        REQUIRE(self == &f.keyboard && key && f.reads < f.inputs.size());
        const auto input = f.inputs[f.reads++];
        *key = input.key;
        if ((input.status & (1ULL << 63)) && input.status != EFI_NOT_READY) {
            f.failure_issued = true;
            if (f.fail_output_after_input) f.fail_outputs_from = f.output_calls + 1;
        }
        f.last_key_read = f.reads == f.inputs.size();
        return input.status;
    }
    static void EFIAPI reset(EFI_RESET_TYPE type, EFI_STATUS status, UINTN size, void *data)
    {
        auto &f = *active;
        REQUIRE(type == EfiResetCold && status == EFI_SUCCESS && size == 0 && data == nullptr);
        ++f.reset_calls;
        f.events.emplace_back("reset-returned");
    }
};

template<typename Work> void check(const char *name, Work work)
{
    case_name = name;
    Fixture fixture;
    work(fixture);
    ++cases_run;
}
} // namespace

void runtime_init() { ++active->init_calls; active->events.emplace_back("init"); }
void runtime_fini() { ++active->fini_calls; active->events.emplace_back("fini"); }

int main()
{
    check("main menu renders seven entries and exits on Esc", [](Fixture &f) {
        REQUIRE(f.run() == EFI_SUCCESS);
        REQUIRE(f.snapshots.size() == 1 && f.snapshots[0].selected() == 0);
        for (const char *item : items) f.snapshots[0].contains(item);
        f.snapshots[0].contains("UEFI Station 0.1");
        f.snapshots[0].contains("FixtureVendor-X");
        f.snapshots[0].contains("2.80");
        f.snapshots[0].contains("80x25");
        f.restored();
    });
    check("Up at the first entry stops and unknown input does not redraw", [](Fixture &f) {
        f.up(); f.key(0, L'x'); f.key(0x04); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.snapshots.size() == 4);
        for (const auto &screen : f.snapshots) REQUIRE(screen.selected() == 0);
        REQUIRE(f.snapshots.front().output_calls == f.snapshots.back().output_calls);
        REQUIRE(f.snapshots.front().clears == f.snapshots.back().clears);
        f.restored();
    });
    check("navigation stops at the last entry and each description follows selection", [](Fixture &f) {
        f.down(8); f.up(6); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.snapshots.size() == 15);
        const char *descriptions[] = {"Select the language", "View firmware", "Choose an entry",
            "View boot settings", "Boot the default", "Restart the machine", "Exit this application"};
        for (unsigned item = 0; item < 7; ++item) {
            REQUIRE(f.snapshots[item].selected() == item);
            f.snapshots[item].contains(descriptions[item]);
            for (unsigned other = 0; other < 7; ++other)
                if (other != item) f.snapshots[item].excludes(descriptions[other]);
        }
        REQUIRE(f.snapshots[7].selected() == 6 && f.snapshots[8].selected() == 6);
        REQUIRE(f.snapshots.back().selected() == 0);
        f.restored();
    });
    for (unsigned item = 0; item < 5; ++item) {
        check("all information pages return to their selected item", [item](Fixture &f) {
            f.down(item); f.enter(); f.escape(); f.escape();
            REQUIRE(f.run() == EFI_SUCCESS);
            const auto &page = f.snapshots[item + 1];
            const char *messages[] = {"English", "FixtureVendor-X", "Not available yet",
                "Default Boot Entry: Not configured", "No default boot entry configured"};
            page.contains(messages[item]);
            REQUIRE(f.snapshots.back().selected() == item);
            REQUIRE(f.reset_calls == 0);
            f.restored();
        });
    }
    check("Enter on Return to Firmware exits without another wait", [](Fixture &f) {
        f.down(6); f.enter();
        REQUIRE(f.run() == EFI_SUCCESS && f.waits == 7 && f.reads == 7);
        f.restored();
    });
    check("continuous page visits preserve each selection", [](Fixture &f) {
        for (unsigned item = 0; item < 5; ++item) { if (item) f.down(); f.enter(); f.escape(); }
        f.escape();
        REQUIRE(f.run() == EFI_SUCCESS);
        REQUIRE(f.snapshots.back().selected() == 4 && f.reset_calls == 0);
        f.restored();
    });
    check("Device Manager uses actual firmware revisions and GOP values", [](Fixture &f) {
        f.down(); f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS);
        const auto &page = f.snapshots[2];
        page.contains("FixtureVendor-X"); page.contains("2.80"); page.contains("A1B2C3D4");
        page.contains("80x25"); page.contains("1366x768"); page.contains("3");
        page.contains("Pixel Format: BGR");
        f.restored();
    });
    for (unsigned corruption = 0; corruption < 11; ++corruption) {
        check("missing or malformed GOP leaves the text menu usable", [corruption](Fixture &f) {
            switch (corruption) {
            case 0: f.locate_status = EFI_NOT_FOUND; break;
            case 1: f.located_interface = nullptr; break;
            case 2: f.gop.Mode = nullptr; break;
            case 3: f.graphics_mode.Info = nullptr; break;
            case 4: f.graphics_mode.SizeOfInfo = sizeof(f.info) - 1; break;
            case 5: f.graphics_mode.MaxMode = 0; break;
            case 6: f.graphics_mode.Mode = f.graphics_mode.MaxMode; break;
            case 7: f.info.Version = 1; break;
            case 8: f.info.HorizontalResolution = 0; break;
            case 9: f.info.PixelFormat = PixelFormatMax; break;
            case 10: f.info.VerticalResolution = 0; break;
            }
            f.down(); f.enter(); f.escape(); f.escape();
            REQUIRE(f.run() == EFI_SUCCESS);
            f.snapshots[0].contains("Graphics: unavailable");
            f.snapshots[2].contains("Graphics unavailable");
            f.restored();
        });
    }
    check("missing LocateProtocol and RuntimeServices remain usable", [](Fixture &f) {
        f.boot.LocateProtocol = nullptr; f.table.RuntimeServices = nullptr;
        REQUIRE(f.run() == EFI_SUCCESS);
        f.snapshots[0].contains("Graphics: unavailable");
        f.restored();
    });
    for (unsigned format = 0; format < PixelFormatMax; ++format) {
        check("Device Manager identifies all valid GOP pixel formats", [format](Fixture &f) {
            f.info.PixelFormat = static_cast<EFI_GRAPHICS_PIXEL_FORMAT>(format);
            f.down(); f.enter(); f.escape(); f.escape();
            REQUIRE(f.run() == EFI_SUCCESS);
            const char *formats[] = {"RGB", "BGR", "BitMask", "BltOnly"};
            f.snapshots[2].contains(std::string("Pixel Format: ") + formats[format]);
            f.restored();
        });
    }
    check("Reset defaults to Cancel so Enter cannot restart", [](Fixture &f) {
        f.down(5); f.enter(); f.enter(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.reset_calls == 0);
        f.snapshots[6].contains("Cancel"); f.snapshots[6].contains("Restart");
        REQUIRE(f.snapshots.back().selected() == 5);
        f.restored();
    });
    check("Esc cancels Reset even when Restart is selected", [](Fixture &f) {
        f.down(5); f.enter(); f.down(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.reset_calls == 0);
        REQUIRE(f.snapshots.back().selected() == 5);
        f.restored();
    });
    check("Reset cursor stops at boundaries and Cancel stays safe", [](Fixture &f) {
        f.down(5); f.enter(); f.up(3); f.enter(); f.enter(); f.down(3); f.up(3); f.enter(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.reset_calls == 0);
        REQUIRE(f.snapshots.back().selected() == 5);
        f.restored();
    });
    for (bool missing_table : {false, true}) {
        check("missing reset method reports unavailable and permits return", [missing_table](Fixture &f) {
            if (missing_table) f.table.RuntimeServices = nullptr; else f.runtime.ResetSystem = nullptr;
            f.down(5); f.enter(); f.down(); f.enter(); f.escape(); f.escape();
            REQUIRE(f.run() == EFI_SUCCESS && f.reset_calls == 0);
            f.contains_output("Restart unavailable");
            f.restored();
        });
    }
    check("only confirmed Restart calls cold reset and unexpected return is an error", [](Fixture &f) {
        f.down(5); f.enter(); f.down(); f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_DEVICE_ERROR && f.reset_calls == 1);
        f.contains_output("ResetSystem");
        f.restored();
    });
    check("reset confirmation defaults to Cancel again after returning and reopening", [](Fixture &f) {
        f.down(5); f.enter(); f.down(); f.escape(); f.enter(); f.enter(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.reset_calls == 0);
        f.restored();
    });
    check("earliest reset return error wins over later input and cleanup failures", [](Fixture &f) {
        f.down(5); f.enter(); f.down(); f.enter(); f.key(0, 0, OP_FAILURE);
        f.fail_restore_attribute = f.fail_restore_position = f.fail_restore_visibility = true;
        REQUIRE(f.run() == EFI_DEVICE_ERROR && f.reset_calls == 1);
        REQUIRE(f.restored_attributes >= 1 && f.restored_positions >= 1 && f.restored_visibility >= 1);
    });
    for (const auto &geometry : {std::pair<UINTN, UINTN>{40, 15}, {79, 24}, {80, 15}, {120, 40}}) {
        check("supported geometry clips safely and permits every page", [geometry](Fixture &f) {
            f.geometry(geometry.first, geometry.second);
            for (unsigned item = 0; item < 6; ++item) { if (item) f.down(); f.enter(); f.escape(); }
            f.escape();
            REQUIRE(f.run() == EFI_SUCCESS);
            for (const char *item : items) f.snapshots[0].contains(item);
            REQUIRE(f.snapshots.back().selected() == 5);
            f.restored();
        });
    }
    check("long firmware vendor is clipped without writing the final screen cell", [](Fixture &f) {
        std::vector<CHAR16> vendor(400, L'W'); vendor.back() = 0;
        f.table.FirmwareVendor = vendor.data(); f.geometry(40, 15);
        f.down(); f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS);
        f.snapshots[2].contains("WWWWWWWWWWWWWWWW");
        f.restored();
    });
    for (const auto &geometry : {std::pair<UINTN, UINTN>{39, 15}, {40, 14}, {8, 4}}) {
        check("too-small text mode emits a short error and returns", [geometry](Fixture &f) {
            f.geometry(geometry.first, geometry.second);
            REQUIRE(f.run() == EFI_UNSUPPORTED && f.waits == 0 && f.reads == 0);
            REQUIRE(!f.output.empty());
            f.restored();
        });
    }
    check("NOT_READY returns to waiting without redrawing or changing selection", [](Fixture &f) {
        f.key(0, 0, EFI_NOT_READY); f.down(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.waits == 3 && f.reads == 3);
        REQUIRE(f.snapshots[0].selected() == 0 && f.snapshots[1].selected() == 0);
        REQUIRE(f.snapshots[0].output_calls == f.snapshots[1].output_calls);
        REQUIRE(f.snapshots[2].selected() == 1);
        f.restored();
    });
    check("unknown and navigation keys on information pages do not perform actions or redraw", [](Fixture &f) {
        f.enter(); f.down(); f.up(); f.enter(); f.key(SCAN_DOWN, L'x'); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.reset_calls == 0);
        REQUIRE(f.snapshots[1].output_calls == f.snapshots[5].output_calls);
        REQUIRE(f.snapshots.back().selected() == 0);
        f.restored();
    });
    for (bool read_failure : {false, true}) {
        check("input error preserves the primary status and restores console", [read_failure](Fixture &f) {
            if (read_failure) f.key(0, 0, OP_FAILURE); else f.fail_wait_at = 1;
            REQUIRE(f.run() == OP_FAILURE);
            REQUIRE(f.waits == 1 && f.reads == (read_failure ? 1U : 0U));
            f.restored();
        });
    }
    check("invalid WaitForEvent result is rejected before ReadKeyStroke", [](Fixture &f) {
        f.unexpected_wait_index = true;
        REQUIRE(f.run() == EFI_DEVICE_ERROR && f.reads == 0);
        f.restored();
    });
    for (bool mutation : {false, true}) for (unsigned operation = 0; operation < 5; ++operation) {
        check("console operation failure restores settings even after partial mutation", [operation, mutation](Fixture &f) {
            f.mutate_before_failure = mutation;
            switch (operation) {
            case 0: f.fail_output_at = 2; break;
            case 1: f.fail_clear_at = 1; break;
            case 2: f.fail_cursor_at = 2; break;
            case 3: f.fail_attribute_at = 2; break;
            case 4: f.fail_visibility_at = 1; break;
            }
            REQUIRE(f.run() == (operation == 0 ? TEXT_FAILURE : OP_FAILURE));
            f.restored();
        });
    }
    for (unsigned operation = 0; operation < 3; ++operation) {
        check("restoration failure supplies status after successful application", [operation](Fixture &f) {
            if (operation == 0) f.fail_restore_attribute = true;
            if (operation == 1) f.fail_restore_position = true;
            if (operation == 2) f.fail_restore_visibility = true;
            REQUIRE(f.run() == RESTORE_FAILURE);
            REQUIRE(f.restored_attributes >= 1 && f.restored_positions >= 1 && f.restored_visibility >= 1);
            if (operation != 0) REQUIRE(f.text_mode.Attribute == ORIGINAL_ATTRIBUTE);
            if (operation != 1) REQUIRE(f.text_mode.CursorColumn == ORIGINAL_COLUMN && f.text_mode.CursorRow == ORIGINAL_ROW);
            if (operation != 2) REQUIRE(f.text_mode.CursorVisible == 1);
        });
    }
    check("cleanup failures cannot replace an input failure", [](Fixture &f) {
        f.key(0, 0, OP_FAILURE);
        f.fail_restore_attribute = f.fail_restore_position = f.fail_restore_visibility = true;
        REQUIRE(f.run() == OP_FAILURE);
        REQUIRE(f.restored_attributes >= 1 && f.restored_positions >= 1 && f.restored_visibility >= 1);
    });
    check("cleanup and error-report failures cannot replace an output failure", [](Fixture &f) {
        f.fail_outputs_from = 2;
        f.fail_restore_attribute = f.fail_restore_position = f.fail_restore_visibility = true;
        REQUIRE(f.run() == TEXT_FAILURE);
        REQUIRE(f.restored_attributes >= 1 && f.restored_positions >= 1 && f.restored_visibility >= 1);
    });
    check("failure to display an input error cannot replace that input status", [](Fixture &f) {
        f.key(0, 0, OP_FAILURE);
        f.fail_output_after_input = true;
        f.fail_restore_attribute = true;
        REQUIRE(f.run() == OP_FAILURE);
        REQUIRE(f.restored_positions >= 1 && f.restored_visibility >= 1);
    });
    check("unsupported EnableCursor keeps firmware visibility and does not fail", [](Fixture &f) {
        f.cursor_unsupported = true;
        REQUIRE(f.run() == EFI_SUCCESS);
        f.restored();
    });
    check("absent EnableCursor retains firmware visibility", [](Fixture &f) {
        f.console.EnableCursor = nullptr;
        REQUIRE(f.run() == EFI_SUCCESS);
        f.restored();
    });
    check("QueryMode error prevents UI input and preserves its status", [](Fixture &f) {
        f.query_status = OP_FAILURE;
        REQUIRE(f.run() == OP_FAILURE && f.waits == 0);
        f.restored();
    });
    check("QueryMode and ReadKeyStroke warnings still permit navigation", [](Fixture &f) {
        f.query_status = 1;
        f.key(SCAN_ESC, 0, 1);
        REQUIRE(f.run() == EFI_SUCCESS);
        f.restored();
    });
    check("boot watchdog is disabled before waiting for user input", [](Fixture &f) {
        REQUIRE(f.run() == EFI_SUCCESS && f.watchdog_calls == 1);
        f.restored();
    });
    check("firmware without a supported watchdog keeps menu usable", [](Fixture &f) {
        f.watchdog_status = EFI_UNSUPPORTED;
        REQUIRE(f.run() == EFI_SUCCESS && f.watchdog_calls == 1);
        f.restored();
    });
    check("watchdog failure is returned without UI input and restores settings", [](Fixture &f) {
        f.watchdog_status = OP_FAILURE;
        REQUIRE(f.run() == OP_FAILURE && f.waits == 0 && f.watchdog_calls == 1);
        REQUIRE(!f.output.empty());
        f.restored();
    });
    check("watchdog failure wins over restoration failure", [](Fixture &f) {
        f.watchdog_status = OP_FAILURE;
        f.fail_restore_attribute = f.fail_restore_position = f.fail_restore_visibility = true;
        REQUIRE(f.run() == OP_FAILURE && f.waits == 0 && f.watchdog_calls == 1);
    });
    for (unsigned missing = 0; missing < 13; ++missing) {
        check("required console and input services are validated", [missing](Fixture &f) {
            switch (missing) {
            case 0: f.table.ConOut = nullptr; break;
            case 1: f.console.Mode = nullptr; break;
            case 2: f.console.QueryMode = nullptr; break;
            case 3: f.console.SetAttribute = nullptr; break;
            case 4: f.console.SetCursorPosition = nullptr; break;
            case 5: f.console.OutputString = nullptr; break;
            case 6: f.table.ConIn = nullptr; break;
            case 7: f.keyboard.ReadKeyStroke = nullptr; break;
            case 8: f.boot.WaitForEvent = nullptr; break;
            case 9: f.console.ClearScreen = nullptr; break;
            case 10: f.keyboard.WaitForKey = nullptr; break;
            case 11: f.table.BootServices = nullptr; break;
            case 12: f.boot.SetWatchdogTimer = nullptr; break;
            }
            REQUIRE(f.run() != EFI_SUCCESS && f.waits == 0);
        });
    }
    check("null system table fails without firmware access", [](Fixture &) {
        REQUIRE(menu_run(nullptr) == EFI_INVALID_PARAMETER);
    });
    for (unsigned invalid = 0; invalid < 5; ++invalid) {
        check("invalid saved console state is rejected before drawing", [invalid](Fixture &f) {
            switch (invalid) {
            case 0: f.text_mode.Mode = -1; break;
            case 1: f.text_mode.Mode = 1; break;
            case 2: f.text_mode.CursorColumn = -1; break;
            case 3: f.text_mode.CursorColumn = static_cast<INT32>(f.columns); break;
            case 4: f.text_mode.CursorRow = static_cast<INT32>(f.rows); break;
            }
            REQUIRE(f.run() == EFI_UNSUPPORTED && f.waits == 0 && f.output_calls == 0);
        });
    }
    check("startup performs init and cleanup after console restoration", [](Fixture &f) {
        REQUIRE(f.run(true) == EFI_SUCCESS);
        f.restored();
    });
    check("startup preserves error and still finalizes runtime", [](Fixture &f) {
        f.key(0, 0, OP_FAILURE);
        REQUIRE(f.run(true) == OP_FAILURE);
        f.restored();
    });
    std::printf("Menu host checks passed: %u cases (real menu, station, and startup contract).\n", cases_run);
}
