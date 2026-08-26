// STGRAdmin.exe - elevated helper used by the installer and the GUI.
//
// Commands:
//   --register-apo          Register COM + AudioEngine APO (uses install dir)
//   --unregister-apo        Remove COM + AudioEngine registration
//   --attach <endpointId>   Attach STGR to a capture endpoint
//   --detach <endpointId>   Detach STGR from a capture endpoint
//   --restart-audio         Restart the Windows audio service
//
// Exit code is the HRESULT of the operation.
#include <windows.h>
#include <cstdio>
#include <string>

#include "../common/log.h"
#include "../common/paths.h"
#include "../common/util.h"
#include "../apo/apo_registration.h"

static int usage()
{
    fprintf(stderr, "STGRAdmin - STGR Microphone Equalizer elevated helper\n"
                    "  --register-apo | --unregister-apo | --attach <id> | "
                    "--detach <id> | --restart-audio\n");
    return 1;
}

int wmain(int argc, wchar_t** argv)
{
    stgr::log_init(L"admin");

    if (argc < 2) return usage();

    const std::wstring cmd = argv[1];
    HRESULT hr = E_INVALIDARG;

    if (cmd == L"--register-apo") {
        hr = stgr::apo::register_apo(stgr::install_dir() + L"\\stgr_apo.dll");
    } else if (cmd == L"--unregister-apo") {
        hr = stgr::apo::unregister_apo();
    } else if (cmd == L"--attach" && argc >= 3) {
        hr = stgr::apo::attach_endpoint(argv[2]);
    } else if (cmd == L"--detach" && argc >= 3) {
        hr = stgr::apo::detach_endpoint(argv[2]);
    } else if (cmd == L"--restart-audio") {
        hr = stgr::apo::restart_audio_service();
    } else {
        return usage();
    }

    wprintf(L"result: 0x%08X\n", (unsigned)hr);
    return (int)hr;
}
