#include <windows.h>
#include <iostream>

namespace codes {
	constexpr ULONG IOCTL_LIST_PROCESSES = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA);
}

struct ProcessInfo {
	ULONG pid;
	ULONG parentPid;
	ULONG sessionId;
	CHAR imageName[16];
};

int main() {
	
	HANDLE hDevice = CreateFileW(L"\\DosDevices\\ExampleDriver", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hDevice == INVALID_HANDLE_VALUE) {
		std::cerr << "[-] Failed to open device handle. Error: " << GetLastError() << std::endl;
		return 1;
	}

	ProcessInfo buffer[64];  // enough for 64 processes
	DWORD bytesReturned = 0;

	BOOL success = DeviceIoControl(
		hDevice,
		codes::IOCTL_LIST_PROCESSES,
		nullptr,                    // no input buffer
		0,                          // input buffer size
		buffer,                     // output buffer
		sizeof(buffer),             // output buffer size
		&bytesReturned,
		nullptr
	);

	if (success) {
		size_t count = bytesReturned / sizeof(ProcessInfo);
		for (size_t i = 0; i < count; i++) {
			std::cout << "PID: " << buffer[i].pid
				<< ", Parent PID: " << buffer[i].parentPid
				<< ", Session: " << buffer[i].sessionId
				<< ", Name: " << buffer[i].imageName << "\n";
		}
	}
	else {
		std::cerr << "DeviceIoControl failed. Error: " << GetLastError() << "\n";
	}
	CloseHandle(hDevice);

	return 0;
}