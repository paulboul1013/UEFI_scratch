#define main menu_all_regressions
#include "../../../tests/menu_host.cpp"
#undef main
int main(int argc, char **argv)
{
    const unsigned operation = argc > 1 ? static_cast<unsigned>(std::atoi(argv[1])) : 0;
    const char *names[] = {"lying attribute", "lying cursor", "lying visibility", "cleanup status priority", "recovery wait priority"};
    REQUIRE(operation < 5);
    check(names[operation], [operation](Fixture &f) {
        f.enable_boot();
        if (operation < 3) {
            f.change_child_text = true;
            f.lie_restore_attribute = operation == 0;
            f.lie_restore_position = operation == 1;
            f.lie_restore_visibility = operation == 2;
            f.boot_manager(); f.enter(); f.escape(); f.escape();
            const EFI_STATUS status = f.run();
            std::printf("operation=%u actual_status=0x%016llX waits=%u child_restore_lied=%u\n", operation, status, f.waits, f.child_restore_lied);
            std::fflush(stdout);
            REQUIRE(status == EFI_DEVICE_ERROR && f.waits == 4);
        } else if (operation == 3) {
            f.auto_release_child = false; f.unload_failures = 1; f.unload_status = EFI_ACCESS_DENIED;
            f.boot_manager(); f.enter(); f.enter(); f.test_status_on_key(EFI_DEVICE_ERROR);
            f.enter(); f.test_status_on_key(EFI_SUCCESS); f.escape(); f.escape();
            REQUIRE(f.run() == EFI_SUCCESS);
            f.snapshots[5].contains("Cleanup: 0x800000000000000F");
        } else {
            f.auto_release_child = false; f.start_status = EFI_ABORTED; f.unload_failures = 1;
            f.boot_manager(); f.enter(); f.fail_wait_at = 5;
            const EFI_STATUS status = f.run();
            std::printf("actual_status=0x%016llX unloads=%u waits=%u\n", status, f.unload_calls, f.waits);
            std::fflush(stdout);
            REQUIRE(status == EFI_ABORTED);
        }
    });
    return 0;
}
