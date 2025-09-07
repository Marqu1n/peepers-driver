#include <windows.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <fstream>

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
	//KPRIORITY BasePriority;
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
	//std::wcout << L"Base Priority: " << info.BasePriority << std::endl;
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

	std::wcout << L"Current Process Address: 0x" << std::hex << info.CurrentProcessAddress << std::dec << std::endl;
	std::wcout << L"Previous Process Address: 0x" << std::hex << info.PreviousProcessAddress << std::dec << std::endl;
	std::wcout << L"Next Process Address: 0x" << std::hex << info.NextProcessAddress << std::dec << std::endl;
}

int main() {
	HANDLE hDevice = CreateFileA("\\\\.\\ExampleDriver",
		GENERIC_READ | GENERIC_WRITE,
		0, NULL, OPEN_EXISTING, 0, NULL);

	if (hDevice == INVALID_HANDLE_VALUE) {
		std::cout << "Failed to open device. Error: " << GetLastError() << std::endl;
		return 1;
	}

	std::cout << "Connected to kernel driver successfully!" << std::endl;

	// Get process count
	PROCESS_COUNT_RESPONSE countResponse;
	DWORD bytesReturned;

	if (!DeviceIoControl(hDevice, IOCTL_GET_PROCESS_COUNT, NULL, 0,
		&countResponse, sizeof(countResponse), &bytesReturned, NULL)) {
		std::cout << "Failed to get process count" << std::endl;
		CloseHandle(hDevice);
		return 1;
	}

	std::cout << "Total processes: " << countResponse.ProcessCount << std::endl;

	std::string command;
	while (true) {
		std::cout << "\nCommands:" << std::endl;
		std::cout << "  list - List all processes to file" << std::endl;
		std::cout << "  count - Get process count" << std::endl;
		std::cout << "  index <n> - Get process by index (0-" << (countResponse.ProcessCount - 1) << ")" << std::endl;
		std::cout << "  pid <pid> - Get process by PID" << std::endl;
		std::cout << "  iterate - Iterate through all processes" << std::endl;
		std::cout << "  quit - Exit" << std::endl;
		std::cout << "\nEnter command: ";

		std::getline(std::cin, command);

		if (command == "quit") {
			break;
		}
		else if (command == "list") {
			if (DeviceIoControl(hDevice, IOCTL_LIST_PROCESSES, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
				std::cout << "Process list written to \\SystemRoot\\KernelProcessList.txt" << std::endl;
			}
			else {
				std::cout << "Failed to list processes" << std::endl;
			}
		}
		else if (command == "count") {
			if (DeviceIoControl(hDevice, IOCTL_GET_PROCESS_COUNT, NULL, 0,
				&countResponse, sizeof(countResponse), &bytesReturned, NULL)) {
				std::cout << "Process count: " << countResponse.ProcessCount << std::endl;
			}
			else {
				std::cout << "Failed to get process count" << std::endl;
			}
		}
		else if (command.substr(0, 5) == "index") {
			try {
				ULONG index = std::stoul(command.substr(6));
				PROCESS_REQUEST request = { 0, index, 0 };
				PROCESS_INFO processInfo;

				if (DeviceIoControl(hDevice, IOCTL_GET_PROCESS_BY_INDEX, &request, sizeof(request),
					&processInfo, sizeof(processInfo), &bytesReturned, NULL)) {
					DisplayProcessInfo(processInfo);
				}
				else {
					std::cout << "Failed to get process by index or invalid index" << std::endl;
				}
			}
			catch (...) {
				std::cout << "Invalid index format" << std::endl;
			}
		}
		else if (command.substr(0, 3) == "pid") {
			try {
				ULONG pid = std::stoul(command.substr(4));
				PROCESS_REQUEST request = { 1, 0, pid };
				PROCESS_INFO processInfo;

				if (DeviceIoControl(hDevice, IOCTL_GET_PROCESS_BY_PID, &request, sizeof(request),
					&processInfo, sizeof(processInfo), &bytesReturned, NULL)) {
					DisplayProcessInfo(processInfo);
				}
				else {
					std::cout << "Failed to get process by PID or process not found" << std::endl;
				}
			}
			catch (...) {
				std::cout << "Invalid PID format" << std::endl;
			}
		}
		else if (command == "iterate") {
			std::cout << "Iterating through all processes..." << std::endl;
			for (ULONG i = 0; i < countResponse.ProcessCount; i++) {
				PROCESS_REQUEST request = { 0, i, 0 };
				PROCESS_INFO processInfo;

				if (DeviceIoControl(hDevice, IOCTL_GET_PROCESS_BY_INDEX, &request, sizeof(request),
					&processInfo, sizeof(processInfo), &bytesReturned, NULL)) {
					std::wcout << L"[" << i << L"] " << processInfo.ProcessName
						<< L" (PID: " << processInfo.ProcessId << L")" << L" Prev Address:" << processInfo.PreviousProcessAddress << L" Address:" << processInfo.CurrentProcessAddress << L" Next address:" << processInfo.NextProcessAddress  << std::endl;
				}
			}


			char systemRoot[MAX_PATH];
			GetEnvironmentVariableA("SystemRoot", systemRoot, MAX_PATH);
			std::string logFile = std::string(systemRoot) + "\\process_list.txt";

			// Cria um ofstream para escrever no arquivo
			std::wofstream file(logFile, std::ios::out);
			if (!file.is_open()) {
				std::cerr << "Não foi possível abrir o arquivo para escrita: " << logFile << std::endl;
				return 1;
			}

			file << L"Iterating through all processes..." << std::endl;

			for (ULONG i = 0; i < countResponse.ProcessCount; i++) {
				PROCESS_REQUEST request = { 0, i, 0 };
				PROCESS_INFO processInfo;

				if (DeviceIoControl(hDevice, IOCTL_GET_PROCESS_BY_INDEX, &request, sizeof(request),
					&processInfo, sizeof(processInfo), &bytesReturned, NULL)) {
					file << L"[" << i << L"] " << processInfo.ProcessName
						<< L" (PID: " << processInfo.ProcessId << L")"
						<< L" Prev Address:" << processInfo.PreviousProcessAddress
						<< L" Address:" << processInfo.CurrentProcessAddress
						<< L" Next address:" << processInfo.NextProcessAddress
						//<< L" Mode:" << processInfo.ListLinkageMode
						<< std::endl;
				}
			}

			file.close();
			std::wcout << L"Process list saved to " << logFile.c_str() << std::endl;
		}
		else {
			std::cout << "Unknown command" << std::endl;
		}
	}

	CloseHandle(hDevice);
	return 0;
}