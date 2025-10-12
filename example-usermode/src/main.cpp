#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winreg.h>
#include <tchar.h>

#include <iostream>
#include <iomanip>
#include <string>
#include <fstream>
#include <sstream>
#include <locale>
#include <codecvt>
#include <thread>
#include <httplib.h>
#include <json.hpp>

#define IOCTL_LIST_PROCESSES CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_GET_PROCESS_COUNT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_GET_PROCESS_BY_INDEX CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_GET_PROCESS_BY_PID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_READ_DATA)

typedef struct _ADJACENT_PROCESS_INFO {
    ULONG ProcessId;
    WCHAR ProcessName[64];
    PVOID EProcessAddress;
} ADJACENT_PROCESS_INFO, * PADJACENT_PROCESS_INFO;

typedef struct _PROCESS_INFO {
	ULONG ProcessId;
	ULONG ParentProcessId;
	WCHAR ProcessName[64];
	ULONG ThreadCount;
	ULONG HandleCount;
	UCHAR BasePriority;
	LARGE_INTEGER CreateTime;
	LARGE_INTEGER UserTime;
	LARGE_INTEGER KernelTime;

	SIZE_T WorkingSetSize;
	SIZE_T PeakWorkingSetSize;
	SIZE_T VirtualSize;
	SIZE_T PeakVirtualSize;
	SIZE_T PagefileUsage;
	SIZE_T PeakPagefileUsage;
	SIZE_T PageFaultCount;

	ULONGLONG ReadOperationCount;
	ULONGLONG WriteOperationCount;
	ULONGLONG OtherOperationCount;
	ULONGLONG ReadTransferCount;
	ULONGLONG WriteTransferCount;
	ULONGLONG OtherTransferCount;

	PVOID CurrentProcessAddress;
    ADJACENT_PROCESS_INFO NextProcess;
    ADJACENT_PROCESS_INFO PreviousProcess;
} PROCESS_INFO, * PPROCESS_INFO;

typedef struct _PROCESS_REQUEST {
	ULONG RequestType; // 0 = by index, 1 = by PID
	ULONG Index;       // Process index (for iteration)
	ULONG ProcessId;   // Process ID (for specific lookup)
} PROCESS_REQUEST, * PPROCESS_REQUEST;

typedef struct _PROCESS_COUNT_RESPONSE {
	ULONG ProcessCount;
	LONG Status;
} PROCESS_COUNT_RESPONSE, * PPROCESS_COUNT_RESPONSE;


using json = nlohmann::json;

 // Global variables
 HANDLE g_hDevice = INVALID_HANDLE_VALUE;
 std::string g_remoteApiUrl = "http://localhost:8080/api/process-data"; // Default URL
 std::string g_serverHost = "0.0.0.0"; // Default host
 int g_serverPort = 8888; // Default port

 const char* REGISTRY_KEY = "SOFTWARE\\PeepersDriver";
 const char* API_URL_VALUE = "RemoteApiUrl";
 const char* SERVER_HOST_VALUE = "ServerHost";
 const char* SERVER_PORT_VALUE = "ServerPort";

 const char* ENV_API_URL = "PEEPERS_API_URL";
 const char* ENV_SERVER_HOST = "PEEPERS_SERVER_HOST";
 const char* ENV_SERVER_PORT = "PEEPERS_SERVER_PORT";

std::string ReadRegistryString(HKEY hKey, const char* valueName, const std::string& defaultValue = "") {
    DWORD dataSize = 0;
    DWORD dataType = REG_SZ;

    LONG result = RegQueryValueExA(hKey, valueName, NULL, &dataType, NULL, &dataSize);
    if (result != ERROR_SUCCESS || dataSize == 0) {
        return defaultValue;
    }

    std::vector<char> buffer(dataSize);
    result = RegQueryValueExA(hKey, valueName, NULL, &dataType,
                             reinterpret_cast<LPBYTE>(buffer.data()), &dataSize);

    if (result == ERROR_SUCCESS) {
        return std::string(buffer.data());
    }

    return defaultValue;
}

