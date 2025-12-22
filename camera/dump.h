#pragma once

void DumpGuid(const GUID* guid, PCSTR Preffix = "", PCSTR Suffix = "\r\n");

void Dump(PRECT prc, PCSTR Preffix = "", PCSTR Suffix = "\r\n");

void Dump(PKS_VIDEOINFOHEADER VideoInfo);

PCSTR get(KSPIN_DATAFLOW dwFlowDirection, PSTR buf, int len);

PCSTR get(KSPIN_COMMUNICATION dwCommunication, PSTR buf, int len);