// STGRScan.exe - isolated plugin scanner.
//
// Runs as a separate process so that a crashing third-party plugin cannot
// take down the GUI. For every plugin file found in the given directories it
// probes metadata and instantiates the plugin to verify loadability, then
// merges the result into the shared plugin cache (incrementally, so a crash
// loses at most the current entry).
#include <windows.h>
#include <string>
#include <vector>

#include "../common/log.h"
#include "../common/util.h"
#include "../plugins/plugin_api.h"
#include "../plugins/plugin_loader.h"

int wmain(int argc, wchar_t** argv)
{
    stgr::log_init(L"scan");
    stgr::log_set_level(stgr::LogLevel::Info);
    STGR_LOG_INFO(L"STGR scan started, %d dirs", argc - 1);

    std::vector<std::wstring> dirs;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--dir" && i + 1 < argc) {
            dirs.push_back(argv[++i]);
        }
    }
    if (dirs.empty()) {
        dirs = stgr::plugins::default_vst3_dirs();
        for (const auto& d : stgr::plugins::default_vst2_dirs()) dirs.push_back(d);
    }

    int scanned = 0, failed = 0;
    for (const auto& dir : dirs) {
        if (!stgr::path_exists(dir)) continue;
        std::vector<std::wstring> files;
        const auto vst3 = stgr::list_files(dir, L".vst3");
        files.insert(files.end(), vst3.begin(), vst3.end());
        const auto dll = stgr::list_files(dir, L".dll");
        files.insert(files.end(), dll.begin(), dll.end());

        for (const auto& file : files) {
            stgr::plugins::PluginEntry entry;
            if (!stgr::plugins::probe_plugin_file(file, entry)) continue;

            // Verify loadability by actually creating the processor.
            auto proc = entry.format == 3
                ? stgr::plugins::create_vst3(file)
                : stgr::plugins::create_vst2(file);
            if (proc && proc->init(48000.0, 1)) {
                entry.status = 0; // ok
                if (entry.uid.empty() && entry.format == 2) {
                    // Best effort: no stable uid from probe; leave empty.
                }
                ++scanned;
            } else {
                entry.status = 2; // scan failed
                ++failed;
            }

            stgr::plugins::merge_plugin_result(entry);
            STGR_LOG_INFO(L"scanned: %s (%s)", file.c_str(),
                          entry.status == 0 ? L"ok" : L"FAILED");
        }
    }

    STGR_LOG_INFO(L"scan finished: %d ok, %d failed", scanned, failed);
    return 0;
}
