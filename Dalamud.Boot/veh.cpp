#define WIN32_LEAN_AND_MEAN
#include "veh.h"
#include <filesystem>
#include <fstream>
#include <Windows.h>
#include <DbgHelp.h>
#include <format>
#include <string>

bool is_whitelist_exception(const DWORD code)
{
    switch (code)
    {
    case STATUS_ACCESS_VIOLATION:
    case STATUS_IN_PAGE_ERROR:
    case STATUS_INVALID_HANDLE:
    case STATUS_INVALID_PARAMETER:
    case STATUS_NO_MEMORY:
    case STATUS_ILLEGAL_INSTRUCTION:
    case STATUS_NONCONTINUABLE_EXCEPTION:
    case STATUS_INVALID_DISPOSITION:
    case STATUS_ARRAY_BOUNDS_EXCEEDED:
    case STATUS_FLOAT_DENORMAL_OPERAND:
    case STATUS_FLOAT_DIVIDE_BY_ZERO:
    case STATUS_FLOAT_INEXACT_RESULT:
    case STATUS_FLOAT_INVALID_OPERATION:
    case STATUS_FLOAT_OVERFLOW:
    case STATUS_FLOAT_STACK_CHECK:
    case STATUS_FLOAT_UNDERFLOW:
    case STATUS_INTEGER_DIVIDE_BY_ZERO:
    case STATUS_INTEGER_OVERFLOW:
    case STATUS_PRIVILEGED_INSTRUCTION:
    case STATUS_STACK_OVERFLOW:
    case STATUS_DLL_NOT_FOUND:
    case STATUS_ORDINAL_NOT_FOUND:
    case STATUS_ENTRYPOINT_NOT_FOUND:
    case STATUS_DLL_INIT_FAILED:
    case STATUS_CONTROL_STACK_VIOLATION:
    case STATUS_FLOAT_MULTIPLE_FAULTS:
    case STATUS_FLOAT_MULTIPLE_TRAPS:
    case STATUS_HEAP_CORRUPTION:
    case STATUS_STACK_BUFFER_OVERRUN:
    case STATUS_INVALID_CRUNTIME_PARAMETER:
    case STATUS_THREAD_NOT_RUNNING:
    case STATUS_ALREADY_REGISTERED:
        return true;
    default:
        return false;
    }
}


bool get_module_file_and_base(const DWORD64 address, DWORD64* module_base, std::filesystem::path& module_file)
{
    HMODULE handle;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(address), &handle))
    {
        if (wchar_t path[1024]; GetModuleFileNameW(handle, path, sizeof path / 2) > 0)
        {
            *module_base = reinterpret_cast<DWORD64>(handle);
            module_file = path;
            return true;
        }
    }
    return false;
}


bool is_ffxiv_address(const DWORD64 address)
{
    DWORD64 module_base;
    if (std::filesystem::path module_path; get_module_file_and_base(address, &module_base, module_path))
        return _wcsicmp(module_path.filename().c_str(), L"ffxiv_dx11.exe") == 0;
    return false;
}


bool get_sym_from_addr(const DWORD64 address, DWORD64* displacement, std::wstring& symbol_name)
{
    char buffer[sizeof(SYMBOL_INFOW) + MAX_SYM_NAME * sizeof(wchar_t)];
    auto symbol = reinterpret_cast<PSYMBOL_INFOW>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    
    if (SymFromAddrW(GetCurrentProcess(), address, displacement, symbol))
    {
        symbol_name.assign(_wcsdup(symbol->Name));
        return true;
    }
    return false;
}


std::wstring to_address_string(const DWORD64 address, const bool try_ptrderef = true)
{
    DWORD64 module_base;
    std::filesystem::path module_path;
    bool is_mod_addr = get_module_file_and_base(address, &module_base, module_path);
    
    DWORD64 value = 0;
    if(try_ptrderef && address > 0x10000 && address < 0x7FFFFFFE0000)
    {
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &value, sizeof value, nullptr);
    }

    std::wstring addr_str = is_mod_addr ?
        std::format(L"{}+{:X}", module_path.filename().c_str(), address - module_base) :
        std::format(L"{:X}", address);

    DWORD64 displacement;
    if (std::wstring symbol; get_sym_from_addr(address, &displacement, symbol))
        return std::format(L"{}\t({})", addr_str, displacement != 0 ? std::format(L"{}+0x{:X}", symbol, displacement) : std::format(L"{}", symbol));
    return value != 0 ? std::format(L"{} [{}]", addr_str, to_address_string(value, false)) : addr_str;
}


