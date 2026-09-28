#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
using std::isfinite; using std::isnan; using std::isinf; using std::abs;
#ifndef PI
#define PI 3.14159265358979323846
#endif
#define radians(x) ((x)*PI/180.0)
#define degrees(x) ((x)*180.0/PI)
#ifndef constrain
#define constrain(x,a,b) ((x)<(a)?(a):((x)>(b)?(b):(x)))
#endif
template<class A,class B> auto min(A a,B b)->typename std::common_type<A,B>::type {return a<b?a:b;}
template<class A,class B> auto max(A a,B b)->typename std::common_type<A,B>::type {return a>b?a:b;}
struct Print { template<class... A> size_t print(A...) { return 0; } };
struct Printable { virtual size_t printTo(Print&) const=0; virtual ~Printable()=default; };
class String {
    std::string value;
public:
    String(const char* s=""): value(s?s:"") {}
    String& operator=(const char* s) {value=s?s:""; return *this;}
    void trim() {}
    size_t length() const {return value.size();}
    void toCharArray(char* out,size_t capacity) const {if(capacity){std::strncpy(out,value.c_str(),capacity);out[capacity-1]=0;}}
};
uint32_t millis();
uint32_t micros();
