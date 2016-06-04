// **********************************************************************
//
// Copyright (c) 2003-2016 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#ifndef ICE_UTIL_ICONV_STRING_CONVERTER
#define ICE_UTIL_ICONV_STRING_CONVERTER

#include <IceUtil/StringConverter.h>
#include <IceUtil/UndefSysMacros.h>

#include <algorithm>
#include <iconv.h>
#include <langinfo.h>
#include <string.h> // For strerror

#if (defined(__APPLE__) && _LIBICONV_VERSION < 0x010B) || defined(__FreeBSD__)
    //
    // See http://sourceware.org/bugzilla/show_bug.cgi?id=2962
    //
#   define ICE_CONST_ICONV_INBUF 1
#endif

namespace IceUtilInternal
{

//
// Converts charT encoded with internalCode to and from UTF-8 byte sequences
//
// The implementation allocates a pair of iconv_t on each thread, to avoid
// opening / closing iconv_t objects all the time.
//
//
template<typename charT>
class IconvStringConverter : public IceUtil::BasicStringConverter<charT>
{
public:

    IconvStringConverter(const char* = nl_langinfo(CODESET));

    virtual ~IconvStringConverter();

    virtual IceUtil::Byte* toUTF8(const charT*, const charT*, IceUtil::UTF8Buffer&) const;

    virtual void fromUTF8(const IceUtil::Byte*, const IceUtil::Byte*, std::basic_string<charT>&) const;

private:

    std::pair<iconv_t, iconv_t> createDescriptors() const;
    std::pair<iconv_t, iconv_t> getDescriptors() const;

    static void cleanupKey(void*);
    static void close(std::pair<iconv_t, iconv_t>);