void print_exception_info(const EXCEPTION_POINTERS* ex, std::wofstream& log)
{
    log << L"\nCall Stack\n{";

    STACKFRAME64 sf;
    sf.AddrPC.Offset = ex->ContextRecord->Rip;
    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ex->ContextRecord->Rsp;
    sf.AddrStack.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ex->ContextRecord->Rbp;
    sf.AddrFrame.Mode = AddrModeFlat;
    CONTEXT ctx = *ex->ContextRecord;    
    int frame_index = 0;

    log << std::format(L"\n  [{}]\t{}", frame_index++, to_address_string(sf.AddrPC.Offset, false));

    do
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &sf, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;

        log << std::format(L"\n  [{}]\t{}", frame_index++, to_address_string(sf.AddrPC.Offset, false));

    } while (sf.AddrReturn.Offset != 0 && sf.AddrPC.Offset != sf.AddrReturn.Offset);

    log << "\n}" << std::endl;

    ctx = *ex->ContextRecord;

    log << "\nRegisters\n{";
    
    log << std::format(L"\n  RAX:\t{}", to_address_string(ctx.Rax));
    log << std::format(L"\n  RBX:\t{}", to_address_string(ctx.Rbx));
    log << std::format(L"\n  RCX:\t{}", to_address_string(ctx.Rcx));
    log << std::format(L"\n  RDX:\t{}", to_address_string(ctx.Rdx));
    log << std::format(L"\n  R8:\t{}", to_address_string(ctx.R8));
    log << std::format(L"\n  R9:\t{}", to_address_string(ctx.R9));
    log << std::format(L"\n  R10:\t{}", to_address_string(ctx.R10));
    log << std::format(L"\n  R11:\t{}", to_address_string(ctx.R11));
    log << std::format(L"\n  R12:\t{}", to_address_string(ctx.R12));
    log << std::format(L"\n  R13:\t{}", to_address_string(ctx.R13));
    log << std::format(L"\n  R14:\t{}", to_address_string(ctx.R14));
    log << std::format(L"\n  R15:\t{}", to_address_string(ctx.R15));

    log << std::format(L"\n  RSI:\t{}", to_address_string(ctx.Rsi));
    log << std::format(L"\n  RDI:\t{}", to_address_string(ctx.Rdi));
    log << std::format(L"\n  RBP:\t{}", to_address_string(ctx.Rbp));
    log << std::format(L"\n  RSP:\t{}", to_address_string(ctx.Rsp));
    log << std::format(L"\n  RIP:\t{}", to_address_string(ctx.Rip));

    log << "\n}" << std::endl;

    if(ctx.Rsp <= 0x10000 || ctx.Rsp >= 0x7FFFFFFE0000) 
        return;

    log << "\nStack\n{";

    for(DWORD64 i = 0; i < 16; i++)
        log << std::format(L"\n  [RSP+{:X}]\t{}", i * 8, to_address_string(*reinterpret_cast<DWORD64*>(ctx.Rsp + i * 8ull)));

    log << "\n}" << std::endl;
}


bool g_veh_message_open;

LONG exception_handler(EXCEPTION_POINTERS* ex)
{
    //block any further exceptions while the message box is open
    if (g_veh_message_open)
        for (;;) Sleep(1);

    if (!is_whitelist_exception(ex->ExceptionRecord->ExceptionCode))
        return EXCEPTION_CONTINUE_SEARCH;

    if (!is_ffxiv_address(ex->ContextRecord->Rip))
        return EXCEPTION_CONTINUE_SEARCH;

    DWORD64 module_base;
    std::filesystem::path module_path;

    get_module_file_and_base(reinterpret_cast<DWORD64>(&exception_handler), &module_base, module_path);
#ifndef NDEBUG
    std::wstring dmp_path = _wcsdup(module_path.replace_filename(L"dalamud_appcrashd.dmp").wstring().c_str());
#else
    std::wstring dmp_path = _wcsdup(module_path.replace_filename(L"dalamud_appcrash.dmp").wstring().c_str());
#endif
    std::wstring log_path = _wcsdup(module_path.replace_filename(L"dalamud_appcrash.log").wstring().c_str());

    std::wofstream log;
    log.open(log_path, std::ios::trunc);

    log << std::format(L"Unhandled native exception occurred at {}", to_address_string(ex->ContextRecord->Rip, false)) << std::endl;
    log << std::format(L"Code: {:X}", ex->ExceptionRecord->ExceptionCode) << std::endl;
    log << L"Time: " << std::chrono::zoned_time{ std::chrono::current_zone(), std::chrono::system_clock::now() } << std::endl;

    SymRefreshModuleList(GetCurrentProcess());
    print_exception_info(ex, log);
    
    log.close();

    MINIDUMP_EXCEPTION_INFORMATION ex_info;
    ex_info.ClientPointers = false;
    ex_info.ExceptionPointers = ex;
    ex_info.ThreadId = GetCurrentThreadId();
    
    HANDLE file = CreateFileW(dmp_path.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpWithDataSegs, &ex_info, nullptr, nullptr);
    CloseHandle(file);

    auto msg = L"An error within the game has occurred and Dalamud has caught it.\n\n"
        L"This could be caused by a faulty plugin.\n"
        L"Please report this issue on our Discord - more information has been recorded separately.\n\n"
        L"The crash dump file is located at:\n"
        L"{0}\n\n"
        L"The log file is located at:\n"
        L"{1}\n\n"
        L"Press OK to exit the application.";

    auto formatted = std::format(msg, dmp_path, log_path);

    // block any other exceptions hitting the veh while the messagebox is open
    g_veh_message_open = true;
    MessageBoxW(nullptr, formatted.c_str(), L"Dalamud Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
    g_veh_message_open = false;

    return EXCEPTION_CONTINUE_SEARCH;
}


PVOID g_veh_handle;

bool veh::add_handler()
{
    if (g_veh_handle)
        return false;
    g_veh_handle = AddVectoredExceptionHandler(0, exception_handler);
    // init the symbol handler, the game already does it in WinMain but just in case
    SymInitializeW(GetCurrentProcess(), nullptr, true);
    return g_veh_handle != nullptr;
}

bool veh::remove_handler()
{
    if (g_veh_handle && RemoveVectoredExceptionHandler(g_veh_handle) != 0)
    {
        g_veh_handle = nullptr;
        return true;
    }
    return false;
}