DWORD ReadRegistryDWORD(HKEY hKey, const char* valueName, DWORD defaultValue = 0) {
    DWORD value = defaultValue;
    DWORD dataSize = sizeof(DWORD);
    DWORD dataType = REG_DWORD;

    LONG result = RegQueryValueExA(hKey, valueName, NULL, &dataType,
                                  reinterpret_cast<LPBYTE>(&value), &dataSize);

    return (result == ERROR_SUCCESS) ? value : defaultValue;
}

bool WriteRegistryString(HKEY hKey, const char* valueName, const std::string& value) {
    LONG result = RegSetValueExA(hKey, valueName, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(value.c_str()),
                                static_cast<DWORD>(value.length() + 1));
    return result == ERROR_SUCCESS;
}

bool WriteRegistryDWORD(HKEY hKey, const char* valueName, DWORD value) {
    LONG result = RegSetValueExA(hKey, valueName, 0, REG_DWORD,
                                reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
    return result == ERROR_SUCCESS;
}

std::string GetEnvironmentVariable(const char* varName, const std::string& defaultValue = "") {
    char* value = nullptr;
    size_t len = 0;

    errno_t err = _dupenv_s(&value, &len, varName);
    if (err == 0 && value != nullptr) {
        std::string result(value);
        free(value);
        return result;
    }

    return defaultValue;
}

void LoadConfiguration() {
    std::cout << "Loading configuration..." << std::endl;

    // Priority order:
    // 1. Environment variables (highest priority)
    // 2. Windows Registry
    // 3. Default values (lowest priority)

    HKEY hKey = NULL;
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, REGISTRY_KEY, 0, KEY_READ, &hKey);

    if (result == ERROR_SUCCESS) {
        g_remoteApiUrl = ReadRegistryString(hKey, API_URL_VALUE);
        g_serverHost = ReadRegistryString(hKey, SERVER_HOST_VALUE, "0.0.0.0");
        g_serverPort = static_cast<int>(ReadRegistryDWORD(hKey, SERVER_PORT_VALUE, 8888));
    }

    std::string envApiUrl = GetEnvironmentVariable(ENV_API_URL);
    if (!envApiUrl.empty()) {
        g_remoteApiUrl = envApiUrl;
        std::cout << "API URL loaded from environment variable" << std::endl;
    }

    std::string envServerHost = GetEnvironmentVariable(ENV_SERVER_HOST);
    if (!envServerHost.empty()) {
        g_serverHost = envServerHost;
        std::cout << "Server host loaded from environment variable" << std::endl;
    }

    std::string envServerPort = GetEnvironmentVariable(ENV_SERVER_PORT);
    if (!envServerPort.empty()) {
        try {
            g_serverPort = std::stoi(envServerPort);
            std::cout << "Server port loaded from environment variable" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Invalid port in environment variable, using default: " << g_serverPort << std::endl;
        }
    }

    if (g_remoteApiUrl.empty()) {
        g_remoteApiUrl = "http://localhost:8080/api/process-data";
        std::cout << "Using default API URL (no configuration found)" << std::endl;
    }

    if (hKey) {
        RegCloseKey(hKey);
    }

    std::cout << "Configuration loaded:" << std::endl;
    std::cout << "  API URL: " << g_remoteApiUrl << std::endl;
    std::cout << "  Server Host: " << g_serverHost << std::endl;
    std::cout << "  Server Port: " << g_serverPort << std::endl;
}

bool SaveConfigurationToRegistry(const std::string& apiUrl, const std::string& serverHost, int serverPort) {
    HKEY hKey = NULL;
    DWORD disposition;

    LONG result = RegCreateKeyExA(HKEY_LOCAL_MACHINE, REGISTRY_KEY, 0, NULL,
                                 REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, &disposition);

    if (result != ERROR_SUCCESS) {
        std::cout << "Failed to create/open registry key. Error: " << result << std::endl;
        return false;
    }

    bool success = true;

    if (!apiUrl.empty()) {
        success &= WriteRegistryString(hKey, API_URL_VALUE, apiUrl);
    }

    if (!serverHost.empty()) {
        success &= WriteRegistryString(hKey, SERVER_HOST_VALUE, serverHost);
    }

    if (serverPort > 0) {
        success &= WriteRegistryDWORD(hKey, SERVER_PORT_VALUE, static_cast<DWORD>(serverPort));
    }

    RegCloseKey(hKey);

    if (success) {
        std::cout << "Configuration saved to registry successfully" << std::endl;
    } else {
        std::cout << "Failed to save some configuration values to registry" << std::endl;
    }

    return success;
}

