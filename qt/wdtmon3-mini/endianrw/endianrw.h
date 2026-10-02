#ifndef ENDIANRW_ENDIANRW_H
#define ENDIANRW_ENDIANRW_H

#include <QtCore/qglobal.h>
#include <QString>
#include <fstream>
#include <stdint.h>

#if defined(Q_OS_WIN32) || !defined(__GNUC__) || defined(__clang__) || defined(Q_OS_ANDROID)
#include "winbswap.h"
#define ENDIANRWBSWAP32 WINBSWAP32
#define ENDIANRWBSWAP64 WINBSWAP64
#else
#define ENDIANRWBSWAP32 __bswap_32
#define ENDIANRWBSWAP64 __bswap_64
#endif

inline uint16_t swap_16(uint16_t val)
{
    return (((val>>8)&0x00ff)|((val<<8)&0xff00));
}

inline void swap_16(char* data)
{
    qSwap(data[0],data[1]);
}

inline void swap_32(char* data)
{
    char tmp;
    tmp = data[0];
    data[0] = data[3];
    data[3] = tmp;
    tmp = data[2];
    data[2] = data[1];
    data[1] = tmp;
}

inline void swap_64(char* data)
{
    char tmp;
    tmp = data[0];
    data[0] = data[7];
    data[7] = tmp;
    tmp = data[1];
    data[1] = data[6];
    data[6] = tmp;
    tmp = data[2];
    data[2] = data[5];
    data[5] = tmp;
    tmp = data[3];
    data[3] = data[4];
    data[4] = tmp;
}

inline uint32_t get_uint32_le(const uint8_t* data)
{
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    return (((uint32_t)*data))|(((uint32_t)data[1])<<8)|(((uint32_t)data[2])<<16)|(((uint32_t)data[3])<<24);
#else
    return *(reinterpret_cast<const uint32_t*>(data));
#endif
}

////////////////////////////////////////////

inline void read_skip(std::istream& istm, uint64_t size)
{
    istm.seekg(size,std::ios_base::cur);
}

inline void read_return(std::istream& istm, int64_t val)
{
    istm.seekg(-1*val,std::ios_base::cur);
}

inline uint8_t read_uint8(std::istream& istm)
{
    return static_cast<uint8_t>(istm.get());
}

inline uint16_t read_uint16(std::istream& istm, bool need_swapping)
{
    static union {
        uint16_t val;
        char data[2];
    };

    istm.read(data,2);
    return (need_swapping ? swap_16(val) : (val) );
}

uint32_t inline read_uint32(std::istream& istm, bool need_swapping)
{
    uint32_t val;
    istm.read(reinterpret_cast<char*>(&val),4);
    return (need_swapping ?  ENDIANRWBSWAP32(val) : val);
}

uint64_t inline read_uint64(std::istream& istm, bool need_swapping)
{
    uint64_t val;
    istm.read(reinterpret_cast<char*>(&val),8);
    return (need_swapping ?  ENDIANRWBSWAP64(val) : val);
}

float inline read_float(std::istream& istm, bool need_swapping)
{
    uint32_t val = read_uint32(istm, need_swapping);
    float ret;
    memcpy(&ret,&val,4);
    return ret;
}

double inline read_double(std::istream& istm, bool need_swapping)
{
    uint64_t val = read_uint64(istm, need_swapping);
    double ret;
    memcpy(&ret,&val,8);
    return ret;
}

inline uint16_t read_uint16_be(std::istream& istm)
{
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    return read_uint16(istm,false);
#else
    return read_uint16(istm,true);
#endif
}

inline uint32_t read_uint32_be(std::istream& istm)
{
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    return read_uint32(istm,false);
#else
    return read_uint32(istm,true);
#endif
}

inline uint32_t read_uint32_le(std::istream& istm)
{
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    return read_uint32(istm,true);
#else
    return read_uint32(istm,false);
#endif
}

inline uint64_t read_uint64_be(std::istream& istm)
{
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    return read_uint64(istm,false);
#else
    return read_uint64(istm,true);
#endif
}

inline double read_double_be(std::istream& istm)
{
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    return read_double(istm,false);
#else
    return read_double(istm,true);
#endif
}

inline uint16_t read_uint16_le(const void* array)
{
    uint16_t val;
    memcpy(&val,array,2);
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    return swap_16(val);
#else
    return val;
#endif
}

inline uint16_t read_uint16_be(const void* array)
{
    uint16_t val;
    memcpy(&val,array,2);
#if Q_BYTE_ORDER != Q_BIG_ENDIAN
    return swap_16(val);
#else
    return val;
#endif
}

inline uint32_t read_uint32_be(const void* array)
{
    uint32_t val;
    memcpy(&val,array,4);
#if Q_BYTE_ORDER != Q_BIG_ENDIAN
    val = ENDIANRWBSWAP32(val);
#endif
    return val;
}

inline uint32_t read_uint32_le(const void* array)
{
    uint32_t val;
    memcpy(&val,array,4);
#if Q_BYTE_ORDER != Q_LITTLE_ENDIAN
    val = ENDIANRWBSWAP32(val);
#endif
    return val;
}

inline uint64_t read_uint64_be(const void* array)
{
    uint64_t val;
    memcpy(&val,array,8);
#if Q_BYTE_ORDER != Q_BIG_ENDIAN
    val = ENDIANRWBSWAP64(val);
#endif
    return val;
}


