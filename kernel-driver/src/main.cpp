#include <ntddk.h>
#include <ntstrsafe.h>
#include <stdlib.h>
#include <wdf.h>

_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
extern "C" DRIVER_DISPATCH DispatchDeviceControl;

void DebugPrint(PCSTR text) {
#ifndef DEBUG
	UNREFERENCED_PARAMETER(text);
#endif // DEBUG

	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, text));
}

namespace codes {
	constexpr ULONG IOCTL_LIST_PROCESSES = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA);
	constexpr ULONG IOCTL_GET_PROCESS_COUNT = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_DATA);
	constexpr ULONG IOCTL_GET_PROCESS_BY_INDEX = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA);
	constexpr ULONG IOCTL_GET_PROCESS_BY_PID = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_READ_DATA);
}

NTSTATUS DispatchCreateClose(PDEVICE_OBJECT deviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

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
	ADJACENT_PROCESS_INFO NextProcess;
	ADJACENT_PROCESS_INFO PreviousProcess;
	//PVOID NextProcessAddress;
	//PVOID PreviousProcessAddress;
} PROCESS_INFO, * PPROCESS_INFO;

// Request structures for usermode communication
typedef struct _PROCESS_REQUEST {
	ULONG RequestType; // 0 = by index, 1 = by PID
	ULONG Index;       // Process index (for iteration)
	ULONG ProcessId;   // Process ID (for specific lookup)
} PROCESS_REQUEST, * PPROCESS_REQUEST;

typedef struct _PROCESS_COUNT_RESPONSE {
	ULONG ProcessCount;
	NTSTATUS Status;
} PROCESS_COUNT_RESPONSE, * PPROCESS_COUNT_RESPONSE;

#define POOL_TAG 'enoN'

// Undocumented functions and structures
extern "C" NTKERNELAPI HANDLE PsGetProcessId(PEPROCESS Process);
extern "C" NTKERNELAPI HANDLE PsGetProcessInheritedFromUniqueProcessId(PEPROCESS Process);
extern "C" NTKERNELAPI PUCHAR PsGetProcessImageFileName(PEPROCESS Process);
extern "C" NTKERNELAPI PPEB PsGetProcessPeb(PEPROCESS Process);
extern "C" NTKERNELAPI ULONG PsGetProcessSessionId(PEPROCESS Process);

// Offsets para EPROCESS (Windows 10/11 x64)
// Estes offsets podem variar entre versões do Windows
#define EPROCESS_ACTIVEPROCESSLINKS_OFFSET 0x1d8  // ActiveProcessLinks
#define EPROCESS_THREADLISTHEAD_OFFSET 0x370      // ThreadListHead
//#define EPROCESS_HANDLECOUNT_OFFSET 0x578         // HandleCount
#define EPROCESS_CREATETIME_OFFSET 0x1f8          // CreateTime
#define EPROCESS_EXITTIME_OFFSET 0x5c0            // ExitTime
#define EPROCESS_RUNDOWNPROTECT_OFFSET 0x1e8      // RundownProtect
#define EPROCESS_VMS_OFFSET 0x400                 // Vm (Virtual Memory Stats)

typedef struct _MMSUPPORT_FLAGS
{
	union
	{
		struct
		{
			UCHAR WorkingSetType : 4;                                         //0x0
			UCHAR Reserved0 : 2;                                              //0x0
			UCHAR MaximumWorkingSetHard : 1;                                  //0x0
			UCHAR MinimumWorkingSetHard : 1;                                  //0x0
			UCHAR Reserved1 : 1;                                              //0x1
			UCHAR TrimmerState : 2;                                           //0x1
			UCHAR LinearAddressProtected : 1;                                 //0x1
			UCHAR PageStealers : 4;                                           //0x1
		};
		struct
		{
			USHORT u1;                                                      //0x0
			UCHAR MemoryPriority;                                           //0x2
			union
			{
				struct
				{
					UCHAR WsleDeleted : 1;                                    //0x3
					UCHAR SvmEnabled : 1;                                     //0x3
					UCHAR ForceAge : 1;                                       //0x3
					UCHAR ForceTrim : 1;                                      //0x3
					UCHAR CommitReleaseState : 2;                             //0x3
					UCHAR Reserved2 : 2;                                      //0x3
				};
				UCHAR u2;                                                   //0x3
			};
		};
		ULONG EntireFlags;                                                  //0x0
	};
};

