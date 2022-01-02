/*++
Copyright (c) 
Module Name:
    DirControl.c
Abstract:
    This is the main module of the Dir control.
    This filter provides the directory control by allowing only the read operations.
Environment:
    Kernel mode
--*/

#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>
#include "dcuk.h"
#include "DirControl.h"
#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

#define DIRCTL_REG_TAG       'Rncs'
#define DIRCTL_STRING_TAG    'Sncs'
//  Structure that contains all the global data structures
//  used throughout the DirControl.

DIRCTL_DATA DirCtlData;
UNICODE_STRING g_DirPathToProtect;
BOOLEAN g_EnableProtection;
FAST_MUTEX g_DirPathLock;
FAST_MUTEX g_MsgLock;

typedef NTSTATUS(*QUERY_INFO_PROCESS) (
    __in HANDLE ProcessHandle,
    __in PROCESSINFOCLASS ProcessInformationClass,
    __out_bcount(ProcessInformationLength) PVOID ProcessInformation,
    __in ULONG ProcessInformationLength,
    __out_opt PULONG ReturnLength
    );

QUERY_INFO_PROCESS ZwQueryInformationProcess;

//  Function prototypes
NTSTATUS
DirCtlPortConnect (
    _In_ PFLT_PORT ClientPort,
    _In_opt_ PVOID ServerPortCookie,
    _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID *ConnectionCookie
    );

VOID
DirCtlPortDisconnect (
    _In_opt_ PVOID ConnectionCookie
    );

NTSTATUS
DirCtlSendFileInfo(
    _Inout_ PFLT_CALLBACK_DATA Data,
    PUNICODE_STRING FileName
    );

BOOLEAN
DirCtlCheckPath (
    _In_ PUNICODE_STRING FileName
    );

NTSTATUS
DirCtlRecvMessage(
    IN PVOID PortCookie,
    IN PVOID InputBuffer OPTIONAL,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer OPTIONAL,
    IN ULONG OutputBufferLength,
    OUT PULONG ReturnOutputBufferLength
);

//  Assign text sections for each routine.
#ifdef ALLOC_PRAGMA
    #pragma alloc_text(INIT, DriverEntry)
    #pragma alloc_text(PAGE, DirCtlInstanceSetup)
    #pragma alloc_text(PAGE, DirCtlPreCreate)
    #pragma alloc_text(PAGE, DirCtlPostCreate)
    #pragma alloc_text(PAGE, DirCtlPortConnect)
    #pragma alloc_text(PAGE, DirCtlPortDisconnect)
#endif


const FLT_OPERATION_REGISTRATION Callbacks[] = {

    { IRP_MJ_CREATE,
      0,
      DirCtlPreCreate,
      DirCtlPostCreate},

    { IRP_MJ_CLEANUP,
      0,
      0,
      NULL},

    { IRP_MJ_WRITE,
      0,
      0,
      NULL},

#if (WINVER>=0x0602)

    { IRP_MJ_FILE_SYSTEM_CONTROL,
      0,
      0,
      NULL
    },

#endif

    { IRP_MJ_OPERATION_END}
};

const FLT_REGISTRATION FilterRegistration = {

    sizeof( FLT_REGISTRATION ),         //  Size
    FLT_REGISTRATION_VERSION,           //  Version
    0,                                  //  Flags
    NULL,                               //  Context Registration.
    Callbacks,                          //  Operation callbacks
    DirCtlUnload,                       //  FilterUnload
    DirCtlInstanceSetup,                //  InstanceSetup
    DirCtlQueryTeardown,                //  InstanceQueryTeardown
    NULL,                               //  InstanceTeardownStart
    NULL,                               //  InstanceTeardownComplete
    NULL,                               //  GenerateFileName
    NULL,                               //  GenerateDestinationFileName
    NULL                                //  NormalizeNameComponent
};

////////////////////////////////////////////////////////////////////////////
//
//    Filter initialization and unload routines.
//
////////////////////////////////////////////////////////////////////////////

NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
/*++
Routine Description:
    This is the initialization routine for the Filter driver.  This
    registers the Filter with the filter manager and initializes all
    its global data structures.
Arguments:
    DriverObject - Pointer to driver object created by the system to
        represent this driver.
    RegistryPath - Unicode string identifying where the parameters for this
        driver are located in the registry.
Return Value:
    Returns STATUS_SUCCESS.
--*/
{
    UNREFERENCED_PARAMETER(RegistryPath);
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING uniString;
    PSECURITY_DESCRIPTOR sd;
    NTSTATUS status;

    ExInitializeDriverRuntime( DrvRtPoolNxOptIn );

    status = FltRegisterFilter( DriverObject, &FilterRegistration, &DirCtlData.Filter );
    if (!NT_SUCCESS( status )) {
        return status;
    }

    RtlInitUnicodeString( &uniString, DCAPPPortName);
    status = FltBuildDefaultSecurityDescriptor( &sd, FLT_PORT_ALL_ACCESS );
    if (NT_SUCCESS( status )) {

        InitializeObjectAttributes( &oa, &uniString,
                                    OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                    NULL, sd );

        status = FltCreateCommunicationPort( DirCtlData.Filter, &DirCtlData.ServerPort,
                                                &oa, NULL, DirCtlPortConnect, DirCtlPortDisconnect,
                                                DirCtlRecvMessage, 1 );
        //  Free the security descriptor in all cases. It is not needed once
        //  the call to FltCreateCommunicationPort() is made.
        FltFreeSecurityDescriptor( sd );
        if (NT_SUCCESS( status )) {

            status = FltStartFiltering( DirCtlData.Filter );
            if (NT_SUCCESS( status )) {
                ExInitializeFastMutex(&g_DirPathLock);
                ExInitializeFastMutex(&g_MsgLock);
                g_EnableProtection = FALSE;
                return STATUS_SUCCESS;
            }

            FltCloseCommunicationPort( DirCtlData.ServerPort );
        }
    }

    FltUnregisterFilter( DirCtlData.Filter );
    return status;
}


NTSTATUS
DirCtlPortConnect (
    _In_ PFLT_PORT ClientPort,
    _In_opt_ PVOID ServerPortCookie,
    _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID *ConnectionCookie
    )
/*++
Routine Description
    This is called when user-mode connects to the server port - to establish a
    connection
Arguments
    ClientPort - This is the client connection port that will be used to
        send messages from the filter
    ServerPortCookie - The context associated with this port when the
        minifilter created this port.
    ConnectionContext - Context from entity connecting to this port (most likely
        your user mode service)
    SizeofContext - Size of ConnectionContext in bytes
    ConnectionCookie - Context to be passed to the port disconnect routine.
Return Value
    STATUS_SUCCESS - to accept the connection
--*/
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER( ServerPortCookie );
    UNREFERENCED_PARAMETER( ConnectionContext );
    UNREFERENCED_PARAMETER( SizeOfContext);
    UNREFERENCED_PARAMETER( ConnectionCookie = NULL );

    FLT_ASSERT( DirCtlData.ClientPort == NULL );
    FLT_ASSERT( DirCtlData.UserProcess == NULL );

    //  Set the user process and port. In a production filter it may
    //  be necessary to synchronize access to such fields with port
    //  lifetime. For instance, while filter manager will synchronize
    //  FltCloseClientPort with FltSendMessage's reading of the port 
    //  handle, synchronizing access to the UserProcess would be up to
    //  the filter.

    DirCtlData.UserProcess = PsGetCurrentProcess();
    DirCtlData.ClientPort = ClientPort;
    return STATUS_SUCCESS;
}


VOID
DirCtlPortDisconnect(
     _In_opt_ PVOID ConnectionCookie
     )
/*++
Routine Description
    This is called when the connection is torn-down. We use it to close our
    handle to the connection
Arguments
    ConnectionCookie - Context from the port connect routine
Return value
    None
--*/
{
    UNREFERENCED_PARAMETER( ConnectionCookie );
    PAGED_CODE();
    FltCloseClientPort( DirCtlData.Filter, &DirCtlData.ClientPort );
    DirCtlData.UserProcess = NULL;
}


NTSTATUS
DirCtlUnload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
    )
/*++
Routine Description:
    This is the unload routine for the Filter driver.  This unregisters the
    Filter with the filter manager and frees any allocated global data
    structures.
Arguments:
    None.
Return Value:
    Returns the final status of the deallocation routines.
--*/
{
    UNREFERENCED_PARAMETER( Flags );
    g_EnableProtection = FALSE;
    FltCloseCommunicationPort( DirCtlData.ServerPort );
    FltUnregisterFilter( DirCtlData.Filter );

    return STATUS_SUCCESS;
}


