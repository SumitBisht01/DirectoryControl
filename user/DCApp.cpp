// DCApp.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include "windows.h"
#include <fltuser.h>
#include "dcuk.h"
#include "dcapp.h"

//  Default and Maximum number of threads.
#define DCAPP_DEFAULT_REQUEST_COUNT       5
#define DCAPP_DEFAULT_THREAD_COUNT        1
#define DCAPP_MAX_THREAD_COUNT            2
#define MAX_PATH_LEN                      MAX_PATH*2
//  Context passed to worker threads
typedef struct _DCAPP_THREAD_CONTEXT {
    HANDLE Port;
    HANDLE Completion;
} DCAPP_THREAD_CONTEXT, * PDCAPP_THREAD_CONTEXT;

BOOL g_bContinue = TRUE;
VOID Usage(VOID) {
    wprintf(L"Connects to the directory protect filter \n");
    wprintf(L"Usage: DCAPP [directory path] \n");
}

/*++
Routine Description
    This is a worker thread that
Arguments
    Context  - This thread context has a pointer to the port handle we use to send/receive messages,
                and a completion port handle that was already associated with the comm. port by the caller
Return Value
    HRESULT indicating the status of thread exit.
--*/
DWORD DCAPPWorker( _In_ PDCAPP_THREAD_CONTEXT Context )
{
    PDCAPP_NOTIFICATION notification;
    DCAPP_REPLY_MESSAGE replyMessage;
    PDCAPP_MESSAGE message = NULL;
    LPOVERLAPPED pOvlp;
    BOOL result = FALSE;
    DWORD outSize = 0;
    HRESULT hr = 0;
    ULONG_PTR key;

    while (TRUE) {

        if (g_bContinue == FALSE)
            break;

        //  Poll for messages from the filter component to scan.
        result = GetQueuedCompletionStatus(Context->Completion, &outSize, &key, &pOvlp, INFINITE);

        //  Obtain the message: note that the message we sent down via FltGetMessage() may NOT be
        //  the one dequeued off the completion queue: this is solely because there are multiple
        //  threads per single port handle. Any of the FilterGetMessage() issued messages can be
        //  completed in random order - and we will just dequeue a random one.
        message = CONTAINING_RECORD(pOvlp, DCAPP_MESSAGE, Ovlp);
        if (!result) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            break;
        }
                
        wprintf(L"Received message, size %Id \n", pOvlp->InternalHigh);

        notification = &message->Notification;
        
        /*int nLen = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)notification->FilePath, -1, NULL, 0);
        WCHAR* wFilePath = new WCHAR[nLen];
        MultiByteToWideChar(CP_UTF8, 0, (LPCCH)notification->FilePath, -1, wFilePath, nLen);
        
        nLen = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)notification->FilePath, -1, NULL, 0);
        WCHAR* wProcessPath = new WCHAR[nLen];
        MultiByteToWideChar(CP_UTF8, 0, (LPCCH)notification->ProcessName, -1, wProcessPath, nLen);*/

        wprintf(L"File path %s Process (P)ID %d Process path %s \n", 
            (WCHAR*)notification->FilePath, notification->ProcessID, (WCHAR*)notification->ProcessName);
        replyMessage.ReplyHeader.Status = 0;
        replyMessage.ReplyHeader.MessageId = message->MessageHeader.MessageId;
        
        /*delete[] wFilePath;
        delete[] wProcessPath;*/

        hr = FilterReplyMessage(Context->Port, (PFILTER_REPLY_HEADER)&replyMessage,
                                sizeof(replyMessage));
        if (SUCCEEDED(hr)) {
            wprintf(L"Replied message \n");
        }
        else {
            wprintf(L"DCAPP: Error replying message. Error = 0x%X \n", hr);
            break;
        }

        memset(&message->Ovlp, 0, sizeof(OVERLAPPED));
        hr = FilterGetMessage(Context->Port, &message->MessageHeader,
                                FIELD_OFFSET(DCAPP_MESSAGE, Ovlp), &message->Ovlp);

        if (hr != HRESULT_FROM_WIN32(ERROR_IO_PENDING)) {
            break;
        }
    }

    if (!SUCCEEDED(hr)) {
        if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE)) {
            wprintf(L"DCAPP: Port is disconnected, probably due to DCAPP filter unloading.\n");
        }
        else {
            printf("DCAPP: Unknown error occured. Error = 0x%X \n", hr);
        }
    }

    free(message);
    return hr;
}

