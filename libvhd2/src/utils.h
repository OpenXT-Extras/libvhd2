/*
 * Copyright (c) 2012 Citrix Systems, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
    @file some useful utilities
*/


#ifndef __UTILS_H__
#define __UTILS_H__

#include <assert.h>
#include <inttypes.h>
#include <syslog.h>
#include <string.h>
#include <endian.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <asm-generic/errno-base.h>

#include <vector>
using std::vector;


//-- define _DEBUG symbol for DEBUG builds; NDEBUG is assumed to be defined for RELEASE builds
#ifndef NDEBUG
    #warning "using DEBUG build !"
    #ifndef _DEBUG
    #define _DEBUG
    #endif
#endif


//-- define compile-time assert
#define ASSERT_COMPILE(expr)    int __static_assert(int static_assert_failed[(expr)?1:-1])


//-- define logging macros
#ifdef _DEBUG
    #define DBG_LOG(fmt, args...)	syslog(LOG_DEBUG, "libvhd2::%s(): "fmt,__func__, ##args)
#else
    #define DBG_LOG(fmt, args...)
#endif

//-- define ASSERT that drops a line to the log
#ifdef _DEBUG
    #define ASSERT(expr)    if(!(expr)) { DBG_LOG("Assert failure! file:%s, line:%d", __FILE__, __LINE__); assert((expr)); }
#else
    #define ASSERT(expr)
#endif

void StrLog(std::string* apStrOut, const char *format, ...);

//-----------------------------------------------------------------------------
/** fault codes, see Fault() definition */
enum TFault
{
    ENotImplemented,                ///< 0  The feature is not implemented
    EMustNotBeCalled,               ///< 1  This part of the code must not be called
    EIndexOutOfRange,               ///< 2  Invalid Index
    EAlreadyExists,                 ///< 3  the object already exists
    EInvalidState,                  ///< 4  the object's state is invalid

    EHContainer_NumClients =  100,  ///< 100  CHandleContainer - specific, problem with a number of clients
    EHContainer_DestroyingDirty,    ///< 101  CHandleContainer - specific, an attempt to destroy contained that still have clients

    EBat_DestroyingDirty = 200,     ///< 200  destroying dirty BAT cache
    EBat_InvalidBlockNumber,        ///< 201  invalid logical block number

    ESecMap_DestroyingDirty = 300,  ///< 300  destroying dirty Block/Sector mapping cache
    ESecMap_InvalidSectorNumber,    ///< 301  Invalid sector number in the block

    ESecPage_DestroyingDirty = 400, ///< 400  destroying Sector bitmap cache page
};

//-- used for abnormal termination in a few known cases, work in both DEBUG and RELEASE builds
#define Fault(arg) do {syslog(LOG_DEBUG, "libvhd2 Fault! code:%d, file:%s, line:%d", (arg), __FILE__, __LINE__ ); abort();} while(0);




//-----------------------------------------------------------------------------
//-- zero-filling functions
template <class T>
inline void FillZ(T& a)
    {memset((void*)&a, 0, sizeof(a));}

inline void FillZ(void* pBuf, size_t size)
    {memset(pBuf, 0, size);}


//-- Min/Max functions (not intended for large classes, passing arguments by value)
template <class T>
inline T Min(T a, T b)
    {return (a < b ? a : b);}

template <class T>
inline T Max(T a, T b)
    {return (a > b ? a : b);}



//-----------------------------------------------------------------------------
//template <class T>
//inline bool BoolXor(T a, T b)
//    { return (!a ^ !b);}



//-- various stuff

const unsigned int  K1KiloByteLog2 = 10;
const unsigned int  K1KiloByte = 1<<K1KiloByteLog2;
const unsigned int  K1MegaByte = 1<<20;

const unsigned int  KBitsInByteLog2 = 3;
const unsigned int  KBitsInByte = 1<<KBitsInByteLog2;


//-----------------------------------------------------------------------------
/*
    Indicates if a number passed in is a power of two.
    @return non-zero value if aVal is a power of 2, 0 otherwise
*/
inline uint32_t IsPowerOf2(uint32_t aVal);
inline uint32_t IsPowerOf2_64(uint64_t aVal);

//-----------------------------------------------------------------------------
/* Calculates the Log2(aVal) */
inline uint32_t Log2_inline(uint32_t aVal);
uint32_t Log2(uint32_t aVal); //-- not inlined



//-----------------------------------------------------------------------------
/*
    Rounds up aVal to the 2^aGranularityLog2
    For example: RoundUp(0x08, 2) == 0x08; RoundUp(0x08, 3) == 0x08; RoundUp(0x08, 4) == 0x10; RoundUp(0x19, 4) == 0x20

    @return rounded-up value
*/
inline uint32_t RoundUp_ToGranularity(uint32_t aVal, uint32_t aGranularityLog2);


bool CheckFill(const void* apBuf, uint32_t aNumBytes, uint8_t aFillByte);


