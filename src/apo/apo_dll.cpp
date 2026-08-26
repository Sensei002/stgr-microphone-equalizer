// STGR APO DLL entry point: COM exports.
#include "apo_guids.h"
#include "stgr_apo.h"
#include "apo_registration.h"
#include <windows.h>
#include <combaseapi.h>
#include <atomic>

namespace {

std::atomic<LONG> g_moduleRefs{0};
std::atomic<LONG> g_locks{0};

class ClassFactory final : public IClassFactory {
public:
    ClassFactory() { g_moduleRefs.fetch_add(1); }
    ~ClassFactory() { g_moduleRefs.fetch_sub(1); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        const ULONG r = --refs_;
        if (r == 0) delete this;
        return r;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) override
    {
        if (!ppvObject) return E_POINTER;
        *ppvObject = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;

        auto* apo = new stgr::apo::StgrApo();
        const HRESULT hr = apo->QueryInterface(riid, ppvObject);
        apo->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL fLock) override
    {
        if (fLock) g_locks.fetch_add(1);
        else g_locks.fetch_sub(1);
        return S_OK;
    }

private:
    std::atomic<ULONG> refs_{1};
};

} // namespace

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (rclsid != CLSID_STGR_APO) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new ClassFactory();
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow()
{
    return (g_moduleRefs.load() == 0 && g_locks.load() == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(GetModuleHandleW(L"stgr_apo.dll"), path, MAX_PATH))
        return E_FAIL;
    return stgr::apo::register_apo(path);
}

STDAPI DllUnregisterServer()
{
    return stgr::apo::unregister_apo();
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls((HINSTANCE)GetModuleHandleW(L"stgr_apo.dll"));
    }
    return TRUE;
}