typedef struct _MMSUPPORT_INSTANCE
{
	ULONG NextPageColor;                                                    //0x0
	volatile ULONG PageFaultCount;                                          //0x4
	ULONGLONG TrimmedPageCount;                                             //0x8
	struct _MMWSL_INSTANCE* VmWorkingSetList;                               //0x10
	struct _LIST_ENTRY WorkingSetExpansionLinks;                            //0x18
	volatile ULONGLONG AgeDistribution[8];                                  //0x28
	struct _KGATE* ExitOutswapGate;                                         //0x68
	ULONGLONG MinimumWorkingSetSize;                                        //0x70
	ULONGLONG MaximumWorkingSetSize;                                        //0x78
	volatile ULONGLONG WorkingSetLeafSize;                                  //0x80
	volatile ULONGLONG WorkingSetLeafPrivateSize;                           //0x88
	volatile ULONGLONG WorkingSetSize;                                      //0x90
	volatile ULONGLONG WorkingSetPrivateSize;                               //0x98
	volatile ULONGLONG PeakWorkingSetSize;                                  //0xa0
	ULONG HardFaultCount;                                                   //0xa8
	USHORT LastTrimStamp;                                                   //0xac
	USHORT PartitionId;                                                     //0xae
	ULONGLONG SelfmapLock;                                                  //0xb0
	volatile struct _MMSUPPORT_FLAGS Flags;                                 //0xb8
	volatile ULONG InterlockedFlags;                                        //0xbc
} MMSUPPORT_INSTANCE, *PMMSUPPORT_INSTANCE;

struct _MMSUPPORT_SHARED
{
	VOID* WorkingSetLockArray;                                              //0x0
	ULONGLONG ReleasedCommitDebt;                                           //0x8
	ULONGLONG ResetPagesRepurposedCount;                                    //0x10
	VOID* WsSwapSupport;                                                    //0x18
	VOID* CommitReleaseContext;                                             //0x20
	VOID* AccessLog;                                                        //0x28
	volatile ULONGLONG ChargedWslePages;                                    //0x30
	volatile ULONGLONG ActualWslePages;                                     //0x38
	volatile LONG WorkingSetCoreLock;                                       //0x40
	VOID* ShadowMapping;                                                    //0x48
};

typedef struct _MMSUPPORT_FULL
{
	struct _MMSUPPORT_INSTANCE Instance;                                    //0x0
	struct _MMSUPPORT_SHARED Shared;                                        //0xc0
} MMSUPPORT, *PMMSUPPORT;

// Global variables to cache process information
static ULONG g_ProcessCount = 0;
static LARGE_INTEGER g_LastUpdateTime = { 0 };

// Helper function to get next process using ActiveProcessLinks
PEPROCESS GetNextProcessManual(PEPROCESS currentProcess) {
	if (!currentProcess) {
		return NULL;
	}

	// Get ActiveProcessLinks from current process
	PLIST_ENTRY activeProcessLinks = (PLIST_ENTRY)((PUCHAR)currentProcess + EPROCESS_ACTIVEPROCESSLINKS_OFFSET);

	// Get next entry in the list
	PLIST_ENTRY nextEntry = activeProcessLinks->Flink;

	if (!nextEntry) {
		return NULL;
	}

	// Calculate EPROCESS address from ActiveProcessLinks offset
	PEPROCESS nextProcess = (PEPROCESS)((PUCHAR)nextEntry - EPROCESS_ACTIVEPROCESSLINKS_OFFSET);

	return nextProcess;
}

NTSTATUS GetProcessCountEPROCESS(PULONG processCount) {
	ULONG count = 0;
	PEPROCESS currentProcess = NULL;
	PEPROCESS initialProcess = NULL;

	// Obter o processo inicial (normalmente System process)
	currentProcess = PsGetCurrentProcess();
	initialProcess = currentProcess;

	if (!currentProcess) {
		return STATUS_UNSUCCESSFUL;
	}

	// Percorrer a lista circular de processos
	do {
		count++;
		currentProcess = GetNextProcessManual(currentProcess);

		if (!currentProcess) {
			break;
		}

		// Verificar se voltamos ao processo inicial (lista circular)
		if (currentProcess == initialProcess && count > 1) {
			break;
		}

		// Proteção contra loop infinito
		if (count > 10000) {
			break;
		}

	} while (currentProcess && currentProcess != initialProcess);

	*processCount = count;
	g_ProcessCount = count;
	KeQuerySystemTime(&g_LastUpdateTime);

	return STATUS_SUCCESS;
}

