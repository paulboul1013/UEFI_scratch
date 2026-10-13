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
    INT32 text_mode;
    UINT32 graphics_mode;

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
    bool override_child_test = false;
    EFI_STATUS child_test_status = EFI_SUCCESS;
};
struct Fixture;
Fixture *active;

struct Fixture {
    EFI_SIMPLE_TEXT_OUTPUT_MODE text_mode {};
    EFI_SIMPLE_TEXT_OUTPUT_MODE child_text_mode {};
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console {};
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL child_console {};
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL keyboard {};
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info {};
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE graphics_mode {};
    EFI_GRAPHICS_OUTPUT_PROTOCOL gop {};
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION child_graphics_info {};
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE child_graphics_mode {};
    EFI_BOOT_SERVICES boot {};
    EFI_RUNTIME_SERVICES runtime {};
    EFI_SYSTEM_TABLE table {};
    EFI_LOADED_IMAGE_PROTOCOL parent_image {}, child_image {};
    std::vector<UINT8> parent_path;
    UINT8 source_path[8] {1, 1, 4, 0, 0x7f, 0xff, 4, 0};
    UINTN columns = 80, rows = 25;
    UINTN menu_columns = 80, menu_rows = 25;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION menu_graphics_info {};
    std::string cells;
    std::vector<UINTN> attributes;
    std::vector<Input> inputs;
    std::vector<Snapshot> snapshots;
    std::vector<std::string> events;
    std::string output;
    EFI_STATUS query_status = EFI_SUCCESS, locate_status = EFI_SUCCESS;
    EFI_STATUS watchdog_status = EFI_SUCCESS;
    EFI_STATUS arm_watchdog_status = EFI_SUCCESS, return_watchdog_status = EFI_SUCCESS;
    EFI_STATUS load_status = EFI_SUCCESS, start_status = EFI_SUCCESS;
    EFI_STATUS unload_status = RESTORE_FAILURE, text_restore_status = EFI_SUCCESS;
    EFI_STATUS gop_restore_status = EFI_SUCCESS, child_test_status = EFI_SUCCESS;
    void *located_interface = &gop;
    unsigned waits = 0, reads = 0, clears = 0, output_calls = 0;
    unsigned cursor_calls = 0, attribute_calls = 0, visibility_calls = 0;
    unsigned fail_wait_at = 0, fail_output_at = 0, fail_clear_at = 0;
    unsigned fail_outputs_from = 0;
    unsigned fail_cursor_at = 0, fail_attribute_at = 0, fail_visibility_at = 0;
    unsigned reset_calls = 0, init_calls = 0, fini_calls = 0;
    unsigned watchdog_calls = 0;
    unsigned load_calls = 0, start_calls = 0, unload_calls = 0, open_calls = 0;
    unsigned unload_failures = 0, text_mode_calls = 0, gop_mode_calls = 0;
    std::vector<UINTN> watchdog_timeouts;
    unsigned child_running_reads = 0;
    unsigned restored_attributes = 0, restored_positions = 0, restored_visibility = 0;
    bool fail_restore_attribute = false, fail_restore_position = false, fail_restore_visibility = false;
    bool cursor_unsupported = false, failure_issued = false, last_key_read = false;
    bool mutate_before_failure = false;
    bool fail_output_after_input = false;
    bool unexpected_wait_index = false;
    bool boot_enabled = false, child_alive = false, child_running = false;
    bool auto_release_child = true, load_error_with_handle = false;
    bool change_child_text = false, change_child_gop = false;
    bool text_restore_changes_gop = false, redraw_changes_gop = false;
    bool replace_child_console = false, replace_child_gop = false;
    bool lie_restore_attribute = false, lie_restore_position = false, lie_restore_visibility = false;
    bool child_restore_lied = false;
    bool remove_child_input = false;
    bool load_null_handle = false;

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
        boot.LoadImage = forbidden_load;
        boot.StartImage = forbidden_start;
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
    void test_status_on_key(EFI_STATUS status)
    {
        REQUIRE(!inputs.empty());
        inputs.back().override_child_test = true;
        inputs.back().child_test_status = status;
    }
    static EFI_HANDLE parent_handle() { return reinterpret_cast<EFI_HANDLE>(0x3456); }
    static EFI_HANDLE device_handle() { return reinterpret_cast<EFI_HANDLE>(0x4567); }
    static EFI_HANDLE child_handle() { return reinterpret_cast<EFI_HANDLE>(0x5678); }
    void boot_manager() { down(2); enter(); }
    void enable_boot()
    {
        boot_enabled = true;
        menu_columns = columns;
        menu_rows = rows;
        menu_graphics_info = info;
        text_mode.MaxMode = 2;
        boot.OpenProtocol = open;
        boot.LoadImage = load;
        boot.StartImage = start;
        boot.UnloadImage = unload;
        console.SetMode = set_text_mode;
        child_console = console;
        gop.SetMode = set_gop_mode;
        parent_image.DeviceHandle = device_handle();
        const CHAR16 *path = L"\\EFI\\BOOT\\BOOTX64.EFI";
        const size_t count = ascii(path).size() + 1;
        const size_t length = 4 + count * 2;
        parent_path.assign(length + 4, 0);
        parent_path[0] = 4; parent_path[1] = 4;
        parent_path[2] = static_cast<UINT8>(length);
        parent_path[3] = static_cast<UINT8>(length >> 8);
        for (size_t index = 0; index < count; ++index) {
            parent_path[4 + index * 2] = static_cast<UINT8>(path[index]);
            parent_path[5 + index * 2] = static_cast<UINT8>(path[index] >> 8);
        }
        parent_path[length] = 0x7f; parent_path[length + 1] = 0xff;
        parent_path[length + 2] = 4;
        parent_image.FilePath = reinterpret_cast<EFI_DEVICE_PATH_PROTOCOL *>(parent_path.data());
        child_image.ImageCodeType = EfiLoaderCode;
        child_image.ImageDataType = EfiLoaderData;
    }
    EFI_STATUS run(bool startup = false)
    {
        if (inputs.empty()) escape();
        const EFI_STATUS result = startup ? menu_test_startup(parent_handle(), &table) : menu_run(parent_handle(), &table);
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
        const auto &current = *table.ConOut->Mode;
        REQUIRE(current.Attribute == ORIGINAL_ATTRIBUTE);
        REQUIRE(current.CursorColumn == ORIGINAL_COLUMN && current.CursorRow == ORIGINAL_ROW);
        REQUIRE(current.CursorVisible == 1);
    }
    void contains_output(const std::string &fragment) const
    {
        if (without_spaces(output).find(without_spaces(fragment)) == std::string::npos) {
            std::fprintf(stderr, "Output for [%s]:\n%s\n", case_name, output.c_str());
            fail(fragment.c_str(), __LINE__);
        }
    }
    static void EFIAPI forbidden() { REQUIRE(false); }
    static EFI_STATUS EFIAPI forbidden_load(BOOLEAN, EFI_HANDLE, EFI_DEVICE_PATH_PROTOCOL *, void *, UINTN, EFI_HANDLE *)
    { REQUIRE(false); return EFI_UNSUPPORTED; }
    static EFI_STATUS EFIAPI forbidden_start(EFI_HANDLE, UINTN *, CHAR16 **)
    { REQUIRE(false); return EFI_UNSUPPORTED; }
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
    static bool guid_is(const EFI_GUID *actual, const EFI_GUID &expected)
    {
        return actual && std::memcmp(actual, &expected, sizeof(expected)) == 0;
    }
    static EFI_STATUS EFIAPI open(EFI_HANDLE handle, EFI_GUID *guid, void **interface,
        EFI_HANDLE agent, EFI_HANDLE controller, UINT32 attributes)
    {
        auto &f = *active;
        REQUIRE(f.boot_enabled && agent == parent_handle() && controller == nullptr);
        ++f.open_calls;
        if (guid_is(guid, EFI_LOADED_IMAGE_PROTOCOL_GUID)) {
            if (handle == parent_handle()) {
                REQUIRE(attributes == EFI_OPEN_PROTOCOL_GET_PROTOCOL && interface);
                *interface = &f.parent_image;
                return EFI_SUCCESS;
            }
            REQUIRE(handle == child_handle());
            if (attributes == EFI_OPEN_PROTOCOL_TEST_PROTOCOL) {
                if (f.child_test_status != EFI_SUCCESS) return f.child_test_status;
                return f.child_alive ? EFI_SUCCESS : EFI_UNSUPPORTED;
            }
            REQUIRE(attributes == EFI_OPEN_PROTOCOL_GET_PROTOCOL && interface && f.child_alive);
            *interface = &f.child_image;
            return EFI_SUCCESS;
        }
        REQUIRE(guid_is(guid, EFI_DEVICE_PATH_PROTOCOL_GUID));
        REQUIRE(handle == device_handle() && attributes == EFI_OPEN_PROTOCOL_GET_PROTOCOL && interface);
        *interface = f.source_path;
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI load(BOOLEAN policy, EFI_HANDLE parent, EFI_DEVICE_PATH_PROTOCOL *path,
        void *source, UINTN size, EFI_HANDLE *image)
    {
        auto &f = *active;
        REQUIRE(f.boot_enabled && policy == 0 && parent == parent_handle());
        REQUIRE(path && source == nullptr && size == 0 && image && *image == nullptr && !f.child_alive);
        ++f.load_calls;
        f.events.emplace_back("load");
        if (f.load_null_handle) return f.load_status;
        if (!(f.load_status & (1ULL << 63)) || f.load_error_with_handle) {
            f.child_alive = true;
            *image = child_handle();
            // The real loader must explicitly clear inherited child arguments.
            f.child_image.LoadOptionsSize = 8;
            f.child_image.LoadOptions = reinterpret_cast<void *>(0x789a);
        }
        return f.load_status;
    }
    static EFI_STATUS EFIAPI start(EFI_HANDLE image, UINTN *exit_size, CHAR16 **exit_data)
    {
        auto &f = *active;
        REQUIRE(image == child_handle() && f.child_alive && !f.child_running);
        REQUIRE(exit_size == nullptr && exit_data == nullptr);
        REQUIRE(f.child_image.LoadOptionsSize == 0 && f.child_image.LoadOptions == nullptr);
        REQUIRE(!f.watchdog_timeouts.empty() && f.watchdog_timeouts.back() == 300);
        ++f.start_calls;
        f.events.emplace_back("start");
        f.child_running = true;
        f.child_running_reads = f.reads;
        if (f.replace_child_console) f.child_text_mode = f.text_mode;
        auto &child_text = f.replace_child_console ? f.child_text_mode : f.text_mode;
        if (f.change_child_text) {
            child_text.Mode = 1;
            f.geometry(60, 18);
            child_text.Attribute = 0x24;
            child_text.CursorColumn = 8;
            child_text.CursorRow = 6;
            child_text.CursorVisible = 1;
        }
        if (f.change_child_gop) {
            f.graphics_mode.Mode = 1;
            f.info.HorizontalResolution = 800;
            f.info.VerticalResolution = 600;
            f.info.PixelFormat = PixelRedGreenBlueReserved8BitPerColor;
        }
        if (f.replace_child_console) {
            f.child_console.Mode = &f.child_text_mode;
            f.table.ConOut = &f.child_console;
        }
        if (f.replace_child_gop) {
            f.child_graphics_mode = f.graphics_mode;
            f.child_graphics_info = f.menu_graphics_info;
            f.child_graphics_mode.Mode = 1;
            f.child_graphics_info.HorizontalResolution = 800;
            f.child_graphics_info.VerticalResolution = 600;
            f.child_graphics_info.PixelFormat = PixelRedGreenBlueReserved8BitPerColor;
            f.child_graphics_mode.Info = &f.child_graphics_info;
            f.gop.Mode = &f.child_graphics_mode;
        }
        if (f.remove_child_input) f.table.ConIn = nullptr;
        f.child_running = false;
        if (f.auto_release_child) f.child_alive = false;
        return f.start_status;
    }
    static EFI_STATUS EFIAPI unload(EFI_HANDLE image)
    {
        auto &f = *active;
        REQUIRE(image == child_handle() && f.child_alive && !f.child_running);
        ++f.unload_calls;
        f.events.emplace_back("unload");
        if (f.unload_failures) { --f.unload_failures; return f.unload_status; }
        f.child_alive = false;
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI set_text_mode(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, UINTN mode)
    {
        auto &f = *active;
        REQUIRE(self == f.table.ConOut && mode == 0 && f.boot_enabled);
        ++f.text_mode_calls;
        f.events.emplace_back("restore-text-mode");
        if (f.text_restore_status & (1ULL << 63)) return f.text_restore_status;
        self->Mode->Mode = 0;
        f.geometry(f.menu_columns, f.menu_rows);
        self->Mode->CursorColumn = 0;
        self->Mode->CursorRow = 0;
        if (f.text_restore_changes_gop) f.info.HorizontalResolution = 1024;
        return f.text_restore_status;
    }
    static EFI_STATUS EFIAPI set_gop_mode(EFI_GRAPHICS_OUTPUT_PROTOCOL *self, UINT32 mode)
    {
        auto &f = *active;
        REQUIRE(self == &f.gop && mode == 3 && f.boot_enabled);
        ++f.gop_mode_calls;
        f.events.emplace_back("restore-gop-mode");
        if (f.gop_restore_status & (1ULL << 63)) return f.gop_restore_status;
        f.graphics_mode.Mode = 3;
        f.info = f.menu_graphics_info;
        self->Mode->Mode = 3;
        self->Mode->Info = &f.info;
        return f.gop_restore_status;
    }
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
        REQUIRE((timeout == 0 || (f.boot_enabled && timeout == 300)) && code == 0 && size == 0 && data == nullptr);
        if (!f.boot_enabled) REQUIRE(f.watchdog_calls == 0 && f.waits == 0);
        if (f.watchdog_calls == 0) REQUIRE(timeout == 0 && f.waits == 0);
        ++f.watchdog_calls;
        f.watchdog_timeouts.push_back(timeout);
        f.events.emplace_back(timeout ? "arm-watchdog" : "disable-watchdog");
        const EFI_STATUS status = f.watchdog_calls == 1 ? f.watchdog_status :
            (timeout ? f.arm_watchdog_status : f.return_watchdog_status);
        if ((status & (1ULL << 63)) && status != EFI_UNSUPPORTED)
            f.failure_issued = true;
        return status;
    }
    static EFI_STATUS EFIAPI query(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, UINTN mode, UINTN *width, UINTN *height)
    {
        auto &f = *active;
        REQUIRE(self == f.table.ConOut && mode < static_cast<UINTN>(self->Mode->MaxMode) && width && height);
        *width = f.boot_enabled && mode == 0 ? f.menu_columns : f.columns;
        *height = f.boot_enabled && mode == 0 ? f.menu_rows : f.rows;
        return f.query_status;
    }
    static EFI_STATUS EFIAPI set_attribute(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, UINTN attribute)
    {
        auto &f = *active;
        REQUIRE(self == f.table.ConOut);
        ++f.attribute_calls;
        if (attribute == ORIGINAL_ATTRIBUTE) {
            ++f.restored_attributes;
            f.events.emplace_back("restore-attribute");
            if (f.fail_restore_attribute) return RESTORE_FAILURE;
        }
        if (f.attribute_calls == f.fail_attribute_at) {
            if (f.mutate_before_failure) self->Mode->Attribute = static_cast<INT32>(attribute);
            f.failure_issued = true;
            return OP_FAILURE;
        }
        if (f.start_calls && f.lie_restore_attribute && !f.child_restore_lied) {
            f.child_restore_lied = true;
            return EFI_SUCCESS;
        }
        self->Mode->Attribute = static_cast<INT32>(attribute);
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI set_cursor(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, UINTN column, UINTN row)
    {
        auto &f = *active;
        REQUIRE(self == f.table.ConOut && column < f.columns && row < f.rows);
        ++f.cursor_calls;
        if ((f.last_key_read || f.failure_issued) && column == ORIGINAL_COLUMN && row == ORIGINAL_ROW) {
            ++f.restored_positions;
            if (f.fail_restore_position) return RESTORE_FAILURE;
        }
        if (f.cursor_calls == f.fail_cursor_at) {
            if (f.mutate_before_failure) {
                self->Mode->CursorColumn = static_cast<INT32>(column);
                self->Mode->CursorRow = static_cast<INT32>(row);
            }
            f.failure_issued = true;
            return OP_FAILURE;
        }
        if (f.start_calls && f.lie_restore_position && !f.child_restore_lied) {
            f.child_restore_lied = true;
            return EFI_SUCCESS;
        }
        self->Mode->CursorColumn = static_cast<INT32>(column);
        self->Mode->CursorRow = static_cast<INT32>(row);
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI enable_cursor(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, BOOLEAN visible)
    {
        auto &f = *active;
        REQUIRE(self == f.table.ConOut && visible <= 1);
        ++f.visibility_calls;
        if (f.cursor_unsupported) return EFI_UNSUPPORTED;
        if (visible == 1) {
            ++f.restored_visibility;
            if (f.fail_restore_visibility) return RESTORE_FAILURE;
        }
        if (f.visibility_calls == f.fail_visibility_at) {
            if (f.mutate_before_failure) self->Mode->CursorVisible = visible;
            f.failure_issued = true;
            return OP_FAILURE;
        }
        if (f.start_calls && f.lie_restore_visibility && !f.child_restore_lied) {
            f.child_restore_lied = true;
            return EFI_SUCCESS;
        }
        self->Mode->CursorVisible = visible;
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI clear(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self)
    {
        auto &f = *active;
        REQUIRE(self == f.table.ConOut);
        ++f.clears;
        if (f.clears == f.fail_clear_at) {
            if (f.mutate_before_failure) {
                self->Mode->CursorColumn = 0;
                self->Mode->CursorRow = 0;
            }
            f.failure_issued = true;
            return OP_FAILURE;
        }
        f.cells.assign(f.cells.size(), ' ');
        f.attributes.assign(f.attributes.size(), static_cast<UINTN>(self->Mode->Attribute));
        self->Mode->CursorColumn = 0;
        self->Mode->CursorRow = 0;
        if (f.redraw_changes_gop && f.start_calls) f.info.HorizontalResolution = 1024;
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI write(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, CHAR16 *text)
    {
        auto &f = *active;
        REQUIRE(self == f.table.ConOut && text);
        ++f.output_calls;
        f.output += ascii(text);
        if (f.output_calls == f.fail_output_at || (f.fail_outputs_from && f.output_calls >= f.fail_outputs_from)) {
            if (f.mutate_before_failure) {
                self->Mode->CursorColumn = 0;
                self->Mode->CursorRow = 0;
            }
            f.failure_issued = true;
            return TEXT_FAILURE;
        }
        for (; *text; ++text) {
            if (*text == L'\r') { self->Mode->CursorColumn = 0; continue; }
            if (*text == L'\n') {
                REQUIRE(static_cast<UINTN>(self->Mode->CursorRow + 1) < f.rows);
                ++self->Mode->CursorRow;
                continue;
            }
            REQUIRE(self->Mode->CursorColumn >= 0 && self->Mode->CursorRow >= 0);
            const UINTN column = static_cast<UINTN>(self->Mode->CursorColumn);
            const UINTN row = static_cast<UINTN>(self->Mode->CursorRow);
            REQUIRE(column < f.columns && row < f.rows);
            const size_t cell = static_cast<size_t>(row * f.columns + column);
            // Writing this cell causes automatic scrolling on some firmware.
            REQUIRE(cell + 1 < f.cells.size());
            f.cells[cell] = *text < 128 ? static_cast<char>(*text) : '?';
            f.attributes[cell] = static_cast<UINTN>(self->Mode->Attribute);
            ++self->Mode->CursorColumn;
            if (static_cast<UINTN>(self->Mode->CursorColumn) == f.columns) {
                self->Mode->CursorColumn = 0;
                ++self->Mode->CursorRow;
            }
        }
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI wait(UINTN count, EFI_EVENT *events, UINTN *index)
    {
        auto &f = *active;
        REQUIRE(count == 1 && events && events[0] == f.keyboard.WaitForKey && index);
        REQUIRE(!f.child_running);
        if (!f.boot_enabled) REQUIRE(f.watchdog_calls == 1);
        ++f.waits;
        f.snapshots.push_back({f.columns, f.cells, f.attributes, f.output_calls, f.clears,
            f.table.ConOut->Mode->Mode, f.gop.Mode ? f.gop.Mode->Mode : f.graphics_mode.Mode});
        REQUIRE(f.waits <= f.inputs.size() + 1); // An unhandled key cannot hang the host test.
        if (f.waits == f.fail_wait_at) { f.failure_issued = true; return OP_FAILURE; }
        *index = f.unexpected_wait_index ? 1 : 0;
        return EFI_SUCCESS;
    }
    static EFI_STATUS EFIAPI read(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *self, EFI_INPUT_KEY *key)
    {
        auto &f = *active;
        REQUIRE(self == &f.keyboard && key && f.reads < f.inputs.size() && !f.child_running);
        const auto input = f.inputs[f.reads++];
        *key = input.key;
        if (input.override_child_test) f.child_test_status = input.child_test_status;
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
        f.snapshots[0].contains("UEFI Station 0.2");
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
            const char *messages[] = {"English", "FixtureVendor-X", "GOP Demo",
                "Default Boot Entry: Not configured", "No default boot entry configured"};
            page.contains(messages[item]);
            REQUIRE(f.snapshots.back().selected() == item);
            REQUIRE(f.reset_calls == 0);
            f.restored();
        });
    }
    check("Boot Manager opens a configured list without loading or starting an image", [](Fixture &f) {
        f.enable_boot(); f.boot_manager(); f.up(); f.down(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS);
        f.snapshots[3].contains("Boot Manager");
        f.snapshots[3].contains("GOP Demo");
        f.snapshots[3].contains("Path: \\EFI\\TOOLS\\GOP.EFI");
        f.snapshots[3].contains("Esc: Back");
        REQUIRE(f.snapshots[3].output_calls == f.snapshots[5].output_calls);
        REQUIRE(f.snapshots[3].clears == f.snapshots[5].clears);
        REQUIRE(f.load_calls == 0 && f.start_calls == 0 && f.open_calls == 0);
        REQUIRE(f.snapshots[6].selected() == 2);
        f.restored();
    });
    check("a returning application can be launched twice and then return to firmware", [](Fixture &f) {
        f.enable_boot(); f.boot_manager(); f.enter(); f.enter(); f.escape(); f.down(4); f.enter();
        REQUIRE(f.run(true) == EFI_SUCCESS);
        REQUIRE(f.load_calls == 2 && f.start_calls == 2 && f.unload_calls == 0 && !f.child_alive);
        REQUIRE(f.watchdog_timeouts == std::vector<UINTN>({0, 300, 0, 300, 0}));
        f.snapshots[4].contains("GOP Demo"); f.snapshots[4].contains("Image returned");
        f.snapshots[4].contains("Status: 0x0000000000000000");
        f.snapshots[5].contains("Image returned");
        REQUIRE(f.snapshots[6].selected() == 2);
        REQUIRE(f.child_running_reads == 5 && f.init_calls == 1 && f.fini_calls == 1);
        f.restored();
    });
    for (unsigned mutation = 1; mutation < 4; ++mutation) {
        check("child text and graphics mutations are restored before menu input resumes", [mutation](Fixture &f) {
            f.enable_boot(); f.change_child_text = mutation & 1; f.change_child_gop = mutation & 2;
            f.boot_manager(); f.enter(); f.escape(); f.escape();
            REQUIRE(f.run() == EFI_SUCCESS && f.start_calls == 1);
            REQUIRE(f.snapshots[4].columns == 80 && f.snapshots[4].text_mode == 0 && f.snapshots[4].graphics_mode == 3);
            f.snapshots[4].contains("80x25"); f.snapshots[4].contains("1366x768");
            f.snapshots[4].contains("Image returned");
            REQUIRE(f.text_mode.Mode == 0 && f.columns == 80 && f.rows == 25);
            REQUIRE(f.graphics_mode.Mode == 3 && f.info.HorizontalResolution == 1366 && f.info.VerticalResolution == 768);
            REQUIRE(f.info.PixelFormat == PixelBlueGreenRedReserved8BitPerColor);
            if (mutation & 1) REQUIRE(f.text_mode_calls >= 1);
            if (mutation & 2) REQUIRE(f.gop_mode_calls >= 1);
            if (mutation == 3) {
                const auto graphics = std::find(f.events.begin(), f.events.end(), "restore-gop-mode");
                const auto text = std::find(f.events.begin(), f.events.end(), "restore-text-mode");
                REQUIRE(graphics != f.events.end() && text != f.events.end() && graphics < text);
            }
            f.restored();
        });
    }
    check("booting without GOP still restores changed text mode", [](Fixture &f) {
        f.enable_boot(); f.locate_status = EFI_NOT_FOUND; f.change_child_text = true;
        f.boot_manager(); f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.start_calls == 1 && f.gop_mode_calls == 0);
        REQUIRE(f.snapshots[4].columns == 80 && f.snapshots[4].text_mode == 0);
        f.snapshots[4].contains("Image returned"); f.restored();
    });
    check("child replacement of ConOut and GOP Mode Info is reacquired before restoration", [](Fixture &f) {
        f.enable_boot(); f.change_child_text = true;
        f.replace_child_console = true; f.replace_child_gop = true;
        f.boot_manager(); f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.start_calls == 1);
        REQUIRE(f.table.ConOut == &f.child_console && f.table.ConOut->Mode == &f.child_text_mode);
        REQUIRE(f.table.ConOut->Mode != &f.text_mode && f.child_text_mode.Mode == 0);
        REQUIRE(f.snapshots[4].text_mode == 0 && f.snapshots[4].graphics_mode == 3);
        REQUIRE(f.gop.Mode == &f.child_graphics_mode && f.gop.Mode->Mode == 3);
        REQUIRE(f.gop.Mode->Info->HorizontalResolution == 1366 && f.gop.Mode->Info->VerticalResolution == 768);
        f.snapshots[4].contains("1366x768"); f.snapshots[4].contains("Image returned");
        f.restored();
    });
    for (unsigned operation = 0; operation < 3; ++operation) {
        const char *names[] = {
            "successful SetAttribute cannot hide a missing child attribute restoration",
            "successful SetCursorPosition cannot hide a missing child cursor restoration",
            "successful EnableCursor cannot hide a missing child visibility restoration"
        };
        check(names[operation], [operation](Fixture &f) {
            f.enable_boot(); f.change_child_text = true;
            f.lie_restore_attribute = operation == 0;
            f.lie_restore_position = operation == 1;
            f.lie_restore_visibility = operation == 2;
            f.boot_manager(); f.enter(); f.escape(); f.escape();
            REQUIRE(f.run() == EFI_DEVICE_ERROR);
            REQUIRE(f.child_restore_lied && f.start_calls == 1 && f.waits == 4 && !f.child_alive);
            f.restored();
        });
    }
    check("missing input protocol after child return is detected before another wait", [](Fixture &f) {
        f.enable_boot(); f.remove_child_input = true;
        f.boot_manager(); f.enter();
        REQUIRE(f.run() == EFI_UNSUPPORTED && f.waits == 4 && f.start_calls == 1 && !f.child_alive);
        f.restored();
    });
    for (unsigned missing = 0; missing < 2; ++missing) {
        check("missing mode restoration methods prevent image launch", [missing](Fixture &f) {
            f.enable_boot();
            if (missing == 0) f.console.SetMode = nullptr; else f.gop.SetMode = nullptr;
            f.boot_manager(); f.enter(); f.escape(); f.escape();
            REQUIRE(f.run() == EFI_SUCCESS && f.load_calls == 0 && f.start_calls == 0);
            f.snapshots[4].contains("Status:");
            f.restored();
        });
    }
    for (EFI_STATUS error : {EFI_NOT_FOUND, EFI_UNSUPPORTED, EFI_DEVICE_ERROR}) {
        check("LoadImage errors stay in the Boot Manager and preserve actual status", [error](Fixture &f) {
            f.enable_boot(); f.load_status = error;
            f.boot_manager(); f.enter(); f.enter(); f.escape(); f.escape();
            REQUIRE(f.run() == EFI_SUCCESS && f.load_calls == 2 && f.start_calls == 0 && f.unload_calls == 0);
            f.snapshots[4].contains("Load failed");
            char status[32]; std::snprintf(status, sizeof(status), "0x%016llX", error);
            f.snapshots[4].contains(status);
            f.restored();
        });
    }
    check("child StartImage errors remain visible and permit a retry", [](Fixture &f) {
        f.enable_boot(); f.start_status = OP_FAILURE;
        f.boot_manager(); f.enter(); f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.start_calls == 2 && f.unload_calls == 0);
        f.snapshots[4].contains("Start failed"); f.snapshots[4].contains("0x8000000000000001");
        f.restored();
    });
    check("watchdog arm failure stops launch and cleans the loaded image", [](Fixture &f) {
        f.enable_boot(); f.arm_watchdog_status = OP_FAILURE;
        f.boot_manager(); f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.start_calls == 0 && f.unload_calls == 1 && !f.child_alive);
        REQUIRE(f.watchdog_timeouts == std::vector<UINTN>({0, 300, 0}));
        f.snapshots[4].contains("0x8000000000000001");
        f.restored();
    });
    check("unsupported launch watchdog does not reject the child application", [](Fixture &f) {
        f.enable_boot(); f.arm_watchdog_status = EFI_UNSUPPORTED;
        f.boot_manager(); f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.start_calls == 1 && f.unload_calls == 0);
        REQUIRE(f.watchdog_timeouts == std::vector<UINTN>({0, 300, 0}));
        f.restored();
    });
    for (EFI_STATUS primary : {static_cast<EFI_STATUS>(EFI_SUCCESS), OP_FAILURE}) {
        check("watchdog disable failure exits before another normal input wait and preserves earlier error", [primary](Fixture &f) {
            f.enable_boot(); f.start_status = primary; f.return_watchdog_status = RESTORE_FAILURE;
            f.boot_manager(); f.enter();
            REQUIRE(f.run() == (primary == EFI_SUCCESS ? RESTORE_FAILURE : primary));
            REQUIRE(f.start_calls == 1 && f.waits == 4 && f.reads == 4 && !f.child_alive);
            REQUIRE(f.watchdog_timeouts == std::vector<UINTN>({0, 300, 0}));
            f.restored();
        });
    }
    check("pending cleanup blocks Escape and returns after an explicit cleanup retry", [](Fixture &f) {
        f.enable_boot(); f.auto_release_child = false; f.start_status = OP_FAILURE; f.unload_failures = 1;
        f.boot_manager(); f.enter(); f.escape(); f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.load_calls == 1 && f.start_calls == 1 && f.unload_calls == 2);
        REQUIRE(!f.child_alive);
        f.snapshots[4].contains("Retry cleanup"); f.snapshots[4].contains("Reset");
        f.snapshots[5].contains("Retry cleanup");
        f.snapshots[6].contains("GOP Demo"); f.snapshots[6].contains("0x8000000000000001");
        f.restored();
    });
    check("pending cleanup Reset Cancel returns to recovery and cleanup can be retried", [](Fixture &f) {
        f.enable_boot(); f.auto_release_child = false; f.start_status = OP_FAILURE; f.unload_failures = 1;
        f.boot_manager(); f.enter(); f.down(); f.enter(); f.enter(); f.up(); f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.load_calls == 1 && f.start_calls == 1 && f.unload_calls == 2);
        REQUIRE(f.reset_calls == 0 && !f.child_alive);
        f.snapshots[6].contains("Cancel"); f.snapshots[6].contains("Restart");
        f.snapshots[7].contains("Retry cleanup");
        f.restored();
    });
    check("missing ResetSystem leaves pending image recovery usable for a cleanup retry", [](Fixture &f) {
        f.enable_boot(); f.auto_release_child = false; f.unload_failures = 1;
        f.runtime.ResetSystem = nullptr;
        f.boot_manager(); f.enter(); f.down(); f.enter(); f.down(); f.enter();
        f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.reset_calls == 0 && f.unload_calls == 2 && !f.child_alive);
        f.snapshots[8].contains("Restart unavailable");
        f.snapshots[8].contains("Retry cleanup");
        f.snapshots[8].excludes("Restart the machine?");
        f.snapshots[9].contains("GOP Demo");
        f.restored();
    });
    check("pending cleanup survives another failed retry without loading a second child", [](Fixture &f) {
        f.enable_boot(); f.auto_release_child = false; f.start_status = OP_FAILURE; f.unload_failures = 2;
        f.boot_manager(); f.enter(); f.enter(); f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.load_calls == 1 && f.start_calls == 1 && f.unload_calls == 3);
        REQUIRE(!f.child_alive);
        f.snapshots[5].contains("Retry cleanup"); f.snapshots[6].contains("GOP Demo");
        f.restored();
    });
    check("failed cleanup retries retain the first cleanup error in recovery", [](Fixture &f) {
        f.enable_boot(); f.auto_release_child = false; f.unload_failures = 1;
        f.unload_status = EFI_ACCESS_DENIED;
        f.boot_manager(); f.enter(); f.enter(); f.test_status_on_key(EFI_DEVICE_ERROR);
        f.enter(); f.test_status_on_key(EFI_SUCCESS); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.unload_calls == 2 && !f.child_alive);
        f.snapshots[4].contains("Cleanup: 0x800000000000000F");
        f.snapshots[5].contains("Cleanup: 0x800000000000000F");
        f.snapshots[5].excludes("Cleanup: 0x8000000000000007");
        f.restored();
    });
    check("Recovery confirmed Reset returning still cleans the child before firmware return", [](Fixture &f) {
        f.enable_boot(); f.auto_release_child = false; f.unload_failures = 1;
        f.boot_manager(); f.enter(); f.down(); f.enter(); f.down(); f.enter();
        f.escape(); f.up(); f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_DEVICE_ERROR);
        REQUIRE(f.reset_calls == 1 && f.load_calls == 1 && f.start_calls == 1 && f.unload_calls == 2 && !f.child_alive);
        f.snapshots[8].contains("Restart failed: ResetSystem returned.");
        f.snapshots[9].contains("Retry cleanup");
        f.restored();
    });
    check("Recovery wait failure keeps the earlier StartImage error after successful cleanup", [](Fixture &f) {
        f.enable_boot(); f.auto_release_child = false; f.start_status = EFI_ABORTED; f.unload_failures = 1;
        f.boot_manager(); f.enter(); f.fail_wait_at = 5;
        REQUIRE(f.run() == EFI_ABORTED);
        REQUIRE(f.unload_calls == 2 && f.start_calls == 1 && f.waits == 5 && !f.child_alive);
        f.restored();
    });
    check("pending image is released before an input failure returns from menu_run", [](Fixture &f) {
        f.enable_boot(); f.auto_release_child = false; f.start_status = OP_FAILURE; f.unload_failures = 1;
        f.boot_manager(); f.enter(); f.key(0, 0, TEXT_FAILURE);
        REQUIRE(f.run() == OP_FAILURE);
        REQUIRE(f.unload_calls == 2 && !f.child_alive && f.waits == 5);
        f.restored();
    });
    check("watchdog disable failure with a pending child stays in recovery until cleanup succeeds", [](Fixture &f) {
        f.enable_boot(); f.auto_release_child = false; f.unload_failures = 2;
        f.return_watchdog_status = OP_FAILURE;
        f.boot_manager(); f.enter(); f.enter();
        REQUIRE(f.run() == OP_FAILURE && f.unload_calls == 3 && !f.child_alive);
        REQUIRE(f.waits == 5 && f.reads == 5);
        f.snapshots[4].contains("Retry cleanup");
        f.restored();
    });
    check("mode restoration failure with a pending child offers cleanup at the measured current size", [](Fixture &f) {
        f.enable_boot(); f.auto_release_child = false; f.unload_failures = 2;
        f.change_child_text = true; f.text_restore_status = RESTORE_FAILURE;
        f.boot_manager(); f.enter(); f.enter();
        REQUIRE(f.run() == RESTORE_FAILURE && f.unload_calls == 3 && !f.child_alive);
        REQUIRE(f.waits == 5 && f.snapshots[4].columns == 60);
        f.snapshots[4].contains("Retry cleanup");
    });
    for (unsigned mutation = 0; mutation < 2; ++mutation) {
        check("post-return verification detects console or redraw changing GOP after restoration", [mutation](Fixture &f) {
            f.enable_boot(); f.change_child_text = true; f.change_child_gop = true;
            if (mutation == 0) f.text_restore_changes_gop = true; else f.redraw_changes_gop = true;
            f.boot_manager(); f.enter();
            REQUIRE(f.run() != EFI_SUCCESS && f.waits == 4 && !f.child_alive);
            REQUIRE(f.text_mode.Mode == 0 && f.columns == 80 && f.rows == 25);
            f.restored();
        });
    }
    for (EFI_STATUS primary : {static_cast<EFI_STATUS>(EFI_SUCCESS), OP_FAILURE}) {
        check("display restoration error stops ordinary input and preserves the first boot error", [primary](Fixture &f) {
            f.enable_boot(); f.change_child_text = true; f.start_status = primary;
            f.text_restore_status = RESTORE_FAILURE;
            f.boot_manager(); f.enter();
            REQUIRE(f.run() == (primary == EFI_SUCCESS ? RESTORE_FAILURE : primary));
            REQUIRE(f.waits == 4 && f.start_calls == 1 && !f.child_alive);
        });
    }
    for (EFI_STATUS primary : {static_cast<EFI_STATUS>(EFI_SUCCESS), OP_FAILURE}) {
        check("GOP restoration failure returns safely without another ordinary menu wait", [primary](Fixture &f) {
            f.enable_boot(); f.change_child_gop = true; f.start_status = primary;
            f.gop_restore_status = RESTORE_FAILURE;
            f.boot_manager(); f.enter();
            REQUIRE(f.run() == (primary == EFI_SUCCESS ? RESTORE_FAILURE : primary));
            REQUIRE(f.waits == 4 && f.start_calls == 1 && !f.child_alive);
            f.restored();
        });
    }
    check("40x15 Boot Manager keeps entry path result status and Escape available", [](Fixture &f) {
        f.geometry(40, 15); f.enable_boot(); f.boot_manager(); f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS);
        f.snapshots[3].contains("GOP Demo"); f.snapshots[3].contains("\\EFI\\TOOLS\\GOP.EFI");
        f.snapshots[3].contains("Enter"); f.snapshots[3].contains("Esc");
        f.snapshots[4].contains("Image returned"); f.snapshots[4].contains("0x0000000000000000");
        f.snapshots[4].contains("Enter"); f.snapshots[4].contains("Esc");
        f.restored();
    });
    check("40x15 Reset confirmation keeps the Escape Back hint visible", [](Fixture &f) {
        f.geometry(40, 15); f.down(5); f.enter(); f.escape(); f.escape();
        REQUIRE(f.run() == EFI_SUCCESS && f.reset_calls == 0);
        f.snapshots[6].contains("Cancel"); f.snapshots[6].contains("Restart");
        f.snapshots[6].contains("Esc Back");
        f.restored();
    });
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
        REQUIRE(menu_run(Fixture::parent_handle(), nullptr) == EFI_INVALID_PARAMETER);
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
    return 0;
}
