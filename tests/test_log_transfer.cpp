#include "../log_transfer.h"
#include <cassert>
#include <cstdio>
#include <string>
int main() {
    LogByteQueue<8> q;
    q.push("abcdefghijk"); assert(q.size()==8 && q.dropped==3);
    uint8_t bytes[10]={}; assert(q.pop(bytes,3)==3);
    assert(std::string((char*)bytes,3)=="abc");
    q.push("123"); assert(q.pop(bytes,10)==8);
    assert(std::string((char*)bytes,8)=="defgh123");
    assert(q.pop(bytes,10)==0);
    for(uint32_t ofs : {0u,89u,90u,139u,140u,55999u,56000u,UINT32_MAX}) {
        for(uint32_t count : {0u,1u,89u,90u,91u,UINT32_MAX}) {
            LogTransferCursor c; c.start(7,56000,ofs,count);
            const uint32_t available=ofs<56000?56000-ofs:0;
            const uint32_t expected=count<available?count:available;
            uint32_t sent=0, packets=0;
            while(c.active) {uint8_t n=c.nextCount(); assert(n<=90); sent+=n; ++packets; c.advance(n);}
            assert(sent==expected && c.offset==ofs+expected && packets>=1 && c.generation==7);
        }
    }
    LogOutputChunk chunk; strcpy(chunk.data,"abcdefgh"); chunk.size=8;
    std::string output;
    auto writer=[&](const uint8_t*p,size_t n){size_t actual=n>2?2:n;output.append((const char*)p,actual);return actual;};
    chunk.send(0,64,writer); assert(output.empty());
    chunk.send(1,64,writer); assert(output=="a");
    while(!chunk.empty()) chunk.send(100,3,writer);
    assert(output=="abcdefgh");
    chunk.clear(); chunk.send(100,64,writer); assert(output=="abcdefgh");
    puts("bounded log transfer regression: PASS");
}
