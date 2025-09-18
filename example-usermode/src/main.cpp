#include <windows.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <locale>
#include <codecvt>
#include <thread>
#include <httplib.h>
#include <nlohmann/json.hpp>

#define IOCTL_LIST_PROCESSES CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_GET_PROCESS_COUNT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_GET_PROCESS_BY_INDEX CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_GET_PROCESS_BY_PID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_READ_DATA)

typedef struct _PROCESS_INFO {
	ULONG ProcessId;
	ULONG ParentProcessId;
	WCHAR ProcessName[64];
	ULONG ThreadCount;
	ULONG HandleCount;
	LONG BasePriority;
	LARGE_INTEGER CreateTime;
	LARGE_INTEGER UserTime;
	LARGE_INTEGER KernelTime;

	// Memory information
	SIZE_T WorkingSetSize;
	SIZE_T PeakWorkingSetSize;
	SIZE_T VirtualSize;
	SIZE_T PeakVirtualSize;
	SIZE_T PagefileUsage;
	SIZE_T PeakPagefileUsage;
	SIZE_T PageFaultCount;

	// I/O information
	ULONGLONG ReadOperationCount;
	ULONGLONG WriteOperationCount;
	ULONGLONG OtherOperationCount;
	ULONGLONG ReadTransferCount;
	ULONGLONG WriteTransferCount;
	ULONGLONG OtherTransferCount;

	PVOID CurrentProcessAddress;
	PVOID NextProcessAddress;
	PVOID PreviousProcessAddress;
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

// ... existing code ...

using json = nlohmann::json;

// Global variables
HANDLE g_hDevice = INVALID_HANDLE_VALUE;
std::string g_remoteApiUrl = "http://localhost:8080/api/process-data"; // Default URL

// Helper function to convert wide string to UTF-8
std::string WideStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Convert PROCESS_INFO to JSON
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

    // Time information
    j["createTime"] = FormatFileTime(info.CreateTime);
    j["userTime"] = info.UserTime.QuadPart;
    j["kernelTime"] = info.KernelTime.QuadPart;

    // Memory information
    j["memory"] = {
        {"workingSetSize", info.WorkingSetSize},
        {"peakWorkingSetSize", info.PeakWorkingSetSize},
        {"virtualSize", info.VirtualSize},
        {"peakVirtualSize", info.PeakVirtualSize},
        {"pagefileUsage", info.PagefileUsage},
        {"peakPagefileUsage", info.PeakPagefileUsage},
        {"pageFaultCount", info.PageFaultCount}
    };

    // I/O information
    j["io"] = {
        {"readOperationCount", info.ReadOperationCount},
        {"writeOperationCount", info.WriteOperationCount},
        {"otherOperationCount", info.OtherOperationCount},
        {"readTransferCount", info.ReadTransferCount},
        {"writeTransferCount", info.WriteTransferCount},
        {"otherTransferCount", info.OtherTransferCount}
    };

    // Address information (as hex strings)
    std::stringstream ss;
    ss << "0x" << std::hex << (uintptr_t)info.CurrentProcessAddress;
    j["currentProcessAddress"] = ss.str();

    ss.str("");
    ss << "0x" << std::hex << (uintptr_t)info.PreviousProcessAddress;
    j["previousProcessAddress"] = ss.str();

    ss.str("");
    ss << "0x" << std::hex << (uintptr_t)info.NextProcessAddress;
    j["nextProcessAddress"] = ss.str();

    return j;
}