NTSTATUS
DirCtlInstanceSetup (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
    )
/*++

Routine Description:
    This routine is called by the filter manager when a new instance is created.
    We specified in the registry that we only want for manual attachments,
    so that is all we should receive here.
Arguments:
    FltObjects - Describes the instance and volume which we are being asked to
        setup.
    Flags - Flags describing the type of attachment this is.
    VolumeDeviceType - The DEVICE_TYPE for the volume to which this instance
        will attach.
    VolumeFileSystemType - The file system formatted on this volume.
Return Value:
  STATUS_SUCCESS            - we wish to attach to the volume
  STATUS_FLT_DO_NOT_ATTACH  - no, thank you
--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );
    UNREFERENCED_PARAMETER( VolumeFilesystemType );

    PAGED_CODE();

    FLT_ASSERT( FltObjects->Filter == DirCtlData.Filter );

    //  Don't attach to network volumes.
    if (VolumeDeviceType == FILE_DEVICE_NETWORK_FILE_SYSTEM) {

       return STATUS_FLT_DO_NOT_ATTACH;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
DirCtlQueryTeardown (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    )
/*++
Routine Description:
    This is the instance detach routine for the filter. This
    routine is called by filter manager when a user initiates a manual instance
    detach. This is a 'query' routine: if the filter does not want to support
    manual detach, it can return a failure status
Arguments:
    FltObjects - Describes the instance and volume for which we are receiving
        this query teardown request.
    Flags - Unused
Return Value:
    STATUS_SUCCESS - we allow instance detach to happen
--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    return STATUS_SUCCESS;
}


BOOLEAN
DirCtlCheckPath (
        _In_ PUNICODE_STRING FileName
    )
/*++
Routine Description:
    Checks if this file name is something we are interested in
Arguments
    Extension - Pointer to the file name extension
Return Value
    TRUE - Yes we are interested
    FALSE - No
--*/
{
    if (FileName->Length == 0) {
        return FALSE;
    }
    
    BOOLEAN bFound = FALSE;
    ExAcquireFastMutex(&g_DirPathLock);
    bFound = RtlPrefixUnicodeString(&g_DirPathToProtect, FileName, FALSE);
    ExReleaseFastMutex(&g_DirPathLock);
    return bFound;
}

FLT_PREOP_CALLBACK_STATUS
DirCtlPreCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
)
/* --
Routine Description :
Pre create callback. If the file is opened with FILE_SUPERSEDE, FILE_OVERWRITE, 
FILE_OVERWRITE_IF option then denying the access.
Arguments :
    Data - The structure which describes the operation parameters.
    FltObject - The structure which describes the objects affected by this
    operation.
    CompletionContext - Output parameter which can be used to pass a context
    from this pre - create callback to the post - create callback.
Return Value :
    FLT_PREOP_COMPLETE - if file is opened with FILE_SUPERSEDE, FILE_OVERWRITE,
    FILE_OVERWRITE_IF option. otherwise
    FLT_PREOP_SUCCESS_WITH_CALLBACK.
 */
{
    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(FltObjects);

    NTSTATUS status;
    PFLT_FILE_NAME_INFORMATION nameInfo;
    BOOLEAN safeToOpen = TRUE;
    BOOLEAN checkFile;
    FLT_PREOP_CALLBACK_STATUS returnValue = FLT_PREOP_SUCCESS_WITH_CALLBACK;

    if (g_EnableProtection == FALSE) {
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;
    }

    status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED |
                                        FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo);
    if (!NT_SUCCESS(status)) {
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;
    }

    FltParseFileNameInformation(nameInfo);

    checkFile = DirCtlCheckPath(&nameInfo->Name);

    if (!checkFile) {
        //  Release file name info, we're done with it
        FltReleaseFileNameInformation(nameInfo);
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;
    }

    UCHAR createDisposition = (UCHAR)(Data->Iopb->Parameters.Create.Options >> 24);

    if (createDisposition == FILE_SUPERSEDE || createDisposition == FILE_OVERWRITE
        || createDisposition == FILE_OVERWRITE_IF) {

        safeToOpen = FALSE;
        DirCtlSendFileInfo(Data, &nameInfo->Name);
    }

    //  Release file name info, we're done with it
    FltReleaseFileNameInformation(nameInfo);

    if (!safeToOpen) {
        //  Ask the filter manager to undo the create.
        DbgPrint("!!! dir ctl -- undoing create \n");

        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;

        returnValue = FLT_PREOP_COMPLETE;
    }

    return returnValue;
}