NTSTATUS GetProcessByIndexEPROCESS(ULONG index, PPROCESS_INFO processInfo) {
	ULONG currentIndex = 0;
	PEPROCESS currentProcess = NULL;
	PEPROCESS initialProcess = NULL;
	PEPROCESS previousProcess = NULL;

	// Obter o processo inicial
	currentProcess = PsGetCurrentProcess();
	initialProcess = currentProcess;

	if (!currentProcess) {
		return STATUS_UNSUCCESSFUL;
	}

	// Percorrer até o índice desejado
	while (currentIndex < index) {
		processInfo->PreviousProcess.ProcessId = processInfo->ProcessId;
		RtlCopyMemory(processInfo->PreviousProcess.ProcessName,
			processInfo->ProcessName,
			sizeof(processInfo->ProcessName));
		processInfo->PreviousProcess.EProcessAddress = currentProcess;
		previousProcess = currentProcess;
		currentProcess = GetNextProcessManual(currentProcess);

		if (!currentProcess) {
			return STATUS_NOT_FOUND;
		}

		currentIndex++;

		// Verificar se voltamos ao início (não deveria acontecer antes do índice)
		if (currentProcess == initialProcess && currentIndex > 0) {
			return STATUS_NOT_FOUND;
		}

		// Proteção contra loop infinito
		if (currentIndex > 10000) {
			return STATUS_NOT_FOUND;
		}
	}

	if (currentIndex != index) {
		return STATUS_NOT_FOUND;
	}

	// Preencher informações do processo
	RtlZeroMemory(processInfo, sizeof(PROCESS_INFO));

	// Informações básicas
	processInfo->ProcessId = HandleToULong(PsGetProcessId(currentProcess));
	processInfo->ParentProcessId = HandleToULong(PsGetProcessInheritedFromUniqueProcessId(currentProcess));

	// Nome do processo
	PUCHAR imageFileName = PsGetProcessImageFileName(currentProcess);
	if (imageFileName) {
		// Converter de ANSI para Unicode
		ANSI_STRING ansiString;
		UNICODE_STRING unicodeString;
		RtlInitAnsiString(&ansiString, (PCSZ)imageFileName);

		unicodeString.Buffer = processInfo->ProcessName;
		unicodeString.MaximumLength = sizeof(processInfo->ProcessName);
		unicodeString.Length = 0;

		RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
	}

	// Acessar campos usando offsets (método não documentado)
	__try {
		// CreateTime
		PLARGE_INTEGER createTime = (PLARGE_INTEGER)((PUCHAR)currentProcess + EPROCESS_CREATETIME_OFFSET);
		processInfo->CreateTime = *createTime;

		// HandleCount
		//PULONG handleCount = (PULONG)((PUCHAR)currentProcess + EPROCESS_HANDLECOUNT_OFFSET);
		//processInfo->HandleCount = *handleCount;

		// Contar threads manualmente percorrendo ThreadListHead
		PLIST_ENTRY threadListHead = (PLIST_ENTRY)((PUCHAR)currentProcess + EPROCESS_THREADLISTHEAD_OFFSET);
		PLIST_ENTRY currentEntry = threadListHead->Flink;
		ULONG threadCount = 0;

		while (currentEntry != threadListHead && threadCount < 1000) {
			threadCount++;
			currentEntry = currentEntry->Flink;
		}
		processInfo->ThreadCount = threadCount;

		// Informações de memória virtual (usando offset para MMSUPPORT)
		PMMSUPPORT vmSupport = (PMMSUPPORT)((PUCHAR)currentProcess + EPROCESS_VMS_OFFSET);
		processInfo->WorkingSetSize = vmSupport->Instance.WorkingSetSize * PAGE_SIZE;
		processInfo->PeakWorkingSetSize = vmSupport->Instance.PeakWorkingSetSize * PAGE_SIZE;
		processInfo->PageFaultCount = vmSupport->Instance.PageFaultCount;

	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		// Em caso de erro ao acessar memória, definir valores padrão
		processInfo->CreateTime.QuadPart = 0;
		processInfo->HandleCount = 0;
		processInfo->ThreadCount = 0;
		processInfo->WorkingSetSize = 0;
		processInfo->PeakWorkingSetSize = 0;
		processInfo->PageFaultCount = 0;
	}

	// Endereços dos processos
	processInfo->CurrentProcessAddress = currentProcess;

	PEPROCESS nextProcess = GetNextProcessManual(currentProcess);
	if (nextProcess != initialProcess)
	{
		processInfo->NextProcess.ProcessId = HandleToULong(PsGetProcessId(nextProcess));
		PUCHAR imageFileNameNextProcess = PsGetProcessImageFileName(nextProcess);
		if (imageFileNameNextProcess) {
			ANSI_STRING ansiString;
			UNICODE_STRING unicodeString;
			RtlInitAnsiString(&ansiString, (PCSZ)imageFileNameNextProcess);

			unicodeString.Buffer = processInfo->NextProcess.ProcessName;
			unicodeString.MaximumLength = sizeof(processInfo->NextProcess.ProcessName);
			unicodeString.Length = 0;

			RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
		}
		processInfo->NextProcess.EProcessAddress = nextProcess;
	}

	return STATUS_SUCCESS;
}