template <typename T>
T read_value(std::istream& istm, bool need_swapping)
{
    size_t tsize = sizeof(T);
    union
    {
        T val;
        char data[sizeof(T)];
    };
    istm.read(data,tsize);
    if(!need_swapping)
        return val;

    char data_cpy[tsize];
    memcpy(data_cpy,data,tsize);
    for(size_t i = 0;i<tsize;++i)
    {
        data[i] = data_cpy[tsize-1-i];
    }
    return val;
}

inline void read_arbitrary(void* data, uint64_t size, std::istream& istm)
{
    istm.read(reinterpret_cast<char*>(data),size);
}

//////////////////////////////////////////////////////

inline void write_arbitrary(const void* data, qint64 size, std::ostream& ostm);


inline void write_uint8(uint8_t value, std::ostream& ostm)
{
    ostm.put (static_cast<char>(value));
}

template<size_t SIZE>
inline void write_padding(std::ostream& ostm)
{
    char padding[SIZE];
    memset(padding, 0, SIZE);
    ostm.write (padding, SIZE);
}

inline void write_padding(uint64_t size, std::ostream& ostm)
{
    if(size)
    {
        char padding[size];
        memset(padding, 0, size);
        ostm.write (padding, size);
    }
}

inline void write_uint16_native(uint16_t value, std::ostream& ostm)
{
    ostm.write (reinterpret_cast<const char*>(&value), 2);
}

inline void write_uint32_native(uint32_t value, std::ostream& ostm)
{
    ostm.write (reinterpret_cast<const char*>(&value), 4);
}

inline void write_uint64_native(uint64_t value, std::ostream& ostm)
{
    ostm.write (reinterpret_cast<const char*>(&value), 8);
}

inline void write_double_native(double value, std::ostream& ostm)
{
    ostm.write (reinterpret_cast<const char*>(&value), sizeof(double));
}

template<typename T>
inline void write_array_native(const T* array, uint64_t array_size, std::ostream& ostm)
{
    ostm.write (reinterpret_cast<const char*>(array), array_size*sizeof(T));
}

inline void write_uint16_be(uint16_t val, std::ostream& ostm)
{
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    val = ((val>>8)&0x00FF)|((val<<8)&0xFF00);
#endif
    write_arbitrary(&val,2, ostm);
}

inline void write_uint16_be(uint16_t val, void* output)
{
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    val = ((val>>8)&0x00FF)|((val<<8)&0xFF00);
#endif
    memcpy(output,&val,2);
}

inline void write_uint16_le(uint16_t val, void* output)
{
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    val = ((val>>8)&0x00FF)|((val<<8)&0xFF00);
#endif
    memcpy(output,&val,2);
}

inline void write_uint32_be(uint32_t val, std::ostream& ostm)
{
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    val = ENDIANRWBSWAP32(val);
#elif Q_BYTE_ORDER == Q_BIG_ENDIAN
    //nothing to do
#else
#error Either BIG or LITTLE endian must be defined
#endif
    write_arbitrary(&val,4, ostm);
}


inline void write_uint32_be(uint32_t val, void* output)
{
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    val = ENDIANRWBSWAP32(val);
#endif
    memcpy(output,&val,4);
}

inline void write_uint32_le(uint32_t val, std::ostream& ostm)
{
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    val = ENDIANRWBSWAP32(val);
#elif Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    //nothing to do
#else
#error Either BIG or LITTLE endian must be defined
#endif
    write_arbitrary(&val,4, ostm);
}

inline void write_uint32_le(uint32_t val, void* output)
{
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    val = ENDIANRWBSWAP32(val);
#endif
    memcpy(output,&val,4);
}

inline void write_uint64_be(quint64 val, std::ostream& ostm)
{
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    val = ENDIANRWBSWAP64(val);
#endif
    write_arbitrary(&val,8, ostm);
}

inline void write_uint64_be(quint64 val, void* output)
{
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    val = ENDIANRWBSWAP64(val);
#elif Q_BYTE_ORDER == Q_BIG_ENDIAN
    //nothing to do
#endif
    memcpy(output,&val,8);
}

inline void write_uint64_le(quint64 val, void* output)
{
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    val = ENDIANRWBSWAP64(val);
#else
    //nothing to do
#endif
    memcpy(output,&val,8);
}

/*Assumes sizeof(double)==64bit*/
inline void write_double_be(double val, std::ostream& ostm)
{
    uint64_t* cast = reinterpret_cast<uint64_t*>(&val);
    write_uint64_be(*cast,ostm);
}

inline void write_arbitrary(const void* data, qint64 size, std::ostream& ostm)
{
    ostm.write(reinterpret_cast<const char*>(data),size);
}

template <typename T>
void write_value_native(const T* value, std::ostream& ostm)
{
    ostm.write (reinterpret_cast<const char*>(value), sizeof(T));
}

/////////////////////////////////////////////////

namespace QBinaryTools
{

inline QString printBuffer(const uint8_t* data, size_t size)
{
    QString pstr = QString("[%1 bytes]: ").arg(size);
    if(!data)
    {
        pstr.append("NULL");
    }
    else
    {
        for(size_t i=0;i<size;++i)
        {
            pstr+=QString("%1|").arg((int)*(data++),2,16,QLatin1Char('0'));
        }
    }
    return pstr;
}

}

#endif  // ENDIANRW_ENDIANRW_H
