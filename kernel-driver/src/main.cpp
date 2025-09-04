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

typedef struct _SYSTEM_THREADS {
	LARGE_INTEGER  KernelTime;
	LARGE_INTEGER  UserTime;
	LARGE_INTEGER  CreateTime;
	ULONG          WaitTime;
	PVOID          StartAddress;
	CLIENT_ID      ClientId;
	KPRIORITY      Priority;
	KPRIORITY      BasePriority;
	ULONG          ContextSwitchCount;
	LONG           State;
	LONG           WaitReason;
} SYSTEM_THREADS, * PSYSTEM_THREADS;

typedef struct _SYSTEM_PROCESSES {
	ULONG            NextEntryDelta;
	ULONG            ThreadCount;
	ULONG            Reserved1[6];
	LARGE_INTEGER    CreateTime;
	LARGE_INTEGER    UserTime;
	LARGE_INTEGER    KernelTime;
	UNICODE_STRING   ProcessName;
	KPRIORITY        BasePriority;
	SIZE_T           ProcessId;
	SIZE_T           InheritedFromProcessId;
	ULONG            HandleCount;
	ULONG            Reserved2[2];
	VM_COUNTERS      VmCounters;
	IO_COUNTERS      IoCounters;
	SYSTEM_THREADS   Threads[1];
} SYSTEM_PROCESSES, * PSYSTEM_PROCESSES;