NTSTATUS GetProcessByPidEPROCESS(ULONG processId, PPROCESS_INFO processInfo) {
	PEPROCESS previousProcess = NULL;
	PEPROCESS currentProcess = NULL;
	PEPROCESS initialProcess = NULL;
	ULONG iterations = 0;

	// Obter o processo inicial
	currentProcess = PsGetCurrentProcess();
	initialProcess = currentProcess;

	if (!currentProcess) {
		return STATUS_UNSUCCESSFUL;
	}

	// Percorrer a lista procurando pelo PID
	do {
		HANDLE currentPid = PsGetProcessId(currentProcess);
		PEPROCESS nextProcess = GetNextProcessManual(currentProcess);

		if (HandleToULong(currentPid) == processId) {
			// Processo encontrado, preencher informações
			RtlZeroMemory(processInfo, sizeof(PROCESS_INFO));

			if (previousProcess != NULL) {
				processInfo->PreviousProcess.ProcessId = HandleToULong(PsGetProcessId(previousProcess));
				PUCHAR previousProcessImageFileName = PsGetProcessImageFileName(previousProcess);
				if (previousProcessImageFileName) {
					ANSI_STRING ansiString;
					UNICODE_STRING unicodeString;
					RtlInitAnsiString(&ansiString, (PCSZ)previousProcessImageFileName);

					unicodeString.Buffer = processInfo->PreviousProcess.ProcessName;
					unicodeString.MaximumLength = sizeof(processInfo->PreviousProcess.ProcessName);
					unicodeString.Length = 0;

					RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
				}
				processInfo->PreviousProcess.EProcessAddress = previousProcess;
			}

			processInfo->ProcessId = processId;
			processInfo->ParentProcessId = HandleToULong(PsGetProcessInheritedFromUniqueProcessId(currentProcess));

			// Nome do processo
			PUCHAR imageFileName = PsGetProcessImageFileName(currentProcess);
			if (imageFileName) {
				ANSI_STRING ansiString;
				UNICODE_STRING unicodeString;
				RtlInitAnsiString(&ansiString, (PCSZ)imageFileName);

				unicodeString.Buffer = processInfo->ProcessName;
				unicodeString.MaximumLength = sizeof(processInfo->ProcessName);
				unicodeString.Length = 0;

				RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
			}

			// Acessar campos usando offsets
			__try {
				PLARGE_INTEGER createTime = (PLARGE_INTEGER)((PUCHAR)currentProcess + EPROCESS_CREATETIME_OFFSET);
				processInfo->CreateTime = *createTime;

				//PULONG handleCount = (PULONG)((PUCHAR)currentProcess + EPROCESS_HANDLECOUNT_OFFSET);
				//processInfo->HandleCount = *handleCount;

				// Contar threads
				PLIST_ENTRY threadListHead = (PLIST_ENTRY)((PUCHAR)currentProcess + EPROCESS_THREADLISTHEAD_OFFSET);
				PLIST_ENTRY currentEntry = threadListHead->Flink;
				ULONG threadCount = 0;

				while (currentEntry != threadListHead && threadCount < 1000) {
					threadCount++;
					currentEntry = currentEntry->Flink;
				}
				processInfo->ThreadCount = threadCount;

				// Informações de memória
				PMMSUPPORT vmSupport = (PMMSUPPORT)((PUCHAR)currentProcess + EPROCESS_VMS_OFFSET);
				processInfo->WorkingSetSize = vmSupport->Instance.WorkingSetSize * PAGE_SIZE;
				processInfo->PeakWorkingSetSize = vmSupport->Instance.PeakWorkingSetSize * PAGE_SIZE;
				processInfo->PageFaultCount = vmSupport->Instance.PageFaultCount;

			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				processInfo->CreateTime.QuadPart = 0;
				processInfo->HandleCount = 0;
				processInfo->ThreadCount = 0;
				processInfo->WorkingSetSize = 0;
				processInfo->PeakWorkingSetSize = 0;
				processInfo->PageFaultCount = 0;
			}

			processInfo->CurrentProcessAddress = currentProcess;
			processInfo->NextProcess.ProcessId = HandleToULong(PsGetProcessId(nextProcess));
			PUCHAR imageFileNameNextProcess = PsGetProcessImageFileName(nextProcess);
			if (imageFileNameNextProcess) {
				ANSI_STRING ansiString;
				UNICODE_STRING unicodeString;
				RtlInitAnsiString(&ansiString, (PCSZ)imageFileNameNextProcess);

				unicodeString.Buffer = processInfo->NextProcess.ProcessName;
				unicodeString.MaximumLength = sizeof(processInfo->NextProcess.ProcessName);
				unicodeString.Length = 0;

				RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
			}
			processInfo->NextProcess.EProcessAddress = nextProcess;
			return STATUS_SUCCESS;
		}

		previousProcess = currentProcess;
		currentProcess = nextProcess;
		iterations++;

		// Proteção contra loop infinito
		if (iterations > 10000) {
			break;
		}

	} while (currentProcess && currentProcess != initialProcess);

	return STATUS_NOT_FOUND;
}