// ... existing code ...
std::string WideStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::string FormatFileTime(LARGE_INTEGER time) {
    FILETIME ft;
    ft.dwLowDateTime = time.LowPart;
    ft.dwHighDateTime = time.HighPart;

    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);

    char buffer[64];
    sprintf_s(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::string(buffer);
}

json ProcessInfoToJson(const PROCESS_INFO& info) {
    json j;

    // Convert wide string process name to UTF-8
    std::wstring processNameWide(info.ProcessName);
    j["processName"] = WideStringToUtf8(processNameWide);

    j["processId"] = info.ProcessId;
    j["parentProcessId"] = info.ParentProcessId;
    j["threadCount"] = info.ThreadCount;
    j["handleCount"] = info.HandleCount;
    j["basePriority"] = info.BasePriority;

    j["createTime"] = FormatFileTime(info.CreateTime);
    j["userTime"] = info.UserTime.QuadPart;
    j["kernelTime"] = info.KernelTime.QuadPart;

    j["workingSetSize"] = info.WorkingSetSize;
    j["peakWorkingSetSize"] = info.PeakWorkingSetSize;
    j["virtualSize"] = info.VirtualSize;
    j["peakVirtualSize"] = info.PeakVirtualSize;
    j["pagefileUsage"] = info.PagefileUsage;
    j["peakPagefileUsage"] = info.PeakPagefileUsage;
    j["pageFaultCount"] = info.PageFaultCount;

    j["readOperationCount"] = info.ReadOperationCount;
    j["writeOperationCount"] = info.WriteOperationCount;
    j["otherOperationCount"] = info.OtherOperationCount;
    j["readTransferCount"] = info.ReadTransferCount;
    j["writeTransferCount"] = info.WriteTransferCount;
    j["otherTransferCount"] = info.OtherTransferCount;

    std::stringstream ss;
    ss << "0x" << std::hex << (uintptr_t)info.CurrentProcessAddress;
    j["currentProcessAddress"] = ss.str();

    j["previousProcess"] = json::object();

    ss.str("");
    ss << "0x" << std::hex << (uintptr_t)info.PreviousProcess.EProcessAddress;
    j["previousProcess"]["eProcessAddress"] = ss.str();
    j["previousProcess"]["processId"] = info.PreviousProcess.ProcessId;
    std::wstring prevProcessNameWide(info.PreviousProcess.ProcessName);
    j["previousProcess"]["processName"] = WideStringToUtf8(prevProcessNameWide);

    j["nextProcess"] = json::object();

    ss.str("");
    ss << "0x" << std::hex << (uintptr_t)info.NextProcess.EProcessAddress;
    j["nextProcess"]["eProcessAddress"] = ss.str();
    j["nextProcess"]["processId"] = info.NextProcess.ProcessId;
    std::wstring nextProcessNameWide(info.NextProcess.ProcessName);
    j["nextProcess"]["processName"] = WideStringToUtf8(nextProcessNameWide);

    return j;
}



std::string FormatBytes(SIZE_T bytes) {
	const char* units[] = { "B", "KB", "MB", "GB" };
	double size = (double)bytes;
	int unit = 0;

	while (size >= 1024 && unit < 3) {
		size /= 1024;
		unit++;
	}

	char buffer[32];
	sprintf_s(buffer, "%.2f %s", size, units[unit]);
	return std::string(buffer);
}