//-----------------------------------------------------------------------------
//-- Extract low/high part of the given word type.
//-- these functions can be made more portable by using sizeof & CHAR_BIT
inline uint8_t  U16Low(uint32_t val)    {return (uint8_t)val;}
inline uint8_t  U16High(uint32_t val)   {return (uint8_t)(val >> 8) ;}

inline int8_t   I16Low(int32_t val)     {return (int8_t)val; }
inline int8_t   I16High(int32_t val)    {return (int8_t)(val >> 8);}

inline uint16_t U32Low(uint32_t val)    {return (uint16_t)val;}
inline uint16_t U32High(uint32_t val)   {return (uint16_t)(val >> 16) ;}

inline int16_t  I32Low(int32_t val)     {return (int16_t)val; }
inline int16_t  I32High(int32_t val)    {return (int16_t)(val >> 16);}

inline uint32_t U64Low(uint64_t val)    {return (uint32_t)val;}
inline uint32_t U64High(uint64_t val)   {return (uint32_t)(val >> 32);}

inline int32_t  I64Low(int64_t val)     {return (int32_t)val;}
inline int32_t  I64High(int64_t val)    {return (int32_t)(val >> 32);}


//-----------------------------------------------------------------------------
/** a list of supported UTF encodings */
enum TUtfEncoding
{
    EUTF_8,     ///< used for MacX
    EUTF_16,    ///< Seems to be used by TapDisk ???
    EUTF_16LE,  ///< LE, default for this platform
    EUTF_16BE   ///< BE, VHD files have data fields in BE
};

int UNICODE_to_ASCII(const void* apDataIn, size_t aNumBytesIn, void* apDataOut, size_t aOutBufSize, TUtfEncoding aEncoding);
int ASCII_to_UNICODE(const void* apDataIn, void* apDataOut, size_t aOutBufSize, size_t& aResultLen, TUtfEncoding aEncoding);



void VHD_Time_To_String(uint32_t aVhdTimeStamp, char *apBuf, size_t aBufLen);
uint32_t VHD_Time(time_t aTime = (time_t)-1);

//####################################################################
/**
    A very thin wrapper around std::vector, representing a dynamic resizeable buffer.
    Frees all previously allocated memory in its destructor
*/
class CDynBuffer
{
 public:
    inline  CDynBuffer(size_t aSize=0);
    inline ~CDynBuffer();

    inline size_t Size() const;
    inline void Resize(size_t aNewSize);

    inline void Fill(uint8_t aFill);
    inline void Fill(size_t aIndexFrom, size_t aNumBytes, uint8_t aFill);
    inline void FillZ();

    inline uint8_t* Ptr();
    inline const uint8_t* Ptr() const;

    inline void Copy(size_t aIndexFrom, size_t aNumBytes, const void* apSrc);

 public:
    /** use vector, it's contents is guaranteed to be contiguous, like a usual array.
        keep it public for the sake of vector API */
    vector<uint8_t> iBuffer;
};

//####################################################################
/**
    A very simple class providing "auto close" pointer behaviour.
    It works like a simple auto_ptr, but before deleting the object it refers to,
    T::Close() method is called.
    Thus, to use this functionality, the T class should contain Close() method.
*/
template<typename T>
class CAutoClosePtr
{
 public:

    explicit CAutoClosePtr(T* p = 0) throw() : iPtr(p) { }
    ~CAutoClosePtr() {DoDelete();}

    T& operator*()  const throw() {ASSERT(iPtr != 0); return *iPtr;}
    T* operator->() const throw() {ASSERT(iPtr != 0); return iPtr; }
    T* get() const throw() { return iPtr; } ///< @return a raw pointer
    T* release() throw()   { T* tmp = iPtr; iPtr = 0; return tmp;} ///< returns a pointer and sets itself to NULL

    /** see auto_ptr::reset()*/
    void reset(T* p = 0) throw() { if (p != iPtr) {DoDelete(); iPtr = p; } }

 private:
    CAutoClosePtr(const CAutoClosePtr&);            ///< outlawed, not supported
    CAutoClosePtr& operator= (const CAutoClosePtr&);///< outlawed, not supported

    void DoDelete() { if(iPtr) iPtr->Close(); delete iPtr; }

  private:
    T* iPtr;
};