FLT_POSTOP_CALLBACK_STATUS
DirCtlPostCreate (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
    )
/*++
Routine Description:
    Post create callback.  
Arguments:
    Data - The structure which describes the operation parameters.
    FltObject - The structure which describes the objects affected by this
        operation.
    CompletionContext - The operation context passed fron the pre-create
        callback.
    Flags - Flags to say why we are getting this post-operation callback.
Return Value:
    FLT_POSTOP_FINISHED_PROCESSING - ok to open the file or we wish to deny
                                     access to this file, hence undo the open
--*/
{
    FLT_POSTOP_CALLBACK_STATUS returnStatus = FLT_POSTOP_FINISHED_PROCESSING;
    PFLT_FILE_NAME_INFORMATION nameInfo;
    NTSTATUS status;
    BOOLEAN safeToOpen = TRUE;
    BOOLEAN checkFile;

    UNREFERENCED_PARAMETER( CompletionContext );
    UNREFERENCED_PARAMETER( Flags );

    if (!NT_SUCCESS( Data->IoStatus.Status ) ||
        (STATUS_REPARSE == Data->IoStatus.Status)) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    if (g_EnableProtection == FALSE) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    status = FltGetFileNameInformation( Data, FLT_FILE_NAME_NORMALIZED |
                                        FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo );
    if (!NT_SUCCESS( status )) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    FltParseFileNameInformation( nameInfo );

    checkFile = DirCtlCheckPath( &nameInfo->Name );

    if (!checkFile) {
        //  Release file name info, we're done with it
        FltReleaseFileNameInformation(nameInfo);
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    if (FlagOn(Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess,
        FILE_WRITE_DATA | FILE_APPEND_DATA |
        DELETE | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA |
        WRITE_DAC | WRITE_OWNER | ACCESS_SYSTEM_SECURITY) 
        || FlagOn(Data->Iopb->Parameters.Create.Options, FILE_DELETE_ON_CLOSE)) {

        safeToOpen = FALSE;
        DirCtlSendFileInfo( Data, &nameInfo->Name);
    }
   
    //  Release file name info, we're done with it
    FltReleaseFileNameInformation(nameInfo);

    if (!safeToOpen) {
        //  Ask the filter manager to undo the create.
        DbgPrint( "!!! dir ctl -- undoing create \n" );

        FltCancelFileOpen( FltObjects->Instance, FltObjects->FileObject );

        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;

        returnStatus = FLT_POSTOP_FINISHED_PROCESSING;
    }

    return returnStatus;
}

NTSTATUS GetProcessImageName(
    PUNICODE_STRING ProcessImageName
)
{
    NTSTATUS status;
    ULONG returnedLength;
    ULONG bufferLength;
    PVOID buffer;
    PUNICODE_STRING imageName;

    PAGED_CODE();

    if (NULL == ZwQueryInformationProcess) {
        UNICODE_STRING routineName;
        RtlInitUnicodeString(&routineName, L"ZwQueryInformationProcess");
        ZwQueryInformationProcess =
            (QUERY_INFO_PROCESS)MmGetSystemRoutineAddress(&routineName);
        if (NULL == ZwQueryInformationProcess) {
            DbgPrint("Cannot resolve ZwQueryInformationProcess\n");
        }
    }

    status = ZwQueryInformationProcess(NtCurrentProcess(),
        ProcessImageFileName,
        NULL, // buffer
        0, // buffer size
        &returnedLength);
    if (STATUS_INFO_LENGTH_MISMATCH != status) {
        return status;
    }

    bufferLength = returnedLength - sizeof(UNICODE_STRING);
    if (ProcessImageName->MaximumLength < bufferLength) {
        ProcessImageName->Length = (USHORT)bufferLength;
        return STATUS_BUFFER_OVERFLOW;
    }

    buffer = ExAllocatePoolWithTag(PagedPool, returnedLength, 'nacS');
    if (NULL == buffer) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessImageFileName,
                                        buffer, returnedLength, &returnedLength);

    if (NT_SUCCESS(status)) {
        imageName = (PUNICODE_STRING)buffer;
        RtlCopyUnicodeString(ProcessImageName, imageName);
    }

    ExFreePool(buffer);
    return status;

}