int wmain(int argc, wchar_t* argv[])
{
    DWORD requestCount = DCAPP_DEFAULT_REQUEST_COUNT;
    DWORD threadCount = DCAPP_DEFAULT_THREAD_COUNT;
    HANDLE threads[DCAPP_MAX_THREAD_COUNT];
    DCAPP_THREAD_CONTEXT context;
    HANDLE port, completion;
    PDCAPP_MESSAGE msg;
    DWORD threadId;
    HRESULT hr;
    DWORD i, j;

    if (argc != 2) {
        Usage();
        return 1;
    }

    size_t dwPathLen = wcsnlen_s(argv[1], MAX_PATH_LEN);
    if (dwPathLen == 0) {
        return 1;
    }

    wprintf(L"DCAPP: Connecting to the filter ...\n");

    hr = FilterConnectCommunicationPort(DCAPPPortName, 0, NULL, 0, NULL, &port);
    if (IS_ERROR(hr)) {
        wprintf(L"ERROR: Connecting to filter port: 0x%08x\n", hr);
        return 2;
    }

    completion = CreateIoCompletionPort(port, NULL, 0, threadCount);
    if (completion == NULL) {
        wprintf(L"ERROR: Creating completion port: %d\n", GetLastError());
        CloseHandle(port);
        return 3;
    }
    wprintf(L"DCAPP: Port = 0x%p Completion = 0x%p\n", port, completion);

    context.Port = port;
    context.Completion = completion;
    BOOL bContinue = TRUE;
    for (i = 0; i < threadCount; i++) {

        threads[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)DCAPPWorker, &context,
            0, &threadId);

        if (threads[i] == NULL) {
            hr = GetLastError();
            wprintf(L"ERROR: Couldn't create thread: %d\n", hr);
            bContinue = FALSE;
            break;
        }

        for (j = 0; j < requestCount; j++) {

            msg = (DCAPP_MESSAGE*)malloc(sizeof(DCAPP_MESSAGE));

            if (msg == NULL) {
                hr = ERROR_NOT_ENOUGH_MEMORY;
                bContinue = FALSE;
                break;
            }

            memset(&msg->Ovlp, 0, sizeof(OVERLAPPED));
            hr = FilterGetMessage(port, &msg->MessageHeader, FIELD_OFFSET(DCAPP_MESSAGE, Ovlp), 
                &msg->Ovlp);

            if (hr != HRESULT_FROM_WIN32(ERROR_IO_PENDING)) {
                free(msg);
                bContinue = FALSE;
                break;
            }
        }
    }

    if (bContinue) {

        DWORD dwByteReturned = 0;
        int nWcharsSize = 0;
        WCHAR szDosName[MAX_PATH_LEN] = L"";
        WCHAR szDriveName[3] = L"";
        memcpy_s(szDriveName, 2 * sizeof(WCHAR), argv[1], 2 * sizeof(WCHAR));
        QueryDosDeviceW(szDriveName, szDosName, MAX_PATH_LEN);
        DWORD dwlstErr = GetLastError();
        WCHAR* szDirPath = argv[1];
        if (dwlstErr == 0) {
            wmemcpy_s(szDosName + wcslen(szDosName), dwPathLen - 2, szDirPath + 2, dwPathLen - 2);
            int nVal = _wcsnicmp(szDosName + wcslen(szDosName), L"\\", 1);
            if (nVal != 0) {
                wmemcpy_s(szDosName + wcslen(szDosName), 1, L"\\", 1);
            }

            DCAPP_INPUT input;
            input.ONOFF = 1;
            input.FileSize = (ULONG)wcslen(szDosName) * sizeof(WCHAR);

            memcpy(input.DirPath, szDosName, wcslen(szDosName) * sizeof(WCHAR));
            //To start the directory protection
            hr = FilterSendMessage(port, &input, sizeof(DCAPP_INPUT), NULL, 0, &dwByteReturned);

            if (hr != S_OK) {
                wprintf(L"Failed to send the input to the driver \n");
            }

            //press any key to stop the directory protection.
            getchar();

            g_bContinue = FALSE;
            //To stop the directory protection.
            input.ONOFF = 0;
            input.FileSize = 0;
            hr = FilterSendMessage(port, &input, sizeof(DCAPP_INPUT), NULL, 0, &dwByteReturned);
        }

        DWORD dwExitCode = 0;
        for (i = 0; i < threadCount; i++) {
            TerminateThread(threads[i], dwExitCode);
        }
        WaitForMultipleObjectsEx(i, threads, TRUE, INFINITE, FALSE);
    }

    wprintf(L"DCAPP:  All done. Result = 0x%08x\n", hr);
    CloseHandle(port);
    CloseHandle(completion);

    return hr;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