    mutable pthread_key_t _key;
    const std::string _internalCode;
};

//
// Implementation
//

#ifdef __SUNPRO_CC
extern "C"
{
    typedef void (*IcePthreadKeyDestructor)(void*);
}
#endif

template<typename charT>
IconvStringConverter<charT>::IconvStringConverter(const char* internalCode) :
    _internalCode(internalCode)
{
    //
    // Verify that iconv supports conversion to/from internalCode
    //
    try
    {
        close(createDescriptors());
    }
    catch(const IceUtil::IllegalConversionException& sce)
    {
        throw IceUtil::IconvInitializationException(__FILE__, __LINE__, sce.reason());
    }

    //
    // Create thread-specific key
    //
#ifdef __SUNPRO_CC
    int rs = pthread_key_create(&_key, reinterpret_cast<IcePthreadKeyDestructor>(&cleanupKey));
#else
    int rs = pthread_key_create(&_key, &cleanupKey);
#endif

    if(rs != 0)
    {
        throw IceUtil::ThreadSyscallException(__FILE__, __LINE__, rs);
    }
}

template<typename charT>
IconvStringConverter<charT>::~IconvStringConverter()
{
    void* val = pthread_getspecific(_key);
    if(val != 0)
    {
        cleanupKey(val);
    }
    if(pthread_key_delete(_key) != 0)
    {
        assert(0);
    }
}

template<typename charT> std::pair<iconv_t, iconv_t>
IconvStringConverter<charT>::createDescriptors() const
{
    std::pair<iconv_t, iconv_t> cdp;

    const char* externalCode = "UTF-8";

    cdp.first = iconv_open(_internalCode.c_str(), externalCode);
    if(cdp.first == iconv_t(-1))
    {
        std::ostringstream os;
        os << "iconv cannot convert from " << externalCode << " to " << _internalCode;
        throw IceUtil::IllegalConversionException(__FILE__, __LINE__, os.str());
    }

    cdp.second = iconv_open(externalCode, _internalCode.c_str());
    if(cdp.second == iconv_t(-1))
    {
        iconv_close(cdp.first);
        std::ostringstream os;
        os << "iconv cannot convert from " << _internalCode << " to " << externalCode;
        throw IceUtil::IllegalConversionException(__FILE__, __LINE__, os.str());
    }
    return cdp;
}

template<typename charT> std::pair<iconv_t, iconv_t>
IconvStringConverter<charT>::getDescriptors() const
{
    void* val = pthread_getspecific(_key);
    if(val != 0)
    {
        return *static_cast<std::pair<iconv_t, iconv_t>*>(val);
    }
    else
    {
        std::pair<iconv_t, iconv_t> cdp = createDescriptors();
        int rs = pthread_setspecific(_key, new std::pair<iconv_t, iconv_t>(cdp));
        if(rs != 0)
        {
            throw IceUtil::ThreadSyscallException(__FILE__, __LINE__, rs);
        }
        return cdp;
    }
}

template<typename charT> /*static*/ void
IconvStringConverter<charT>::cleanupKey(void* val)
{
    std::pair<iconv_t, iconv_t>* cdp = static_cast<std::pair<iconv_t, iconv_t>*>(val);

    close(*cdp);
    delete cdp;
}

template<typename charT> /*static*/ void
IconvStringConverter<charT>::close(std::pair<iconv_t, iconv_t> cdp)
{
#ifndef NDEBUG
    int rs = iconv_close(cdp.first);
    assert(rs == 0);

    rs = iconv_close(cdp.second);
    assert(rs == 0);
#else
    iconv_close(cdp.first);
    iconv_close(cdp.second);
#endif
}

template<typename charT> IceUtil::Byte*
IconvStringConverter<charT>::toUTF8(const charT* sourceStart,
                                    const charT* sourceEnd,
                                    IceUtil::UTF8Buffer& buf) const
{
    iconv_t cd = getDescriptors().second;

    //
    // Reset cd
    //
#ifdef NDEBUG
    iconv(cd, 0, 0, 0, 0);
#else
    size_t rs = iconv(cd, 0, 0, 0, 0);
    assert(rs == 0);
#endif

#ifdef ICE_CONST_ICONV_INBUF
    const char* inbuf = reinterpret_cast<const char*>(sourceStart);
#else
    char* inbuf = reinterpret_cast<char*>(const_cast<charT*>(sourceStart));
#endif
    size_t inbytesleft = (sourceEnd - sourceStart) * sizeof(charT);
    char* outbuf  = 0;

    size_t count = 0;
    //
    // Loop while we need more buffer space
    //
    do
    {
        size_t howMany = std::max(inbytesleft, size_t(4));
        outbuf = reinterpret_cast<char*>(buf.getMoreBytes(howMany,
                                                          reinterpret_cast<IceUtil::Byte*>(outbuf)));
        count = iconv(cd, &inbuf, &inbytesleft, &outbuf, &howMany);
    } while(count == size_t(-1) && errno == E2BIG);

    if(count == size_t(-1))
    {
        throw IceUtil::IllegalConversionException(__FILE__,
                                                  __LINE__,
                                                  errno != 0 ? strerror(errno) : "Unknown error");
    }
    return reinterpret_cast<IceUtil::Byte*>(outbuf);
}

template<typename charT> void
IconvStringConverter<charT>::fromUTF8(const IceUtil::Byte* sourceStart, const IceUtil::Byte* sourceEnd,
                                      std::basic_string<charT>& target) const
{
    iconv_t cd = getDescriptors().first;

    //
    // Reset cd
    //
#ifdef NDEBUG
    iconv(cd, 0, 0, 0, 0);
#else
    size_t rs = iconv(cd, 0, 0, 0, 0);
    assert(rs == 0);
#endif

#ifdef ICE_CONST_ICONV_INBUF
    const char* inbuf = reinterpret_cast<const char*>(sourceStart);
#else
    char* inbuf = reinterpret_cast<char*>(const_cast<IceUtil::Byte*>(sourceStart));
#endif
    size_t inbytesleft = sourceEnd - sourceStart;

    char* outbuf = 0;
    size_t outbytesleft = 0;
    size_t count = 0;

    //
    // Loop while we need more buffer space
    //
    do
    {
        size_t bytesused = 0;
        if(outbuf != 0)
        {
            bytesused = outbuf - reinterpret_cast<const char*>(target.data());
        }

        const size_t increment = std::max<size_t>(inbytesleft, 4);
        target.resize(target.size() + increment);
        outbuf = const_cast<char*>(reinterpret_cast<const char*>(target.data())) + bytesused;
        outbytesleft += increment * sizeof(charT);

        count = iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);

    } while(count == size_t(-1) && errno == E2BIG);

    if(count == size_t(-1))
    {
        throw IceUtil::IllegalConversionException(__FILE__,
                                                  __LINE__,
                                                  errno != 0 ? strerror(errno) : "Unknown error");
    }

    target.resize(target.size() - (outbytesleft / sizeof(charT)));
}
}

namespace IceUtil
{

template<typename charT>
ICE_HANDLE <BasicStringConverter<charT> >
createIconvStringConverter(const char* internalCode = nl_langinfo(CODESET))
{
    return ICE_MAKE_SHARED(IceUtilInternal::IconvStringConverter<charT>, internalCode);
}

}

#endif
