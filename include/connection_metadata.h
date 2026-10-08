#pragma once
#include <cstring>
#include "protocol.h"
#include "memory.h"

// Native codecs 0x437260/0x437320: full SteamID64 in network byte order.
inline uint64_t UbNetworkId(const uint8_t* p) {
    uint64_t value=0;
    for (unsigned i=0;i<8;i++) value=(value<<8)|p[i];
    return value;
}
inline int UbBase64Digit(uint8_t c) {
    if (c>='A' && c<='Z') return c-'A';
    if (c>='a' && c<='z') return c-'a'+26;
    if (c>='0' && c<='9') return c-'0'+52;
    return c=='+'?62:c=='/'?63:-1;
}
inline bool UbDecodeRoom(const uint8_t* bytes,size_t size,uint8_t (&out)[202]) {
    // Original 0x5f8aa0 produces a 202-byte record, then 0x4d58e0 Base64;
    // 0x5f8610 reads its big-endian encoded length and 0x4d5a60 decodes it.
    if (!bytes || size<274 || bytes[0]!=1 || bytes[1]!=16) return false;
    size_t used=0;
    for (size_t i=2;i<274;i+=4) {
        int a=UbBase64Digit(bytes[i]),b=UbBase64Digit(bytes[i+1]);
        if (a<0 || b<0) return false;
        if (i==270) {
            if (bytes[i+2]!='=' || bytes[i+3]!='=' || (b&15)) return false;
            out[used++]=uint8_t((a<<2)|(b>>4));break;
        }
        int c=UbBase64Digit(bytes[i+2]),d=UbBase64Digit(bytes[i+3]);
        if (c<0 || d<0) return false;
        out[used++]=uint8_t((a<<2)|(b>>4));out[used++]=uint8_t((b<<4)|(c>>2));out[used++]=uint8_t((c<<6)|d);
    }
    return used==sizeof(out);
}
inline uint8_t UbConnectionFromWire(const uint8_t* bytes,size_t size,uint64_t owner_id) {
    if (!bytes || !UbPlayerId(owner_id)) return UB_CONNECTION_UNKNOWN;
    // Ranked producer 0x5f9e40/parser 0x5f9ac0; room producer 0x5f8aa0/parser 0x5f8610.
    // The earlier identity is a matchmaking target, not the publishing account.
    // Ranked 0x5f9e40 writes the publisher (0xc49328) in the trailing 8 bytes;
    // room 0x5f8aa0 likewise writes object+0x240 after its Wi-Fi field.
    // Bind only that publisher to the search row's exact owner ID.
    bool ranked=size>=0x84 && UbNetworkId(bytes+0x7c)==owner_id;
    uint8_t decoded[202]{};
    bool room=UbDecodeRoom(bytes,size,decoded) && UbNetworkId(decoded+0xc2)==owner_id;
    if (ranked==room) return UB_CONNECTION_UNKNOWN;
    const uint8_t* record=ranked?bytes:decoded;
    size_t offset=ranked?0x78:0xc0;
    unsigned raw=(unsigned(record[offset])<<8)|record[offset+1];
    if (raw>1) return UB_CONNECTION_UNKNOWN;
    return raw==1?UB_CONNECTION_WIFI:UB_CONNECTION_WIRED;
}
inline void UbReadConnection(uintptr_t row,UbCandidate& candidate) {
    uint32_t data=0,size=0;
    uint8_t bytes[274]{};
    if (UbGet(row+0x34,data) && data && UbGet(uintptr_t(data)+0x400,size) && size==0x400 &&
        UbRead(data,bytes,sizeof(bytes)))
        candidate.connection_type=UbConnectionFromWire(bytes,sizeof(bytes),candidate.steam_id);
}
inline void UbReadCachedName(uintptr_t row,UbCandidate& candidate) {
    uint32_t pointer=0;char raw[44]{};
    if (!UbGet(row+0x30,pointer) || !pointer || !UbRead(pointer,raw,sizeof(raw))) return;
    const char* end=static_cast<const char*>(memchr(raw,0,sizeof(raw)));
    if (!end || end==raw) return;
    int length=int(end-raw);wchar_t wide[44]{};
    // Native 0x4e0880 converts Steam UTF-8 to the game's current CP_ACP and
    // 0x4ff460 copies up to 40 bytes. Decode that cache; do not call Steam again.
    int count=MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,raw,length,wide,44);
    if (!count && length==40) count=MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,raw,--length,wide,44);
    if (!count) return;
    int used=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,count,candidate.name_utf8,128,nullptr,nullptr);
    if (used>0) {candidate.name_utf8[used]=0;candidate.flags|=UB_NAME_KNOWN;}
}
