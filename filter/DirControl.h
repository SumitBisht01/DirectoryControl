/*++
Module Name:
    dircontrol.h
Abstract:
    Header file which contains the structures, type definitions,
    constants, global variables and function prototypes that are
    only visible within the kernel.
Environment:
    Kernel mode
--*/
#ifndef __DIRCONTROL_H__
#define __DIRCONTROL_H__
///////////////////////////////////////////////////////////////////////////
//
//  Global variables
//
///////////////////////////////////////////////////////////////////////////

typedef struct _DIRCTL_DATA {

    //  The object that identifies this driver.
    PDRIVER_OBJECT DriverObject;

    //  The filter handle that results from a call to
    //  FltRegisterFilter.
    PFLT_FILTER Filter;

    //  Listens for incoming connections
    PFLT_PORT ServerPort;

    //  User process that connected to the port
    PEPROCESS UserProcess;

    //  Client port for a connection to user-mode
    PFLT_PORT ClientPort;

} DIRCTL_DATA, *PDIRCTL_DATA;

extern DIRCTL_DATA DirCtlData;

#pragma warning(push)
#pragma warning(disable:4200) // disable warnings for structures with zero length arrays.

#pragma warning(pop)

DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    );

NTSTATUS
DirCtlUnload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
    );

NTSTATUS
DirCtlQueryTeardown (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    );

FLT_PREOP_CALLBACK_STATUS
DirCtlPreCreate (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

FLT_POSTOP_CALLBACK_STATUS
DirCtlPostCreate (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
    );

//FLT_PREOP_CALLBACK_STATUS
//DirCtlPreCleanup (
//    _Inout_ PFLT_CALLBACK_DATA Data,
//    _In_ PCFLT_RELATED_OBJECTS FltObjects,
//    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
//    );

NTSTATUS
DirCtlInstanceSetup (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
    );

#endif /* __SCANNER_H__ */

