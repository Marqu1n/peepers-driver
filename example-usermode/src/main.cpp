#include <windows.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>

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

    namespace fs = std::filesystem;

    if (success) {
        // Caminho da pasta onde os logs ficarão
        std::string pasta = "C:\\LogsProcessos\\";
        fs::create_directories(pasta); // Cria a pasta se não existir

        // Pega a data/hora atual
        auto agora = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(agora);

        std::tm tm{};
        localtime_s(&tm, &t);

        // Monta o nome do arquivo com a data
        std::ostringstream oss;
        oss << pasta
            << "processos_"
            << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S")
            << ".txt";

        std::string nomeArquivo = oss.str();

        std::ofstream outFile(nomeArquivo, std::ios::out);

        if (!outFile.is_open()) {
            std::cerr << "Não foi possível abrir o arquivo para escrita!\n";
        }
        else {
            size_t count = bytesReturned / sizeof(ProcessInfo);
            for (size_t i = 0; i < count; i++) {
                outFile << "PID: " << buffer[i].pid
                    << ", Parent PID: " << buffer[i].parentPid
                    << ", Session: " << buffer[i].sessionId
                    << ", Name: " << buffer[i].imageName << "\n";
            }
            outFile.close();
            std::cout << "Resultado salvo em " << nomeArquivo << "\n";
        }
    }
    else {
        std::cerr << "DeviceIoControl failed. Error: " << GetLastError() << "\n";
    }


	CloseHandle(hDevice);

	return 0;
}