NTSTATUS
DirCtlSendFileInfo (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PUNICODE_STRING FileName
    )
/*++
Routine Description:
    This routine is called to send file info to user mode 
Arguments:
    FileName -   Name of the file.
Return Value:
    The status of the operation, hopefully STATUS_SUCCESS.  The common failure
    status will probably be STATUS_INSUFFICIENT_RESOURCES.
--*/
{
    NTSTATUS status = STATUS_SUCCESS;
    PDCAPP_NOTIFICATION notification = NULL;
    ULONG replyLength;
    UNICODE_STRING pni; 

    //  If not client port just return.
    if (DirCtlData.ClientPort == NULL) {
        return STATUS_SUCCESS;
    }

    try {
        notification = ExAllocatePoolWithTag(NonPagedPool, sizeof(DCAPP_NOTIFICATION), 'nacS');

        if (NULL == notification) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            leave;
        }
        RtlZeroMemory(notification, sizeof(DCAPP_NOTIFICATION));
        
        PEPROCESS objCurProcess = IoThreadToProcess(Data->Thread);
        HANDLE nCurProcID = PsGetProcessId(objCurProcess);

        pni.MaximumLength = DCAPP_BUFFER_SIZE;
        pni.Buffer = ExAllocatePoolWithTag(NonPagedPool, pni.MaximumLength, 'nacS');

        if (pni.Buffer != NULL) {
        
            pni.Length = 0;
            status = GetProcessImageName(&pni);
            if (NT_SUCCESS(status)){

                DbgPrint("ProcessName = %ws\n", pni.Buffer);
                RtlCopyMemory(&notification->ProcessName, pni.Buffer, pni.Length);
                pni.MaximumLength = 0;
                ExFreePool(pni.Buffer);
                pni.Buffer = NULL;
            }
        }

        RtlCopyMemory(&notification->FilePath, FileName->Buffer, FileName->Length);
        RtlCopyMemory(&notification->ProcessID, (void*)&nCurProcID, sizeof(ULONG));
        
        ExAcquireFastMutex(&g_MsgLock);
        status = FltSendMessage( DirCtlData.Filter,
                                    &DirCtlData.ClientPort,
                                    notification,
                                    sizeof(DCAPP_NOTIFICATION),
                                    notification,
                                    &replyLength,
                                    NULL );
        
        if (STATUS_SUCCESS != status) {
                //  Couldn't send message
                DbgPrint( "!!! dir ctl --- couldn't send message to user-mode, status 0x%X\n", status );
        }
    } finally {
        
        if (NULL != notification) {
            ExFreePoolWithTag( notification, 'nacS' );
        }
        ExReleaseFastMutex(&g_MsgLock);
    }
    return status;
}

NTSTATUS
DirCtlRecvMessage(
    IN PVOID PortCookie,
    IN PVOID InputBuffer OPTIONAL,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer OPTIONAL,
    IN ULONG OutputBufferLength,
    OUT PULONG ReturnOutputBufferLength
)
{
    UNREFERENCED_PARAMETER(PortCookie);
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(ReturnOutputBufferLength);

    ExAcquireFastMutex(&g_DirPathLock);
    try {
        if (((PDCAPP_INPUT)InputBuffer)->ONOFF == 1)
        {
            RtlInitUnicodeString(&g_DirPathToProtect, NULL);
            g_DirPathToProtect.MaximumLength = (USHORT)(((PDCAPP_INPUT)InputBuffer)->FileSize);
            g_DirPathToProtect.Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                            g_DirPathToProtect.MaximumLength, 'nacS');
            if (g_DirPathToProtect.Buffer == NULL) {
                ExReleaseFastMutex(&g_DirPathLock);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
                
            RtlCopyMemory(g_DirPathToProtect.Buffer, ((PDCAPP_INPUT)InputBuffer)->DirPath, 
                g_DirPathToProtect.MaximumLength);
            g_EnableProtection = TRUE;
        }
        else {
            g_EnableProtection = FALSE;
            RtlFreeUnicodeString(&g_DirPathToProtect);
        }
    }
    finally {
    }
    ExReleaseFastMutex(&g_DirPathLock);
      
    DbgPrint("!!! Dir ctl --- received message\n", DirCtlData.ClientPort);
    return STATUS_SUCCESS;
}