// Enhanced process information structure for usermode communication
typedef struct _PROCESS_INFO {
	ULONG ProcessId;
	ULONG ParentProcessId;
	WCHAR ProcessName[64];
	ULONG ThreadCount;
	ULONG HandleCount;
	KPRIORITY BasePriority;
	LARGE_INTEGER CreateTime;
	LARGE_INTEGER UserTime;
	LARGE_INTEGER KernelTime;

	// Memory information (using only available VM_COUNTERS members)
	SIZE_T WorkingSetSize;
	SIZE_T PeakWorkingSetSize;
	SIZE_T VirtualSize;
	SIZE_T PeakVirtualSize;
	SIZE_T PagefileUsage;
	SIZE_T PeakPagefileUsage;
	SIZE_T PageFaultCount;  // Changed from PrivatePageCount

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

#define SystemProcessInformation 5
#define POOL_TAG 'enoN'

extern "C"
NTSTATUS NTAPI ZwQuerySystemInformation(ULONG SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength);

// Global variables to cache process information
static PVOID g_ProcessBuffer = NULL;
static ULONG g_ProcessBufferSize = 0;
static ULONG g_ProcessCount = 0;
static LARGE_INTEGER g_LastUpdateTime = { 0 };

NTSTATUS UpdateProcessCache() {
	NTSTATUS status;
	ULONG bufferSize = 0;
	PVOID newBuffer = NULL;

	// Get required buffer size
	status = ZwQuerySystemInformation(SystemProcessInformation, NULL, 0, &bufferSize);
	if (status != STATUS_INFO_LENGTH_MISMATCH) {
		return status;
	}

	// Allocate buffer with some extra space
	bufferSize += 0x1000;
	newBuffer = ExAllocatePoolWithTag(PagedPool, bufferSize, POOL_TAG);
	if (!newBuffer) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// Query process information
	status = ZwQuerySystemInformation(SystemProcessInformation, newBuffer, bufferSize, &bufferSize);
	if (!NT_SUCCESS(status)) {
		ExFreePoolWithTag(newBuffer, POOL_TAG);
		return status;
	}

	// Free old buffer and update cache
	if (g_ProcessBuffer) {
		ExFreePoolWithTag(g_ProcessBuffer, POOL_TAG);
	}

	g_ProcessBuffer = newBuffer;
	g_ProcessBufferSize = bufferSize;

	// Count processes
	PSYSTEM_PROCESSES processEntry = (PSYSTEM_PROCESSES)g_ProcessBuffer;
	g_ProcessCount = 0;

	do {
		g_ProcessCount++;
		if (processEntry->NextEntryDelta == 0) break;
		processEntry = (PSYSTEM_PROCESSES)((BYTE*)processEntry + processEntry->NextEntryDelta);
	} while (TRUE);

	KeQuerySystemTime(&g_LastUpdateTime);
	return STATUS_SUCCESS;
}

NTSTATUS GetProcessCount(PULONG processCount) {
	LARGE_INTEGER currentTime;
	KeQuerySystemTime(&currentTime);

	// Update cache if it's older than 5 seconds or empty
	if (!g_ProcessBuffer || (currentTime.QuadPart - g_LastUpdateTime.QuadPart) > 50000000LL) {
		NTSTATUS status = UpdateProcessCache();
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}

	*processCount = g_ProcessCount;
	return STATUS_SUCCESS;
}

NTSTATUS GetProcessByIndex(ULONG index, PPROCESS_INFO processInfo) {
	if (!g_ProcessBuffer) {
		NTSTATUS status = UpdateProcessCache();
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}

	if (index >= g_ProcessCount) {
		return STATUS_INVALID_PARAMETER;
	}

	PSYSTEM_PROCESSES processEntry = (PSYSTEM_PROCESSES)g_ProcessBuffer;
	PSYSTEM_PROCESSES previousEntry = NULL;
	ULONG currentIndex = 0;

	while (currentIndex < index && processEntry->NextEntryDelta) {
		previousEntry = processEntry;
		processEntry = (PSYSTEM_PROCESSES)((BYTE*)processEntry + processEntry->NextEntryDelta);
		currentIndex++;
	}

	if (currentIndex != index) {
		return STATUS_NOT_FOUND;
	}

	RtlZeroMemory(processInfo, sizeof(PROCESS_INFO));

	processInfo->ProcessId = (ULONG)processEntry->ProcessId;
	processInfo->ParentProcessId = (ULONG)processEntry->InheritedFromProcessId;
	processInfo->ThreadCount = processEntry->ThreadCount;
	processInfo->HandleCount = processEntry->HandleCount;
	processInfo->BasePriority = processEntry->BasePriority;
	processInfo->CreateTime = processEntry->CreateTime;
	processInfo->UserTime = processEntry->UserTime;
	processInfo->KernelTime = processEntry->KernelTime;

	if (processEntry->ProcessName.Length > 0 && processEntry->ProcessName.Buffer) {
		ULONG nameLength = min(processEntry->ProcessName.Length / sizeof(WCHAR), 63);
		RtlCopyMemory(processInfo->ProcessName, processEntry->ProcessName.Buffer, nameLength * sizeof(WCHAR));
		processInfo->ProcessName[nameLength] = L'\0';
	}
	else {
		wcscpy_s(processInfo->ProcessName, 64, L"System Idle Process");
	}

	processInfo->WorkingSetSize = processEntry->VmCounters.WorkingSetSize;
	processInfo->PeakWorkingSetSize = processEntry->VmCounters.PeakWorkingSetSize;
	processInfo->VirtualSize = processEntry->VmCounters.VirtualSize;
	processInfo->PeakVirtualSize = processEntry->VmCounters.PeakVirtualSize;
	processInfo->PagefileUsage = processEntry->VmCounters.PagefileUsage;
	processInfo->PeakPagefileUsage = processEntry->VmCounters.PeakPagefileUsage;
	processInfo->PageFaultCount = processEntry->VmCounters.PageFaultCount;

	processInfo->ReadOperationCount = processEntry->IoCounters.ReadOperationCount;
	processInfo->WriteOperationCount = processEntry->IoCounters.WriteOperationCount;
	processInfo->OtherOperationCount = processEntry->IoCounters.OtherOperationCount;
	processInfo->ReadTransferCount = processEntry->IoCounters.ReadTransferCount;
	processInfo->WriteTransferCount = processEntry->IoCounters.WriteTransferCount;
	processInfo->OtherTransferCount = processEntry->IoCounters.OtherTransferCount;

	processInfo->CurrentProcessAddress = processEntry;
	processInfo->PreviousProcessAddress = previousEntry;

	if (processEntry->NextEntryDelta != 0) {
		processInfo->NextProcessAddress = (PVOID)((BYTE*)processEntry + processEntry->NextEntryDelta);
	}
	else {
		processInfo->NextProcessAddress = NULL;
	}

	return STATUS_SUCCESS;
}

NTSTATUS GetProcessByPid(ULONG processId, PPROCESS_INFO processInfo) {
	if (!g_ProcessBuffer) {
		NTSTATUS status = UpdateProcessCache();
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}

	PSYSTEM_PROCESSES processEntry = (PSYSTEM_PROCESSES)g_ProcessBuffer;

	do {
		if (processEntry->ProcessId == processId) {
			// Found the process, fill information
			RtlZeroMemory(processInfo, sizeof(PROCESS_INFO));

			processInfo->ProcessId = (ULONG)processEntry->ProcessId;
			processInfo->ParentProcessId = (ULONG)processEntry->InheritedFromProcessId;
			processInfo->ThreadCount = processEntry->ThreadCount;
			processInfo->HandleCount = processEntry->HandleCount;
			processInfo->BasePriority = processEntry->BasePriority;
			processInfo->CreateTime = processEntry->CreateTime;
			processInfo->UserTime = processEntry->UserTime;
			processInfo->KernelTime = processEntry->KernelTime;

			// Copy process name
			if (processEntry->ProcessName.Length > 0 && processEntry->ProcessName.Buffer) {
				ULONG nameLength = min(processEntry->ProcessName.Length / sizeof(WCHAR), 63);
				RtlCopyMemory(processInfo->ProcessName, processEntry->ProcessName.Buffer, nameLength * sizeof(WCHAR));
				processInfo->ProcessName[nameLength] = L'\0';
			}
			else {
				wcscpy_s(processInfo->ProcessName, 64, L"System Idle Process");
			}

			// Memory information
			processInfo->WorkingSetSize = processEntry->VmCounters.WorkingSetSize;
			processInfo->PeakWorkingSetSize = processEntry->VmCounters.PeakWorkingSetSize;
			processInfo->VirtualSize = processEntry->VmCounters.VirtualSize;
			processInfo->PeakVirtualSize = processEntry->VmCounters.PeakVirtualSize;
			processInfo->PagefileUsage = processEntry->VmCounters.PagefileUsage;
			processInfo->PeakPagefileUsage = processEntry->VmCounters.PeakPagefileUsage;
			processInfo->PageFaultCount = processEntry->VmCounters.PageFaultCount;

			// I/O information
			processInfo->ReadOperationCount = processEntry->IoCounters.ReadOperationCount;
			processInfo->WriteOperationCount = processEntry->IoCounters.WriteOperationCount;
			processInfo->OtherOperationCount = processEntry->IoCounters.OtherOperationCount;
			processInfo->ReadTransferCount = processEntry->IoCounters.ReadTransferCount;
			processInfo->WriteTransferCount = processEntry->IoCounters.WriteTransferCount;
			processInfo->OtherTransferCount = processEntry->IoCounters.OtherTransferCount;

			return STATUS_SUCCESS;
		}

		if (processEntry->NextEntryDelta == 0) break;
		processEntry = (PSYSTEM_PROCESSES)((BYTE*)processEntry + processEntry->NextEntryDelta);
	} while (TRUE);

	return STATUS_NOT_FOUND;
}

NTSTATUS ListProcesses() {
	NTSTATUS ntstatus = STATUS_SUCCESS;

	UNICODE_STRING uniName = RTL_CONSTANT_STRING(L"\\SystemRoot\\KernelProcessList.txt");
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
		ULONG bufferSize = 0;

		if (ZwQuerySystemInformation(SystemProcessInformation, NULL, 0, &bufferSize) == STATUS_INFO_LENGTH_MISMATCH) {
			if (bufferSize) {
				PVOID memory = ExAllocatePoolWithTag(PagedPool, bufferSize, POOL_TAG);

				if (memory) {
					ntstatus = ZwQuerySystemInformation(SystemProcessInformation, memory, bufferSize, &bufferSize);
					if (NT_SUCCESS(ntstatus)) {
						PSYSTEM_PROCESSES processEntry = (PSYSTEM_PROCESSES)memory;

						do {
							if (processEntry->ProcessName.Length) {
								CHAR string[100];
								ntstatus = RtlStringCbPrintfA(string, _countof(string), "%ws : %llu\n", processEntry->ProcessName.Buffer, processEntry->ProcessId);

								if (NT_SUCCESS(ntstatus)) {
									size_t length;
									ntstatus = RtlStringCbLengthA(string, _countof(string), &length);

									if (NT_SUCCESS(ntstatus))
										ntstatus = ZwWriteFile(file, NULL, NULL, NULL, &ioStatusBlock, string, (ULONG)length, NULL, NULL);
								}
							}
							if (processEntry->NextEntryDelta == 0) break;
							processEntry = (PSYSTEM_PROCESSES)((BYTE*)processEntry + processEntry->NextEntryDelta);
						} while (TRUE);
					}
					ExFreePoolWithTag(memory, POOL_TAG);
				}
			}
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
		status = ListProcesses();
		break;

	case codes::IOCTL_GET_PROCESS_COUNT:
		if (outputBufferLength >= sizeof(PROCESS_COUNT_RESPONSE)) {
			PPROCESS_COUNT_RESPONSE response = (PPROCESS_COUNT_RESPONSE)outputBuffer;
			status = GetProcessCount(&response->ProcessCount);
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
				status = GetProcessByIndex(request->Index, processInfo);
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
				status = GetProcessByPid(request->ProcessId, processInfo);
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

	// Clean up process cache
	if (g_ProcessBuffer) {
		ExFreePoolWithTag(g_ProcessBuffer, POOL_TAG);
		g_ProcessBuffer = NULL;
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