void DisplayProcessInfo(const PROCESS_INFO& info) {
	std::wcout << L"\n=== Process Information ===" << std::endl;
	std::wcout << L"Name: " << info.ProcessName << std::endl;
	std::wcout << L"PID: " << info.ProcessId << std::endl;
	std::wcout << L"Parent PID: " << info.ParentProcessId << std::endl;
	std::wcout << L"Threads: " << info.ThreadCount << std::endl;
	std::wcout << L"Handles: " << info.HandleCount << std::endl;
	std::wcout << L"Base Priority: " << info.BasePriority << std::endl;
	std::wcout << L"Create Time: " << FormatFileTime(info.CreateTime).c_str() << std::endl;

	std::wcout << L"\n--- Memory Information ---" << std::endl;
	std::wcout << L"Working Set: " << FormatBytes(info.WorkingSetSize).c_str() << std::endl;
	std::wcout << L"Peak Working Set: " << FormatBytes(info.PeakWorkingSetSize).c_str() << std::endl;
	std::wcout << L"Virtual Size: " << FormatBytes(info.VirtualSize).c_str() << std::endl;
	std::wcout << L"Peak Virtual Size: " << FormatBytes(info.PeakVirtualSize).c_str() << std::endl;
	std::wcout << L"Pagefile Usage: " << FormatBytes(info.PagefileUsage).c_str() << std::endl;
	std::wcout << L"Page Faults: " << info.PageFaultCount << std::endl;

	std::wcout << L"\n--- I/O Information ---" << std::endl;
	std::wcout << L"Read Operations: " << info.ReadOperationCount << std::endl;
	std::wcout << L"Write Operations: " << info.WriteOperationCount << std::endl;
	std::wcout << L"Other Operations: " << info.OtherOperationCount << std::endl;
	std::wcout << L"Read Transfer: " << info.ReadTransferCount << L" bytes" << std::endl;
	std::wcout << L"Write Transfer: " << info.WriteTransferCount << L" bytes" << std::endl;
	std::wcout << L"Other Transfer: " << info.OtherTransferCount << L" bytes" << std::endl;
}