// Send data to remote API
bool SendToRemoteApi(const json& data, const std::string& endpoint = "") {
    try {
        httplib::Client client(g_remoteApiUrl.c_str());
        client.set_connection_timeout(5, 0); // 5 seconds
        client.set_read_timeout(10, 0); // 10 seconds

        std::string jsonStr = data.dump();
        std::string url = endpoint.empty() ? "/" : endpoint;

        auto res = client.Post(url.c_str(), jsonStr, "application/json");

        if (res && res->status == 200) {
            std::cout << "Data sent successfully to remote API" << std::endl;
            return true;
        } else {
            std::cout << "Failed to send data to remote API. Status: "
                      << (res ? res->status : -1) << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cout << "Exception sending data to remote API: " << e.what() << std::endl;
        return false;
    }
}

// ... existing code ...

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

int main() {
    std::cout << "=== Process Monitor Webhook Server ===" << std::endl;

    // Open connection to kernel driver
    g_hDevice = CreateFileA("\\\\.\\ExampleDriver",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);

    if (g_hDevice == INVALID_HANDLE_VALUE) {
        std::cout << "Failed to open device. Error: " << GetLastError() << std::endl;
        std::cout << "Make sure the kernel driver is loaded." << std::endl;
        return 1;
    }

    std::cout << "Connected to kernel driver successfully!" << std::endl;

    // Test connection by getting process count
    PROCESS_COUNT_RESPONSE countResponse;
    DWORD bytesReturned;

    if (!DeviceIoControl(g_hDevice, IOCTL_GET_PROCESS_COUNT, NULL, 0,
        &countResponse, sizeof(countResponse), &bytesReturned, NULL)) {
        std::cout << "Failed to get process count from driver" << std::endl;
        CloseHandle(g_hDevice);
        return 1;
    }

    std::cout << "Driver connection verified. Total processes: " << countResponse.ProcessCount << std::endl;

    // Create HTTP server
    httplib::Server server;

    // Enable CORS for all origins (adjust as needed for security)
    server.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // Handle OPTIONS requests for CORS preflight
    server.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        return;
    });

    // Register webhook endpoints
    server.Post("/webhook/list-processes", HandleListProcesses);
    server.Post("/webhook/process-count", HandleGetProcessCount);
    server.Post("/webhook/process-by-index", HandleGetProcessByIndex);
    server.Post("/webhook/process-by-pid", HandleGetProcessByPid);
    server.Post("/webhook/iterate-processes", HandleIterateProcesses);
    server.Post("/webhook/set-api-url", HandleSetApiUrl);

    // Health check endpoint
    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        json response;
        response["status"] = "healthy";
        response["service"] = "Process Monitor Webhook";
        response["driverConnected"] = (g_hDevice != INVALID_HANDLE_VALUE);
        response["remoteApiUrl"] = g_remoteApiUrl;
        res.set_content(response.dump(), "application/json");
    });

    // Status endpoint with detailed information
    server.Get("/status", [&countResponse](const httplib::Request&, httplib::Response& res) {
        json response;
        response["service"] = "Process Monitor Webhook Server";
        response["driverConnected"] = (g_hDevice != INVALID_HANDLE_VALUE);
        response["remoteApiUrl"] = g_remoteApiUrl;

        if (g_hDevice != INVALID_HANDLE_VALUE) {
            DWORD bytesReturned;
            PROCESS_COUNT_RESPONSE currentCount;
            if (DeviceIoControl(g_hDevice, IOCTL_GET_PROCESS_COUNT, NULL, 0,
                &currentCount, sizeof(currentCount), &bytesReturned, NULL)) {
                response["currentProcessCount"] = currentCount.ProcessCount;
            }
        }

        response["availableEndpoints"] = {
            "POST /webhook/list-processes",
            "POST /webhook/process-count",
            "POST /webhook/process-by-index",
            "POST /webhook/process-by-pid",
            "POST /webhook/iterate-processes",
            "POST /webhook/set-api-url",
            "GET /health",
            "GET /status"
        };

        res.set_content(response.dump(2), "application/json");
    });

    // Set server configuration
    const char* host = "0.0.0.0";
    int port = 8888;

    std::cout << "\n=== Webhook Server Configuration ===" << std::endl;
    std::cout << "Host: " << host << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Remote API URL: " << g_remoteApiUrl << std::endl;
    std::cout << "\n=== Available Endpoints ===" << std::endl;
    std::cout << "POST /webhook/list-processes - List all processes to file" << std::endl;
    std::cout << "POST /webhook/process-count - Get total process count" << std::endl;
    std::cout << "POST /webhook/process-by-index - Get process by index (JSON: {\"index\": N})" << std::endl;
    std::cout << "POST /webhook/process-by-pid - Get process by PID (JSON: {\"pid\": N})" << std::endl;
    std::cout << "POST /webhook/iterate-processes - Get all processes summary" << std::endl;
    std::cout << "POST /webhook/set-api-url - Set remote API URL (JSON: {\"apiUrl\": \"url\"})" << std::endl;
    std::cout << "GET /health - Health check" << std::endl;
    std::cout << "GET /status - Detailed status information" << std::endl;

    std::cout << "\nStarting webhook server..." << std::endl;

    // Start server in a separate thread so we can handle shutdown gracefully
    std::thread serverThread([&server, host, port]() {
        if (!server.listen(host, port)) {
            std::cout << "Failed to start server on " << host << ":" << port << std::endl;
        }
    });

    std::cout << "Webhook server started successfully!" << std::endl;
    std::cout << "Server is listening on http://" << host << ":" << port << std::endl;
    std::cout << "\nPress 'q' and Enter to quit..." << std::endl;

    // Wait for user input to quit
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

    // Clean up
    if (g_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hDevice);
    }

    std::cout << "Server stopped. Goodbye!" << std::endl;
    return 0;
}