NTSTATUS ListProcessesEPROCESS() {
	NTSTATUS ntstatus = STATUS_SUCCESS;

	UNICODE_STRING uniName = RTL_CONSTANT_STRING(L"\\SystemRoot\\KernelProcessListEPROCESS.txt");
	OBJECT_ATTRIBUTES objAttr;

	InitializeObjectAttributes(&objAttr, &uniName,
		OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
		NULL, NULL);

	HANDLE file;
	IO_STATUS_BLOCK ioStatusBlock;

	ntstatus = ZwCreateFile(&file,
		GENERIC_WRITE,
		&objAttr, &ioStatusBlock, NULL,
		FILE_ATTRIBUTE_NORMAL,
		0,
		FILE_OVERWRITE_IF,
		FILE_SYNCHRONOUS_IO_NONALERT,
		NULL, 0);

	if (NT_SUCCESS(ntstatus)) {
		PEPROCESS currentProcess = PsGetCurrentProcess();
		PEPROCESS initialProcess = currentProcess;
		ULONG iterations = 0;

		if (currentProcess) {
			do {
				HANDLE processId = PsGetProcessId(currentProcess);
				PUCHAR imageFileName = PsGetProcessImageFileName(currentProcess);

				if (imageFileName) {
					CHAR string[200];
					ntstatus = RtlStringCbPrintfA(string, sizeof(string),
						"PID: %lu, Name: %s, EPROCESS: 0x%p\n",
						HandleToULong(processId),
						imageFileName,
						currentProcess);

					if (NT_SUCCESS(ntstatus)) {
						size_t length;
						ntstatus = RtlStringCbLengthA(string, sizeof(string), &length);

						if (NT_SUCCESS(ntstatus)) {
							ntstatus = ZwWriteFile(file, NULL, NULL, NULL, &ioStatusBlock,
								string, (ULONG)length, NULL, NULL);
						}
					}
				}

				currentProcess = GetNextProcessManual(currentProcess);
				iterations++;

				// Proteção contra loop infinito
				if (iterations > 10000) {
					break;
				}

			} while (currentProcess && currentProcess != initialProcess);
		}

		ZwClose(file);
	}

	return STATUS_SUCCESS;
}

NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	ULONG controlCode = stack->Parameters.DeviceIoControl.IoControlCode;
	PVOID inputBuffer = irp->AssociatedIrp.SystemBuffer;
	PVOID outputBuffer = irp->AssociatedIrp.SystemBuffer;
	ULONG inputBufferLength = stack->Parameters.DeviceIoControl.InputBufferLength;
	ULONG outputBufferLength = stack->Parameters.DeviceIoControl.OutputBufferLength;
	ULONG bytesReturned = 0;

	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

	switch (controlCode) {
	case codes::IOCTL_LIST_PROCESSES:
		status = ListProcessesEPROCESS();
		break;

	case codes::IOCTL_GET_PROCESS_COUNT:
		if (outputBufferLength >= sizeof(PROCESS_COUNT_RESPONSE)) {
			PPROCESS_COUNT_RESPONSE response = (PPROCESS_COUNT_RESPONSE)outputBuffer;
			status = GetProcessCountEPROCESS(&response->ProcessCount);
			response->Status = status;
			bytesReturned = sizeof(PROCESS_COUNT_RESPONSE);
		}
		else {
			status = STATUS_BUFFER_TOO_SMALL;
		}
		break;

	case codes::IOCTL_GET_PROCESS_BY_INDEX:
		if (inputBufferLength >= sizeof(PROCESS_REQUEST) && outputBufferLength >= sizeof(PROCESS_INFO)) {
			PPROCESS_REQUEST request = (PPROCESS_REQUEST)inputBuffer;
			PPROCESS_INFO processInfo = (PPROCESS_INFO)outputBuffer;

			if (request->RequestType == 0) { // By index
				status = GetProcessByIndexEPROCESS(request->Index, processInfo);
				if (NT_SUCCESS(status)) {
					bytesReturned = sizeof(PROCESS_INFO);
				}
			}
			else {
				status = STATUS_INVALID_PARAMETER;
			}
		}
		else {
			status = STATUS_BUFFER_TOO_SMALL;
		}
		break;

	case codes::IOCTL_GET_PROCESS_BY_PID:
		if (inputBufferLength >= sizeof(PROCESS_REQUEST) && outputBufferLength >= sizeof(PROCESS_INFO)) {
			PPROCESS_REQUEST request = (PPROCESS_REQUEST)inputBuffer;
			PPROCESS_INFO processInfo = (PPROCESS_INFO)outputBuffer;

			if (request->RequestType == 1) { // By PID
				status = GetProcessByPidEPROCESS(request->ProcessId, processInfo);
				if (NT_SUCCESS(status)) {
					bytesReturned = sizeof(PROCESS_INFO);
				}
			}
			else {
				status = STATUS_INVALID_PARAMETER;
			}
		}
		else {
			status = STATUS_BUFFER_TOO_SMALL;
		}
		break;
	}

	irp->IoStatus.Status = status;
	irp->IoStatus.Information = bytesReturned;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

//Object passed between usermode and kernel
struct Request {
	ULONG RequestId; // Unique request ID
	ULONG DataLength; // Length of the data
	UCHAR Data[256]; // Buffer for data
};

void DriverUnload(PDRIVER_OBJECT driverObject) {
	UNICODE_STRING simbolicLink = {};
	RtlInitUnicodeString(&simbolicLink, L"\\??\\ExampleDriver");
	IoDeleteSymbolicLink(&simbolicLink);

	if (driverObject->DeviceObject) {
		IoDeleteDevice(driverObject->DeviceObject);
	}

	DebugPrint("[+] Driver unloaded successfully\n");
}

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath) {
	DebugPrint("[+] Hello World! from kernel");

	UNICODE_STRING driverName = {};

	RtlInitUnicodeString(&driverName, L"\\Driver\\ExampleDriver");

	UNREFERENCED_PARAMETER(registryPath);

	UNICODE_STRING deviceName = {};
	RtlInitUnicodeString(&deviceName, L"\\Device\\ExampleDriver");
	PDEVICE_OBJECT deviceObject = nullptr;
	NTSTATUS status = IoCreateDevice(driverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObject);

	if (status != STATUS_SUCCESS) {
		DebugPrint("[-] Failed to create driver device.\n");
		return status;
	}
	DebugPrint("[+] Success to create driver device.\n");

	UNICODE_STRING simbolicLink = {};
	RtlInitUnicodeString(&simbolicLink, L"\\??\\ExampleDriver");
	status = IoCreateSymbolicLink(&simbolicLink, &deviceName);

	if (status != STATUS_SUCCESS)
	{
		DebugPrint("[-] Failed to create simbolic link.\n");
		IoDeleteDevice(deviceObject);
		return status;
	}

	DebugPrint("[+] Success to create driver simbolic link.\n");

	driverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreateClose;
	driverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchCreateClose;
	driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
	driverObject->DriverUnload = DriverUnload;

	return status;
}