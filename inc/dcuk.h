/*++

Copyright (c) 

Module Name:
    dcuk.h
Abstract:
    Header file which contains the structures, type definitions,
    constants, global variables and function prototypes that are
    shared between kernel and user mode.
Environment:
    Kernel & user mode
--*/

#ifndef __DCUK_H__
#define __DCUK_H__

//
//  Name of port used to communicate
//

const WCHAR DCAPPPortName[] = L"\\DirCtlPort";


#define DCAPP_BUFFER_SIZE   256*2

typedef struct _DCAPP_NOTIFICATION {

    UCHAR FilePath[DCAPP_BUFFER_SIZE];
    UCHAR ProcessName[DCAPP_BUFFER_SIZE];
    ULONG ProcessID;
} DCAPP_NOTIFICATION, *PDCAPP_NOTIFICATION;

typedef struct _DCAPP_INPUT {

    ULONG ONOFF;
    ULONG FileSize;
    UCHAR DirPath[DCAPP_BUFFER_SIZE];
} DCAPP_INPUT, *PDCAPP_INPUT;

#endif //  __DCUK_H__


