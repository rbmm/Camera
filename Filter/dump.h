#pragma once

void DumpBytes(const UCHAR* pb, ULONG cb);

void DumpGuid(const GUID* guid, PCSTR Preffix = "", PCSTR Suffix = "\r\n");

void Dump(PRECT prc, PCSTR Preffix = "", PCSTR Suffix = "\r\n");

void Dump(PKS_VIDEOINFOHEADER VideoInfo);

void Dump(PKSDATAFORMAT DataFormat);

void Dump(PKSPIN_CONNECT p);

void Dump(PKS_DATAFORMAT_VIDEOINFOHEADER p, ULONG cb);

PCSTR get(KSPIN_DATAFLOW dwFlowDirection, PSTR buf, int len);

PCSTR get(KSPIN_COMMUNICATION dwCommunication, PSTR buf, int len);

void Dump(PKSEVENTDATA p);

void Dump(PKSIDENTIFIER p, bool bProbe = true);

void DumpHeader(PKSSTREAM_HEADER psh);

void DumpFrame(PKS_FRAME_INFO pfi);