void HandleListProcesses(const httplib::Request& req, httplib::Response& res) {
    DWORD bytesReturned;

    if (!DeviceIoControl(g_hDevice, IOCTL_LIST_PROCESSES, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
        json errorResponse;
        errorResponse["success"] = false;
        errorResponse["error"] = "Failed to list processes";
        errorResponse["errorCode"] = GetLastError();
        res.status = 500;
        res.set_content(errorResponse.dump(), "application/json");
        return;
    }

    json response;
    response["success"] = true;
    response["message"] = "Processes listed successfully to file";
    response["operation"] = "list-processes";

    res.set_content(response.dump(), "application/json");
    res.status = response["success"] ? 200 : 500;
}

void HandleGetProcessCount(const httplib::Request& req, httplib::Response& res) {
    PROCESS_COUNT_RESPONSE countResponse;
    DWORD bytesReturned;

    if (!DeviceIoControl(g_hDevice, IOCTL_GET_PROCESS_COUNT, NULL, 0,
        &countResponse, sizeof(countResponse), &bytesReturned, NULL)) {
        json errorResponse;
        errorResponse["success"] = false;
        errorResponse["error"] = "Failed to get process count";
        errorResponse["errorCode"] = GetLastError();
        res.status = 500;
        res.set_content(errorResponse.dump(), "application/json");
        return;
    }

    json response;
    response["success"] = true;
    response["processCount"] = countResponse.ProcessCount;
    response["operation"] = "process-count";
    res.set_content(response.dump(), "application/json");
}

void HandleGetProcessByIndex(const httplib::Request& req, httplib::Response& res) {
    json response;

    if (g_hDevice == INVALID_HANDLE_VALUE) {
        response["success"] = false;
        response["error"] = "Driver not connected";
        res.set_content(response.dump(), "application/json");
        res.status = 500;
        return;
    }

    json requestJson;
    try {
        requestJson = json::parse(req.body);
    } catch (const std::exception& e) {
        response["success"] = false;
        response["error"] = "Invalid JSON in request body";
        res.set_content(response.dump(), "application/json");
        res.status = 400;
        return;
    }

    if (!requestJson.contains("index")) {
        response["success"] = false;
        response["error"] = "Missing 'index' parameter";
        res.set_content(response.dump(), "application/json");
        res.status = 400;
        return;
    }

    ULONG index = requestJson["index"];
    PROCESS_REQUEST request = { 0, index, 0 };
    PROCESS_INFO processInfo;
    DWORD bytesReturned;

    if (DeviceIoControl(g_hDevice, IOCTL_GET_PROCESS_BY_INDEX, &request, sizeof(request),
        &processInfo, sizeof(processInfo), &bytesReturned, NULL)) {
        response["success"] = true;
        response["processInfo"] = ProcessInfoToJson(processInfo);
    } else {
        response["success"] = false;
        response["error"] = "Failed to get process by index or invalid index";
    }

    res.set_content(response.dump(), "application/json");
    res.status = response["success"] ? 200 : 500;
}

void HandleGetProcessByPid(const httplib::Request& req, httplib::Response& res) {
    json response;

    if (g_hDevice == INVALID_HANDLE_VALUE) {
        response["success"] = false;
        response["error"] = "Driver not connected";
        res.set_content(response.dump(), "application/json");
        res.status = 500;
        return;
    }

    json requestJson;
    try {
        requestJson = json::parse(req.body);
    } catch (const std::exception& e) {
        response["success"] = false;
        response["error"] = "Invalid JSON in request body";
        res.set_content(response.dump(), "application/json");
        res.status = 400;
        return;
    }

    if (!requestJson.contains("pid")) {
        response["success"] = false;
        response["error"] = "Missing 'pid' parameter";
        res.set_content(response.dump(), "application/json");
        res.status = 400;
        return;
    }

    ULONG pid = requestJson["pid"];
    PROCESS_REQUEST request = { 1, 0, pid };
    PROCESS_INFO processInfo;
    DWORD bytesReturned;

    if (DeviceIoControl(g_hDevice, IOCTL_GET_PROCESS_BY_PID, &request, sizeof(request),
        &processInfo, sizeof(processInfo), &bytesReturned, NULL)) {
        response["success"] = true;
        response["processInfo"] = ProcessInfoToJson(processInfo);
    } else {
        response["success"] = false;
        response["error"] = "Failed to get process by PID or process not found";
    }

    res.set_content(response.dump(), "application/json");
    res.status = response["success"] ? 200 : 500;
}

void HandleIterateProcesses(const httplib::Request& req, httplib::Response& res) {
    json response;

    if (g_hDevice == INVALID_HANDLE_VALUE) {
        response["success"] = false;
        response["error"] = "Driver not connected";
        res.set_content(response.dump(), "application/json");
        res.status = 500;
        return;
    }

    PROCESS_COUNT_RESPONSE countResponse;
    DWORD bytesReturned;

    if (!DeviceIoControl(g_hDevice, IOCTL_GET_PROCESS_COUNT, NULL, 0,
        &countResponse, sizeof(countResponse), &bytesReturned, NULL)) {
        response["success"] = false;
        response["error"] = "Failed to get process count";
        res.set_content(response.dump(), "application/json");
        res.status = 500;
        return;
    }

    json processes = json::array();

    for (ULONG i = 0; i < countResponse.ProcessCount; i++) {
        PROCESS_REQUEST request = { 0, i, 0 };
        PROCESS_INFO processInfo;

        if (DeviceIoControl(g_hDevice, IOCTL_GET_PROCESS_BY_INDEX, &request, sizeof(request),
            &processInfo, sizeof(processInfo), &bytesReturned, NULL)) {

            json processBasic;
            std::wstring processNameWide(processInfo.ProcessName);
            //processBasic["index"] = i;
            //processBasic["processName"] = WideStringToUtf8(processNameWide);
            //processBasic["processId"] = processInfo.ProcessId;
            processBasic = ProcessInfoToJson(processInfo);
            //DisplayProcessInfo(processInfo);

            processes.push_back(processBasic);
        }
    }

    response["success"] = true;
    response["processCount"] = countResponse.ProcessCount;
    response["processes"] = processes;

    res.set_content(response.dump(), "application/json");
    res.status = 200;
}

void HandleSetApiUrl(const httplib::Request& req, httplib::Response& res) {
    json response;

    json requestJson;
    try {
        requestJson = json::parse(req.body);
    } catch (const std::exception& e) {
        response["success"] = false;
        response["error"] = "Invalid JSON in request body";
        res.set_content(response.dump(), "application/json");
        res.status = 400;
        return;
    }

    if (!requestJson.contains("apiUrl")) {
        response["success"] = false;
        response["error"] = "Missing 'apiUrl' parameter";
        res.set_content(response.dump(), "application/json");
        res.status = 400;
        return;
    }

    g_remoteApiUrl = requestJson["apiUrl"];
    response["success"] = true;
    response["message"] = "API URL updated successfully (runtime only)";
    response["newApiUrl"] = g_remoteApiUrl;
    response["note"] = "Use /webhook/save-config to persist to registry";

    res.set_content(response.dump(), "application/json");
    res.status = 200;
}

void HandleSaveConfig(const httplib::Request& req, httplib::Response& res) {
    json response;

    json requestJson;
    try {
        requestJson = json::parse(req.body);
    } catch (const std::exception& e) {
        response["success"] = false;
        response["error"] = "Invalid JSON in request body";
        res.set_content(response.dump(), "application/json");
        res.status = 400;
        return;
    }

    std::string apiUrl = requestJson.value("apiUrl", "");
    std::string serverHost = requestJson.value("serverHost", "");
    int serverPort = requestJson.value("serverPort", 0);

    if (!apiUrl.empty()) {
        g_remoteApiUrl = apiUrl;
    }
    if (!serverHost.empty()) {
        g_serverHost = serverHost;
    }
    if (serverPort > 0) {
        g_serverPort = serverPort;
    }

    bool success = SaveConfigurationToRegistry(apiUrl, serverHost, serverPort);

    response["success"] = success;
    if (success) {
        response["message"] = "Configuration saved to registry successfully";
        response["savedValues"] = {
            {"apiUrl", apiUrl.empty() ? "not changed" : apiUrl},
            {"serverHost", serverHost.empty() ? "not changed" : serverHost},
            {"serverPort", serverPort == 0 ? "not changed" : std::to_string(serverPort)}
        };
    } else {
        response["error"] = "Failed to save configuration to registry";
    }

    response["currentConfig"] = {
        {"apiUrl", g_remoteApiUrl},
        {"serverHost", g_serverHost},
        {"serverPort", g_serverPort}
    };

    res.set_content(response.dump(), "application/json");
    res.status = success ? 200 : 500;
}

void HandleGetConfig(const httplib::Request& req, httplib::Response& res) {
    json response;

    response["success"] = true;
    response["configuration"] = {
        {"apiUrl", g_remoteApiUrl},
        {"serverHost", g_serverHost},
        {"serverPort", g_serverPort}
    };

    response["configurationSources"] = {
        {"priority1", "Environment Variables"},
        {"priority2", "Windows Registry (HKEY_LOCAL_MACHINE\\SOFTWARE\\PeepersDriver)"},
        {"priority3", "Default Values"}
    };

    response["environmentVariables"] = {
        {"PEEPERS_API_URL", "Remote API URL"},
        {"PEEPERS_SERVER_HOST", "Webhook server host"},
        {"PEEPERS_SERVER_PORT", "Webhook server port"}
    };

    response["registryValues"] = {
        {"RemoteApiUrl", "Remote API URL"},
        {"ServerHost", "Webhook server host"},
        {"ServerPort", "Webhook server port"}
    };

    res.set_content(response.dump(2), "application/json");
    res.status = 200;
}

int main() {
    std::cout << "=== Process Monitor Webhook Server ===" << std::endl;

    LoadConfiguration();

    g_hDevice = CreateFileA("\\\\.\\ExampleDriver",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);

    if (g_hDevice == INVALID_HANDLE_VALUE) {
        std::cout << "Failed to open device. Error: " << GetLastError() << std::endl;
        std::cout << "Make sure the kernel driver is loaded." << std::endl;
        return 1;
    }

    std::cout << "Connected to kernel driver successfully!" << std::endl;

    PROCESS_COUNT_RESPONSE countResponse;
    DWORD bytesReturned;

    if (!DeviceIoControl(g_hDevice, IOCTL_GET_PROCESS_COUNT, NULL, 0,
        &countResponse, sizeof(countResponse), &bytesReturned, NULL)) {
        std::cout << "Failed to get process count from driver" << std::endl;
        CloseHandle(g_hDevice);
        return 1;
    }

    std::cout << "Driver connection verified. Total processes: " << countResponse.ProcessCount << std::endl;

    httplib::Server server;

    server.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return httplib::Server::HandlerResponse::Unhandled;
    });

    server.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        return;
    });

    server.Post("/webhook/list-processes", HandleListProcesses);
    server.Post("/webhook/process-count", HandleGetProcessCount);
    server.Post("/webhook/process-by-index", HandleGetProcessByIndex);
    server.Post("/webhook/process-by-pid", HandleGetProcessByPid);
    server.Post("/webhook/iterate-processes", HandleIterateProcesses);
    server.Post("/webhook/set-api-url", HandleSetApiUrl);
    server.Post("/webhook/save-config", HandleSaveConfig);
    server.Get("/webhook/get-config", HandleGetConfig);

    // Health check endpoint
    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        json response;
        response["status"] = "healthy";
        response["service"] = "Process Monitor API Server";
        response["driverConnected"] = (g_hDevice != INVALID_HANDLE_VALUE);
        response["serverHost"] = g_serverHost;
        response["serverPort"] = g_serverPort;
        res.set_content(response.dump(), "application/json");
    });

    // Status endpoint with detailed information
    server.Get("/status", [&countResponse](const httplib::Request&, httplib::Response& res) {
        json response;
        response["service"] = "Process Monitor API Server";
        response["driverConnected"] = (g_hDevice != INVALID_HANDLE_VALUE);
        response["configuration"] = {
            {"serverHost", g_serverHost},
            {"serverPort", g_serverPort}
        };

        if (g_hDevice != INVALID_HANDLE_VALUE) {
            DWORD bytesReturned;
            PROCESS_COUNT_RESPONSE currentCount;
            if (DeviceIoControl(g_hDevice, IOCTL_GET_PROCESS_COUNT, NULL, 0,
                &currentCount, sizeof(currentCount), &bytesReturned, NULL)) {
                response["currentProcessCount"] = currentCount.ProcessCount;
            }
        }

        response["availableEndpoints"] = {
            "POST /webhook/list-processes - List all processes to file",
            "POST /webhook/process-count - Get total process count",
            "POST /webhook/process-by-index - Get process by index",
            "POST /webhook/process-by-pid - Get process by PID",
            "POST /webhook/iterate-processes - Get all processes summary",
            "POST /webhook/set-api-url - Set remote API URL (runtime)",
            "POST /webhook/save-config - Save configuration to registry",
            "GET /webhook/get-config - Get current configuration",
            "GET /health - Health check",
            "GET /status - Detailed status"
        };

        res.set_content(response.dump(2), "application/json");
    });

    std::cout << "\n=== API Server Configuration ===" << std::endl;
    std::cout << "Host: " << g_serverHost << std::endl;
    std::cout << "Port: " << g_serverPort << std::endl;
    std::cout << "\n=== Available Endpoints ===" << std::endl;
    std::cout << "POST /webhook/list-processes - List all processes to file (returns success message)" << std::endl;
    std::cout << "POST /webhook/process-count - Get total process count (returns count)" << std::endl;
    std::cout << "POST /webhook/process-by-index - Get process by index (returns process info)" << std::endl;
    std::cout << "POST /webhook/process-by-pid - Get process by PID (returns process info)" << std::endl;
    std::cout << "POST /webhook/iterate-processes - Get all processes summary (returns process list)" << std::endl;
    std::cout << "POST /webhook/set-api-url - Set remote API URL (returns confirmation)" << std::endl;
    std::cout << "POST /webhook/save-config - Save configuration to registry (returns saved values)" << std::endl;
    std::cout << "GET /webhook/get-config - Get current configuration (returns config)" << std::endl;
    std::cout << "GET /health - Health check (returns status)" << std::endl;
    std::cout << "GET /status - Detailed status information (returns full status)" << std::endl;

    std::cout << "\nStarting API server..." << std::endl;

    std::thread serverThread([&server]() {
        if (!server.listen(g_serverHost.c_str(), g_serverPort)) {
            std::cout << "Failed to start server on " << g_serverHost << ":" << g_serverPort << std::endl;
        }
        });

    std::cout << "Webhook server started successfully!" << std::endl;
    std::cout << "Server is listening on http://" << g_serverHost << ":" << g_serverPort << std::endl;

    std::string input;
    while (std::getline(std::cin, input)) {
        if (input == "q" || input == "quit") {
            break;
        }
        std::cout << "Press 'q' and Enter to quit..." << std::endl;
    }

    std::cout << "Shutting down server..." << std::endl;
    server.stop();

    if (serverThread.joinable()) {
        serverThread.join();
    }

    if (g_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hDevice);
    }

    std::cout << "Server stopped. Goodbye!" << std::endl;
    return 0;
}