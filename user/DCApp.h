#pragma once
/*++
Copyright (c)
Module Name:
    DCApp.h
Abstract:
    Header file which contains the structures, type definitions,
    constants, global variables and function prototypes for the
    user mode part of the DCAPP.
--*/
#ifndef __DCAPP_H__
#define __DCAPP_H__
#pragma pack(1)

typedef struct _DCAPP_MESSAGE {
    //  Required structure header.
    FILTER_MESSAGE_HEADER MessageHeader;
    //  Private DCAPP-specific fields begin here.
    DCAPP_NOTIFICATION Notification;
    //  Overlapped structure: this is not really part of the message
    //  However we embed it instead of using a separately allocated overlap structure
    OVERLAPPED Ovlp;
} DCAPP_MESSAGE, * PDCAPP_MESSAGE;

typedef struct _DCAPP_REPLY_MESSAGE {
    //  Required structure header.
    FILTER_REPLY_HEADER ReplyHeader;
    //  Private DCAPP-specific fields begin here.
    //DCAPP_REPLY Reply;
} DCAPP_REPLY_MESSAGE, * PDCAPP_REPLY_MESSAGE;

#endif //  __DCAPP_H__