//####################################################################
/**
    This class represents a bit vector i.e. an array of bits. Vector size can be from 1 to 2^32 bits.
    Note that all "boolean" values here are uint32_t type. "false" corresponds to value '0', "true" corresponds to any non-zero value
*/
class CBitVector
    {
 public:

    CBitVector(); //-- Creates an empty vector. see Create() methods for memory allocation
   ~CBitVector();

    void Close();
    int  Create(uint32_t aNumBits);
    void New(uint32_t aNumBits);


    //-- vector size related methods
    inline uint32_t Size() const;


    //-- single bit manipulation methods
    inline uint32_t operator[](uint32_t aIndex) const;
    inline void SetBit(uint32_t aIndex);
    inline void ResetBit(uint32_t aIndex);
    inline void InvertBit(uint32_t aIndex);
    inline void SetBitVal(uint32_t aIndex, uint32_t aVal);

    //-- bulk filling operations
    void Fill(uint32_t aVal);
    void Fill(uint32_t aIndexFrom, uint32_t aIndexTo, uint32_t aVal);

    bool IsFilledWith(uint32_t aVal) const;
    bool IsFilledWith(uint32_t aIndexFrom, uint32_t aIndexTo, uint32_t aVal) const;

    void  ImportData(uint32_t aStartBit, uint32_t aNumBits, const void* apData);
    void  ExportData(uint32_t aStartBit, uint32_t aNumBits, void* apBuf) const;

    //-- assignment
    CBitVector& operator=(const CBitVector& aRhs);

    //-- logical operations between 2 vectors.
    void And(const CBitVector& aRhs);
    void Or (const CBitVector& aRhs);
    void Xor(const CBitVector& aRhs);
    void Invert();

    //-- comparison
    bool operator==(const CBitVector& aRhs) const;
    bool operator!=(const CBitVector& aRhs) const;


    /** Bit search specifiers */
    enum TFindDirection
        {
        ELeft,      //< Search from the given position to the left (towards lower index)
        ERight,     //< Search from the given position to the right (towards higher index)
        ENearestL,  //< Search in both directions starting from the given position; in the case of the equal distances return the position to the left
        ENearestR   //< Search in both directions starting from the given position; in the case of the equal distances return the position to the right

        //-- N.B the current position the search starts with isn't included to the search.
        };

    bool Find(uint32_t& aStartPos, bool aBitVal, TFindDirection aDir) const;



    /** panic codes */
    enum TPanicCode
        {
        EIndexOutOfRange,       //< index out of range
        EWrondFindDirection,    //< a value doesn't belong to TFindDirection
        ESizeMismatch,          //< Size mismatch for binary operators
        ENotInitialised,        //< No memory allocated for the array
        ENotImplemented,        //< functionality isn't implemented

        EDataAlignment,         //< wrong data alignment when importing / exporting raw data
        };


 private:

    //-- these are outlawed.
    CBitVector(const CBitVector& aRhs);
    //-------------------------------------


    void Panic(TPanicCode aPanicCode) const __attribute__ ((noreturn));

    inline uint32_t WordNum(uint32_t aBitPos)  const;
    inline uint32_t BitInWord(uint32_t aBitPos) const;

    bool FindToRight(uint32_t& aStartPos, bool aBitVal) const;
    bool FindToLeft (uint32_t& aStartPos, bool aBitVal) const;


 private:

    int  DoCreate(uint32_t aNumBits);

    inline uint32_t MaskLastWord(uint32_t aVal) const;
    inline uint32_t ItrLeft(uint32_t& aIdx) const;
    inline uint32_t ItrRight(uint32_t& aIdx) const;

    inline uint32_t SizeInWords(uint32_t aSizeInBits) const;

 private:

    uint32_t   iNumBits; ///< number of bits in the vector
    uint32_t*  ipData;   ///< pointer to the data
    uint32_t   iNumWords;///< number of 32-bit words that store bits
    };


//####################################################################
/**
    A helper class that allows finding extents (consecutive values) of '1's or '0's in the CBitVector container.
    Uses quite efficient CBitVector::Find() operation.
*/
class TBitExtentFinder
{
 public:

    TBitExtentFinder(const CBitVector& aBitVector);
    TBitExtentFinder(const CBitVector& aBitVector, uint32_t aStartPos, uint32_t aMaxLen);

    bool FindExtent();

    /** initialise/reset finder state*/
    void Init(uint32_t aStartPos, uint32_t aMaxLen)
    {
        iState = EInit;
        iSeqLen  = 0;
        iCurrPos = aStartPos;
        iLastPos = iCurrPos + aMaxLen;

        if(iLastPos > iBitVector.Size())
            Fault(EIndexOutOfRange);


    }

    uint32_t ExtStartPos() const {ASSERT(iState!=EInit); return iCurrPos;} ///< @return bit number of the extent start in the aBitVector
    uint32_t ExtLen() const      {ASSERT(iState!=EInit); return iSeqLen; } ///< @return extent length (number of bits in it)
    bool     ExtBitVal() const   {ASSERT(iState!=EInit); return iVal; }    ///< @return extent bit value


 private:
    /** internal states */
    enum TState
    {
        EInit = 0,  ///< initial state before search
        EFound,     ///< a sub-sequence found
        EFinished   ///< search finished; reached the end of the vector
    };

 private:
    const CBitVector& iBitVector;
    TState      iState;   ///< object internal state
    uint32_t    iLastPos; ///< Last position in the vector to deal with
    uint32_t    iVal;     ///< bit value of the sequence we are looking for
    uint32_t    iCurrPos; ///< current position of the sequence start
    uint32_t    iSeqLen;  ///< length of the found sequence

};


#include "utils.inl"

#endif // __UTILS_H__

