#include <ntifs.h>
#include <ntstrsafe.h>

extern "C" {
	NTKERNELAPI NTSTATUS IoCreateDriver(PUNICODE_STRING DriverName, PDRIVER_INITIALIZE InitializationFunction);	
}
_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
extern "C" DRIVER_DISPATCH DispatchDeviceControl;

void DebugPrint(PCSTR text) {
#ifndef DEBUG
	UNREFERENCED_PARAMETER(text);
#endif // DEBUG

	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, text));
}
//Basic Process struct
struct ProcessInfo {
	ULONG pid;
	ULONG parentPid;
	ULONG sessionId;
	CHAR imageName[16]; // PsGetProcessImageFileName returns up to 15 chars + null
};

// Function to list all running processes
extern "C" {

	// Returns the parent PID of a process
	NTKERNELAPI HANDLE PsGetProcessInheritedFromUniqueProcessId(
		_In_ PEPROCESS Process
	);

	// Returns the process ID
	NTKERNELAPI HANDLE PsGetProcessId(
		_In_ PEPROCESS Process
	);

	// Returns the session ID
	NTKERNELAPI ULONG PsGetProcessSessionId(
		_In_ PEPROCESS Process
	);

	// Returns the image file name (15 chars + null terminator)
	NTKERNELAPI const CHAR* PsGetProcessImageFileName(
		_In_ PEPROCESS Process
	);

}

typedef PEPROCESS(*t_PsGetNextProcess)(PEPROCESS Process);

t_PsGetNextProcess PsGetNextProcessPtr = nullptr;

void ResolvePsFunctions() {
	UNICODE_STRING routineName;
	RtlInitUnicodeString(&routineName, L"PsGetNextProcess"); // nome exato do export
	PsGetNextProcessPtr = (t_PsGetNextProcess)MmGetSystemRoutineAddress(&routineName);

	if (!PsGetNextProcessPtr) {
		KdPrint(("[-] Failed to resolve PsGetNextProcess\n"));
	}
	else {
		KdPrint(("[+] PsGetNextProcess resolved\n"));
	}
}

namespace codes {
	constexpr ULONG IOCTL_LIST_PROCESSES = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA);
}

NTSTATUS ListProcesses(PVOID outputBuffer, ULONG outputBufferLength, ULONG* bytesReturned) {
	ULONG processCount = 0;
	PEPROCESS process = PsGetNextProcessPtr(nullptr);

	while (process) {
		if (processCount >= outputBufferLength / sizeof(ProcessInfo)) {
			break; // Buffer is full
		}
		ProcessInfo* info = reinterpret_cast<ProcessInfo*>(outputBuffer) + processCount;
		info->pid = (ULONG)(ULONG_PTR)PsGetProcessId(process);
		info->parentPid = (ULONG)(ULONG_PTR)PsGetProcessInheritedFromUniqueProcessId(process);
		info->sessionId = PsGetProcessSessionId(process);
		RtlZeroMemory(info->imageName, sizeof(info->imageName));
		RtlStringCchCopyA(info->imageName, sizeof(info->imageName), PsGetProcessImageFileName(process));

		processCount++;
		process = PsGetNextProcessPtr(process);
	}
	if (bytesReturned) {
		*bytesReturned = processCount * sizeof(ProcessInfo);
	}
	return STATUS_SUCCESS;
}

NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	ULONG controlCode = stack->Parameters.DeviceIoControl.IoControlCode;
	PVOID outputBuffer = Irp->AssociatedIrp.SystemBuffer;
	ULONG outputBufferLength = stack->Parameters.DeviceIoControl.OutputBufferLength;
	ULONG bytesReturned = 0;

	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

	switch (controlCode) {
	case codes::IOCTL_LIST_PROCESSES:
		status = ListProcesses(outputBuffer, outputBufferLength, &bytesReturned);
		break;
	}

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = bytesReturned;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS DriverMain(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath) {
	UNREFERENCED_PARAMETER(registryPath);

	UNICODE_STRING deviceName = {};
	RtlInitUnicodeString(&deviceName, L"\\Device\\ExampleDriver");

	PDEVICE_OBJECT deviceObject = nullptr;
	NTSTATUS status = IoCreateDevice(driverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &deviceObject);

	if (status != STATUS_SUCCESS) {
		DebugPrint("[-] Failed to create driver device.\n");
		return status;
	}
	DebugPrint("[+] Success to create driver device.\n");

	UNICODE_STRING simbolicLink = {};
	RtlInitUnicodeString(&simbolicLink, L"\\DosDevices\\ExampleDriver");

	status = IoCreateSymbolicLink(&simbolicLink, &deviceName);
	if (status != STATUS_SUCCESS)
	{
		DebugPrint("[-] Failed to create simbolic link.\n");
		return status; 
	}

	DebugPrint("[+] Success to create driver simbolic link.\n");

	//Allows to send small data between usermode and kernel driver
	SetFlag(deviceObject->Flags, DO_BUFFERED_IO);

	//list all running processes and their info
	driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;

	ClearFlag(deviceObject->Flags, DO_DEVICE_INITIALIZING);
	
	return status;
}
//Object passed between usermode and kernel
struct Request {
	ULONG RequestId; // Unique request ID
	ULONG DataLength; // Length of the data
	UCHAR Data[256]; // Buffer for data	
};
// This is the entry point for the driver

NTSTATUS DriverEntry() {
	DebugPrint("[+] Hello World! from kernel");

	UNICODE_STRING driverName = {};

	RtlInitUnicodeString(&driverName, L"\\Driver\\ExampleDriver");


	return IoCreateDriver(&driverName, &DriverMain);
}