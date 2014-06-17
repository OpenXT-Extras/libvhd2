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

    @file imlementation of varuous utilities
*/

#include <stdio.h>
#include <stdarg.h>
#include <stdexcept>

#include "vhd.h"


//--------------------------------------------------------------------

/**
    Convert UNICODE string to ASCII. See TUTfEncoding for supported UNICODE types.

    @param  apDataIn        pointer to the UNICODE string
    @param  aNumBytesIn     number of _bytes_ in the input UNICODE string
    @param  apDataOut       pointer to the output buffer where ASCII will be put
    @param  aOutBufSize     size of the output buffer. 1 byte will be taken for string null-terminating. should be >= 2
    @param  aEncoding       type of the input data encoding. @see TUTfEncoding

    @return 0 on success, negative value indicating some error.
*/
int UNICODE_to_ASCII(const void* apDataIn, size_t aNumBytesIn, void* apDataOut, size_t aOutBufSize, TUtfEncoding aEncoding)
{

    if(!aNumBytesIn || aOutBufSize < 2)
    {
        ASSERT(0);
        return KErrArgument;
    }

    const char* pEnc = NULL;

    switch(aEncoding)
    {
        case EUTF_8:    pEnc = "UTF-8";     break;
        case EUTF_16:   pEnc = "UTF-16";    break;
        case EUTF_16LE: pEnc = "UTF-16LE";  break;
        case EUTF_16BE: pEnc = "UTF-16BE";  break;

        default:
        ASSERT(0);
        return KErrNotSupported; //-- unsupported encoding
    };

    ASSERT(pEnc);

    iconv_t cd;
    cd = iconv_open("ASCII", pEnc);
    if(cd == (iconv_t)-1)
        return KErrGeneral;

    FillZ(apDataOut, aOutBufSize);

    size_t bytesIn  = aNumBytesIn;
    size_t bytesOut = aOutBufSize-1; //-- take into account trailing 0 in the output string

    //-- terrible hack!!! iconv() may want to change the input string for some reason.
    //-- !! the only way to do it properly is to make a copy of the input buffer and use it :(  @todo: think about it!
    char* pDataIn = (char*)apDataIn;

    char* pDataOut = (char*)apDataOut;

    const size_t convRes = iconv(cd, &pDataIn, &bytesIn, &pDataOut, &bytesOut);

    iconv_close(cd);

    if (convRes == (size_t)-1 || bytesIn)
        return KErrGeneral;

    return KErrNone;
}

//--------------------------------------------------------------------

/**
    Convert ASCII string to UNICODE. See TUTfEncoding for supported UNICODE types.

    @param  apDataIn        pointer to the null-terminated ASCII string
    @param  apDataOut       pointer to the output buffer where UNICODE data will be put
    @param  aOutBufSize     size of the output buffer in bytes.
    @param  aResultLen      out: number of resultant _bytes_ in the output UNICODE string
    @param  aEncoding       type of the input data encoding. @see TUTfEncoding

    @return 0 on success, negative value indicating some error.
*/
int ASCII_to_UNICODE(const void* apDataIn, void* apDataOut, size_t aOutBufSize, size_t& aResultLen, TUtfEncoding aEncoding)
{
    const char* pStrIn = (const char*)apDataIn;
    const size_t numBytesIn = strlen(pStrIn);

    if(!numBytesIn || aOutBufSize < 2)
    {
        ASSERT(0);
        return KErrArgument;
    }

    const char* pEnc = NULL;

    switch(aEncoding)
    {
        case EUTF_8:    pEnc = "UTF-8";     break;
        case EUTF_16:   pEnc = "UTF-16";    break;
        case EUTF_16LE: pEnc = "UTF-16LE";  break;
        case EUTF_16BE: pEnc = "UTF-16BE";  break;

        default:
        ASSERT(0);
        return KErrNotSupported; //-- unsupported encoding
    };

    ASSERT(pEnc);

    iconv_t cd;
    cd = iconv_open(pEnc, "ASCII");
    if(cd == (iconv_t)-1)
        return KErrGeneral;

    FillZ(apDataOut, aOutBufSize);

    size_t bytesIn  = numBytesIn;
    size_t bytesOut = aOutBufSize;

    //-- terrible hack!!! iconv() may want to change the input string for some reason.
    //-- !! the only way to do it properly is to make a copy of the input buffer and use it :(  @todo: think about it!
    char* pDataIn = (char*)pStrIn;

    char* pDataOut = (char*)apDataOut;

    const size_t convRes = iconv(cd, &pDataIn, &bytesIn, &pDataOut, &bytesOut);

    iconv_close(cd);

    if (convRes == (size_t)-1 || bytesIn)
        return KErrGeneral;

    aResultLen = aOutBufSize - bytesOut;

    return KErrNone;
}

//-----------------------------------------------------------------------------
/**
    Calculates the log2 of a number. Not inlined version

    @param aNum Number to calulate the log two of
    @return The log two of the number passed in
*/
uint32_t Log2(uint32_t aVal)
{
    return Log2_inline(aVal);
}


//-----------------------------------------------------------------------------
/**
    Check if a given buffer is filled with some byte.
    Uses a kind of binary search, dividing a buffer by halves and comparing them. This gives O(log2(N)) performance for the worst case.
    memcmp used here is supposed to be well-optimised to cope with various issues, like data alignment

    @param  apBuf       pointer to the buffer with data
    @param  aNumBytes   number of bytes to check
    @param  aFillByte   a fill pattren byte
    @return             true if whole buffer is filled with "aFillByte", false otherwise
*/
bool CheckFill(const void* apBuf, uint32_t aNumBytes, uint8_t aFillByte)
{
    if(!aNumBytes)
    {
        ASSERT(0);
        return false;
    }

    const uint8_t* pBuf = (const uint8_t*)apBuf;

    if(pBuf[0] != aFillByte)
        return false;

    uint32_t len = aNumBytes >> 1; //-- current length of the half-buffers to compare

    if(!len)
        return true; //-- 1 byte array, and we have already checked it

    if(pBuf[0] != pBuf[aNumBytes-1])
        return false;  //-- this check is for the array with odd number of bytes

    const uint8_t* const p1 = pBuf;    //-- points to the 1st half of the buffer

    for(;;)
    {
        const uint8_t* p2 = pBuf + len;//-- points to the 2nd half of the buffer

        if(len == 1)
            return (*p1 == *p2);  //-- 2 last bytes left to compare

        if(memcmp(p1, p2, len))
            return false;   //-- 2 sub-arrays differ

        ASSERT(len > 1);
        len >>= 1;

    }

    ASSERT(0);
}


//-----------------------------------------------------------------------------
/**
    Allows formatting and printing a string both to the debug log and into some buffer.
    in Release mode doesn't rint a string to the debug log

    @param  apStrOut if not NULL, them the string being logged will be _appended_ to there along with the /n character

    other parameters the same as for "printf"
*/
void StrLog(std::string* apStrOut, const char *format, ...)
{
    char buf[128];

    va_list argList;
    va_start(argList, format);
    vsnprintf(buf, sizeof(buf), format, argList);
    va_end(argList);

    if(apStrOut)
    {
        apStrOut->append(buf);
        apStrOut->append("\n");
    }

#ifdef _DEBUG
    syslog(LOG_DEBUG, " %s", buf);
#endif

}


//--------------------------------------------------------------------
/**
    Convert a VHD timestamp (seconds since Jan 1, 2000, 12:00:00) to string representation
    @param  aVhdTimeStamp   VHD timestamp
    @param  apBuf           out: formatted string
    @param  aBufLen         buffer length
*/
void VHD_Time_To_String(uint32_t aVhdTimeStamp, char *apBuf, size_t aBufLen)
{
    tm tm1;
    memset(&tm1, 0, sizeof(tm1));


	// VHD uses an epoch of 12:00AM, Jan 1, 2000.
	// Need to adjust this to the expected epoch of 1970.
	tm1.tm_year  = 100;
	tm1.tm_mon   = 0;
	tm1.tm_mday  = 1;

	const time_t t1 = mktime(&tm1);
    const time_t t2 = t1 + (time_t)aVhdTimeStamp;

    gmtime_r(&t2, &tm1);

    snprintf(apBuf, aBufLen, "%02d.%02d.%04d %d:%d:%d", tm1.tm_mday, tm1.tm_mon, tm1.tm_year+1900, tm1.tm_hour, tm1.tm_min, tm1.tm_sec);
}


//--------------------------------------------------------------------
/**
    Produces VHD timestamp (seconds since Jan 1, 2000, 12:00:00) from some time_t value.
    @param      aTime time. If not specified, current time will be used
    @return     VHD timestamp
*/
uint32_t VHD_Time(time_t aTime /*=(time_t)-1*/)
{
    if(aTime ==(time_t)-1)
    {//-- current  time requested
        aTime = time(NULL);
    }

	struct tm tm;
	time_t micro_epoch;
	memset(&tm, 0, sizeof(struct tm));

	tm.tm_year   = 100;
	tm.tm_mon    = 0;
	tm.tm_mday   = 1;
	micro_epoch  = mktime(&tm);

	return (uint32_t)(aTime - micro_epoch);
}

//####################################################################
//# class CBitVector implementation
//####################################################################

const uint32_t K_FFFF = 0xFFFFFFFF; //-- all one bits, beware rigth shifts of signed integers!

CBitVector::CBitVector()
          :iNumBits(0), ipData(NULL), iNumWords(0)
{
}

CBitVector::~CBitVector()
{
    Close();
}


//--------------------------------------------------------------------
/** explicitly closes the object and deallocates memory */
void CBitVector::Close()
{
    iNumBits  = 0;
    iNumWords = 0;

    free(ipData);
    ipData = NULL;
}

//--------------------------------------------------------------------
/**
    Panics.
    @param aPanicCode   a panic code
*/
void CBitVector::Panic(TPanicCode aPanicCode) const
{
    DBG_LOG("CBitVector::Panic(%d)", aPanicCode);
    Fault(aPanicCode);
}

//--------------------------------------------------------------------
/**
    Create the vector with the size of aNumBits bits. This method doesn't throw any exceptions on allocation failure

    @param  aNumBits    number of bits in the vector
    @return system-wide error codes:
        KErrNoMemory    unable to allocate sufficient amount of memory for the array
        KErrInUse       an attempt to call Create() for non-empty vector. Close it first.
        KErrArgument    invalid aNumBits value == 0
*/
int CBitVector::Create(uint32_t aNumBits)
{

    if(ipData)
        return KErrInUse; //-- array is already in use. Close it first.

    if(!aNumBits)
        return KErrArgument;

    return DoCreate(aNumBits);
}


int  CBitVector::DoCreate(uint32_t aNumBits)
{
    ASSERT(!iNumBits && !ipData && !iNumWords);

    //-- memory is allocated by word (32 bit) quiantities
    const uint32_t numWords = SizeInWords(aNumBits) ;
    const uint32_t numBytes = numWords << 2;

    ipData = (uint32_t*)malloc(numBytes);
    if(!ipData)
        return KErrNoMemory;

    FillZ(ipData, numBytes);

    iNumBits  = aNumBits;
    iNumWords = numWords;

    return KErrNone;
}





//--------------------------------------------------------------------
/**
    The same as CBitVector::Create(), but throws exceptions instead of returning a error code
*/
void CBitVector::New(uint32_t aNumBits)
{
    if(ipData)
        throw std::runtime_error("CBitVector::New() already created"); //-- array is already in use. Close it first.

    if(!aNumBits)
        throw std::runtime_error("CBitVector::New() invalid parameters");


    if(DoCreate(aNumBits) != KErrNone)
        throw std::bad_alloc();
}


//--------------------------------------------------------------------
/**
    @return number of 32-bit words enough to contain aSizeInBits bits
*/
inline uint32_t CBitVector::SizeInWords(uint32_t aSizeInBits) const
{
    ASSERT(aSizeInBits);
    const uint32_t numWords = ((aSizeInBits-1) >> 5) + 1 ;
    return numWords;
}


//--------------------------------------------------------------------
/**
    Fill a bit vector with a given bit value
    @param aVal a bit value
*/
void CBitVector::Fill(uint32_t aVal)
{
    if(!ipData)
        Panic(ENotInitialised);

    memset(ipData, (aVal ? 0xFF : 0x00), iNumWords << 2);
}


//--------------------------------------------------------------------
/**
    Copy data from the rhs vector. The vectrors must have the same size.
    @param aRhs right-hyadd side object
*/
CBitVector& CBitVector::operator=(const CBitVector& aRhs)
{
    if(this == &aRhs)
    {//-- assigning to itself, potential source of errors
        ASSERT(0);
        return *this;
    }

    if(Size() != aRhs.Size())
        Panic(ESizeMismatch);

    if(Size())
    {
        ASSERT(ipData && iNumWords && iNumBits);
        memcpy(ipData, aRhs.ipData, iNumWords << 2);
    }

    return *this;
}


//--------------------------------------------------------------------
/**
    Fill a range from bit number "aIndexFrom" to "aIndexTo" inclusively with the value of aVal

    @param  aIndexFrom  start bit number (inclusive)
    @param  aIndexTo    end bit number (inclusive)
    @param  aVal        the value to be used to fill the range (0s or 1s)
*/
void CBitVector::Fill(uint32_t aIndexFrom, uint32_t aIndexTo, uint32_t aVal)
    {
    if(!ipData)
        Panic(ENotInitialised);

    if(aIndexFrom == aIndexTo)
    {
        SetBitVal(aIndexFrom, aVal);
        return;
    }

    //-- swap indexes if they are not in order
    if(aIndexFrom > aIndexTo)
    {
        const uint32_t tmp = aIndexFrom;
        aIndexFrom = aIndexTo;
        aIndexTo = tmp;
    }

    if((aIndexFrom >= iNumBits) || (aIndexTo >= iNumBits))
        Panic(EIndexOutOfRange);

    const uint32_t wordStart = WordNum(aIndexFrom);
    const uint32_t wordTo    = WordNum(aIndexTo);

    if(aVal)
    {//-- filling a range with '1'

        uint32_t shift = BitInWord(aIndexFrom);
        const uint32_t mask1 = (K_FFFF >> shift) << shift;

        uint32_t mask2 = K_FFFF;
        shift = 1+BitInWord(aIndexTo);
        if(shift < 32)
        {
            mask2 = ~((mask2 >> shift) << shift);
        }

        if(wordTo == wordStart)
        {//-- a special case, filling is in the same word
            ipData[wordStart] |= (mask1 & mask2);
        }
        else
        {
            ipData[wordStart] |= mask1;
            ipData[wordTo]    |= mask2;

            const uint32_t wholeWordsBetween = wordTo - wordStart - 1; //-- whole words that can be bulk filled

            if(wholeWordsBetween)
                memset(ipData+wordStart+1, 0xFF, wholeWordsBetween << 2);

        }
    }
    else
    {//-- filling a range with '0'

        uint32_t shift = BitInWord(aIndexFrom);
        const uint32_t mask1 = ~((K_FFFF >> shift) << shift);

        uint32_t mask2 = 0;
        shift = 1+BitInWord(aIndexTo);
        if(shift < 32)
        {
            mask2 = ((K_FFFF >> shift) << shift);
        }

        if(wordTo == wordStart)
        {//-- a special case, filling is in the same word
            ipData[wordStart] &= (mask1 | mask2);
        }
        else
        {
            ipData[wordStart] &= mask1;
            ipData[wordTo]    &= mask2;

            const uint32_t wholeWordsBetween = wordTo - wordStart - 1; //-- whole words that can be bulk filled

            if(wholeWordsBetween)
                memset(ipData+wordStart+1, 0x00, wholeWordsBetween << 2);

        }
    }

}

//--------------------------------------------------------------------
/**
    Check if the whole vector is completely filled with '0' or '1's. This is much faster than using Num1Bits() etc.

    @param  aVal  the value to be used to check the vector filling (0s or 1s)
    @return true if whole vector is filled with 'aVal'
*/
bool CBitVector::IsFilledWith(uint32_t aVal) const
{
    if(!ipData)
        Panic(ENotInitialised);

    const uint32_t val = aVal ? K_FFFF : 0;

    //-- 1. check whole words filling
    for(uint32_t i=0; i<iNumWords-1; ++i)
    {
        if(ipData[i] != val)
            return false;
    }

    //-- 2. check the last word filling
    uint32_t lastWord = MaskLastWord(ipData[iNumWords-1]);

    if(aVal)
    {//-- all meaningful bits in the last word must be 1
        const uint32_t mask = ~MaskLastWord(K_FFFF);
        lastWord |= mask;
    }

    return (lastWord == val);
}


//--------------------------------------------------------------------
/**

    Check if the subvectorvector is filled with '0' or '1's. This is much faster than using Num1Bits() etc.

    @todo make it better, deal with whole words!!

    @param  aIndexFrom  start bit number (inclusive)
    @param  aIndexTo    end bit number (inclusive)
    @param  aVal  the value to be used to check the vector filling (0s or 1s)

    @return true if whole vector is filled with 'aVal'
*/
bool CBitVector::IsFilledWith(uint32_t aIndexFrom, uint32_t aIndexTo, uint32_t aVal) const
{
    if(!ipData)
        Panic(ENotInitialised);

    if(aIndexFrom == aIndexTo)
    {
        return operator[](aIndexFrom);
    }

    //-- swap indexes if they are not in order
    if(aIndexFrom > aIndexTo)
    {
        const uint32_t tmp = aIndexFrom;
        aIndexFrom = aIndexTo;
        aIndexTo = tmp;
    }

    if((aIndexFrom >= iNumBits) || (aIndexTo >= iNumBits))
        Panic(EIndexOutOfRange);

    const uint32_t wordStart = WordNum(aIndexFrom);
    const uint32_t wordTo    = WordNum(aIndexTo);

    uint32_t shift = BitInWord(aIndexFrom);
    uint32_t mask1 = (K_FFFF >> shift) << shift; //-- has '1' bits for the bits we are interested in wordStart

    uint32_t mask2 = K_FFFF;    //-- has '1' bits for the bits we are interested in wordTo
    shift = 1+BitInWord(aIndexTo);
    if(shift < 32)
    {
        mask2 = ~((mask2 >> shift) << shift);
    }

    if(aVal)
    {//-- check that the range contains all '1' bits
        if(wordTo == wordStart)
        {//-- a special case, checking bits in the same word
            const uint32_t mask = mask1 & mask2;
            return ((ipData[wordStart] & mask) == mask);
        }
        else
        {
            if((ipData[wordStart] & mask1) != mask1)
                return false;

            if((ipData[wordTo] & mask2) != mask2)
                return false;

            for(uint32_t i=wordStart+1; i<wordTo; ++i)
            {
                if(ipData[i] != K_FFFF)
                    return false;
            }

            return true;
        }
    }
    else //if(aVal)
    {//-- check that the range contains all '0' bits

        if(wordTo == wordStart)
        {//-- a special case, checking bits in the same word
            const uint32_t mask = mask1 & mask2;
            return ((ipData[wordStart] & mask) == 0);
        }
        else
        {
            if((ipData[wordStart] & mask1) != 0)
                return false;

            if((ipData[wordTo] & mask2) != 0)
                return false;

            for(uint32_t i=wordStart+1; i<wordTo; ++i)
            {
                if(ipData[i] != 0)
                    return false;
            }

            return true;
        }
    }
}

//--------------------------------------------------------------------
/**
    Import data to the internal bit vector representation.
    Just replaces number of bytes from apData to the ipData.

    @param aStartBit starting bit number. Must have 8-bit alignment.
    @param aNumBits  number of bits to import; granularity: 1 bit, i.e. it can be 177, for example.
    @param apData    pointer to the data (bitstream) to import.
*/
void  CBitVector::ImportData(uint32_t aStartBit, uint32_t aNumBits, const void* apData)
{
    ASSERT(aNumBits);

    if(!ipData)
        Panic(ENotInitialised);

    //-- check parameters granularity. aStartBit must have 8-bit alignment
    if(aStartBit & 0x07)
        Panic(EDataAlignment);

    if(!iNumWords || (aStartBit + aNumBits > iNumBits))
        Panic(EIndexOutOfRange);

    const uint32_t bitsTail = aNumBits & 0x07;
    const uint32_t nBytes = aNumBits >> KBitsInByteLog2;

    if(nBytes)
    {//-- copy full array of bytes
        const uint32_t startByte = aStartBit >> KBitsInByteLog2;
        memcpy(((uint8_t*)ipData) + startByte, apData, nBytes);
    }

    if(bitsTail)
    {//-- we need to copy trailing bits from the input data to the corresponding byte of the internal array
        const uint8_t mask   = (uint8_t)(0xFF >> (8-bitsTail));
        const uint8_t orMask = (uint8_t)( *((const uint8_t*)apData + nBytes) & mask);
        const uint8_t andMask= (uint8_t)~mask;

        uint8_t* pbData = (uint8_t*)ipData + nBytes;
        *pbData &= andMask;
        *pbData |= orMask;
    }

}

//--------------------------------------------------------------------
/**
    Import data to the internal bit vector representation.
    Just replaces number of bytes from apData to the ipData.

    @param aStartBit starting bit number. Must have 8-bit alignment.
    @param aNumBits  number of bits to export, must comprise the whole byte, i.e. be multiple of 8.
                     The client is responsible for masking extra bits it doesn't need.
                     Another implication: e.g. if the bitvector consists of 3 bits, this value must be 8.
                     The value of bits 3-7 in the aData[0] will be undefined.

    @param apBuf     pointer to the buffer where data are going.
*/
void  CBitVector::ExportData(uint32_t aStartBit, uint32_t aNumBits, void* apBuf) const
{
    ASSERT(aNumBits);

    if(!ipData)
        Panic(ENotInitialised);

    //-- check parameters granularity. aStartBit and number of bits must have 8-bit alignment
    if((aStartBit & 0x07) || ((aNumBits & 0x07)))
        Panic(EDataAlignment);

    if(!iNumWords || (aStartBit+aNumBits > (iNumWords << (KBitsInByteLog2+sizeof(uint32_t)))))
        Panic(EIndexOutOfRange);

    const uint32_t nBytes = aNumBits >> KBitsInByteLog2;
    const uint32_t startByte = aStartBit >> KBitsInByteLog2;

    memcpy(apBuf, ((const uint8_t*)ipData) + startByte, nBytes);
}

//--------------------------------------------------------------------

/**
    Comparison operator.
    @param  aRhs a vector to compate with.
    @panic ESizeMismatch in the case of different vector sizes
*/
bool CBitVector::operator==(const CBitVector& aRhs) const
{
    if(!ipData)
        Panic(ENotInitialised);

    if(iNumBits != aRhs.iNumBits)
        Panic(ESizeMismatch);

    if(!iNumBits)
        return true; //-- comparing 0-lenght arrays

    if(this == &aRhs)
    {//-- comparing with itself, potential source of errors
        ASSERT(0);
        return true;
    }

    if(iNumWords >= 1)
    {
        const uint cntBytes = (iNumBits >> 5) << 2; //-- bytes to compare
        if(memcmp(ipData, aRhs.ipData, cntBytes))
            return false;
    }

    const uint bitsRest  = iNumBits & 0x1F;
    if(bitsRest)
    {
        const uint32_t mask = K_FFFF >> (32-bitsRest);
        return ( (ipData[iNumWords-1] & mask) == (aRhs.ipData[iNumWords-1] & mask) );
    }

    return true;
}

bool CBitVector::operator!=(const CBitVector& aRhs) const
{
    return ! ((*this) == aRhs);
}



//--------------------------------------------------------------------

/** Invert all bits in a bit vector */
void CBitVector::Invert()
{
    if(!ipData)
        Panic(ENotInitialised);

    for(uint i=0; i<iNumWords; ++i)
        ipData[i] ^= K_FFFF;
}


//--------------------------------------------------------------------

/**
    Perform "And" operation between 2 vectors. They shall be the same size.
    @param  aRhs a vector from the right hand side
    @panic ESizeMismatch in the case of different vector sizes
*/
void CBitVector::And(const CBitVector& aRhs)
{
    if(!ipData)
        Panic(ENotInitialised);

    if(iNumBits != aRhs.iNumBits)
        Panic(ESizeMismatch);

    for(uint i=0; i<iNumWords; ++i)
        ipData[i] &= aRhs.ipData[i];
}

//--------------------------------------------------------------------

/**
    Perform "Or" operation between 2 vectors. They shall be the same size.
    @param  aRhs a vector from the right hand side
    @panic ESizeMismatch in the case of different vector sizes
*/
void CBitVector::Or(const CBitVector& aRhs)
{
    if(!ipData)
        Panic(ENotInitialised);

    if(iNumBits != aRhs.iNumBits)
        Panic(ESizeMismatch);

    for(uint i=0; i<iNumWords; ++i)
        ipData[i] |= aRhs.ipData[i];
}

//--------------------------------------------------------------------

/**
    Perform "XOR" operation between 2 vectors. They shall be the same size.
    @param  aRhs a vector from the right hand side
    @panic ESizeMismatch in the case of different vector sizes
*/
void CBitVector::Xor(const CBitVector& aRhs)
{
    if(!ipData)
        Panic(ENotInitialised);

    if(iNumBits != aRhs.iNumBits)
        Panic(ESizeMismatch);

    for(uint i=0; i<iNumWords; ++i)
        ipData[i] ^= aRhs.ipData[i];
}


//--------------------------------------------------------------------


/**
    Search for a specified bit value ('0' or '1') in the vector from the given position.
    @param  aStartPos   zero-based index; from this position the search will start. This position isn't included to the search.
                        On return may contain a new position if the specified bit is found in specified direction.
    @param  aBitVal     zero or non-zero bit to search.
    @param  aDir        Specifies the search direction

    @return true if the specified bit value is found; aStartPos gets updated.
            false otherwise.

*/
bool CBitVector::Find(uint32_t& aStartPos, bool aBitVal, TFindDirection aDir) const
    {
    if(aStartPos >= iNumBits)
        Panic(EIndexOutOfRange);

    ASSERT(iNumWords && ipData);

    switch(aDir)
        {
        case ERight:    //-- Search from the given position to the right
            return FindToRight(aStartPos, aBitVal);

        case ELeft:     //-- Search from the given position to the left (towards lower index)
            return FindToLeft(aStartPos, aBitVal);

        case ENearestL: //-- Search for the nearest value in both directions starting from left
            Panic(ENotImplemented);

        case ENearestR: //-- Search for the nearest value in both directions starting from right
            Panic(ENotImplemented);

        default:
            break;
        };

    Panic(EWrondFindDirection);
    return false;
    }


//--------------------------------------------------------------------
/**
    Internal method to look for a given bit value in the right direction.
    see bool CBitVector::Find(...)
*/

bool CBitVector::FindToRight(uint32_t& aStartPos, bool aBitVal) const
{
    if(aStartPos >= iNumBits-1)
        return false; //-- no way to the right

    const uint32_t startPos = aStartPos+1;
    const uint32_t fInvert = aBitVal ? 0 : K_FFFF; //-- invert everything if we are looking for '0' bit

    uint32_t wordNum = WordNum(startPos);
    uint32_t val = ipData[wordNum] ^ fInvert;

    if(wordNum == iNumWords-1)
    {//-- process the last word in the array, some higher bits might not belong to the bit vector
        val = MaskLastWord(val);
    }

    const uint32_t shift = BitInWord(startPos);
    val = (val >> shift) << shift; //-- mask unused low bits

    if(val)
    {//-- there are '1' bits in the current word
        goto found;
    }
    else
    {//-- search in higher words
        wordNum++;

        while(iNumWords-wordNum > 1)
        {
            val = ipData[wordNum] ^ fInvert;
            if(val)
                goto found;

            wordNum++;
        }

        if(wordNum == iNumWords-1)
        {//-- process the last word in the array, some higher bith might not belong to the bit vector
            val = ipData[wordNum] ^ fInvert;
            val = MaskLastWord(val);

            if(val)
                goto found;
        }
    }

    return false; //-- haven't found anything

  found:

    val &= (~val+1); //-- select rightmost bit
    aStartPos = (wordNum << 5)+Log2(val);
    return true;
}


//--------------------------------------------------------------------

/**
    Internal method to look for a given bit value in the left direction.
    see bool CBitVector::Find(...)
*/

bool CBitVector::FindToLeft (uint32_t& aStartPos, bool aBitVal) const
{
    if(!aStartPos)
        return false; //-- no way to the left

    const uint32_t startPos=aStartPos-1;
    const uint32_t fInvert = aBitVal ? 0 : K_FFFF; //-- invert everything if we are looking for '0' bit

    uint32_t wordNum = WordNum(startPos);
    uint32_t val = ipData[wordNum] ^ fInvert;

    const uint32_t shift = 31-(BitInWord(startPos));
    val = (val << shift) >> shift; //-- mask unused high bits

    if(val)
    {//-- there are '1' bits in the current word
        goto found;
    }
    else
    {//-- search in the lower words
        while(wordNum)
        {
            wordNum--;
            val=ipData[wordNum] ^ fInvert;
            if(val)
                goto found;
        }
    }

    return false; //-- nothing found

 found:
    aStartPos = (wordNum << 5)+Log2(val);
    return true;
}

//####################################################################
//# class TBitExtentFinder implementation
//####################################################################


/**
    Constructor.
    @param  aBitVector  reference to the bit vector object we are looking in
*/
TBitExtentFinder::TBitExtentFinder(const CBitVector& aBitVector)
                :iBitVector(aBitVector)
{
    Init(0, iBitVector.Size());
}


/**
    Constructor.
    @param  aBitVector  reference to the bit vector object we are looking in
    @param  aStartPos   starting position in the vector
    @param  aMaxLen     max. number of bits to process. (aStartPos+aMaxLen) must be <= aBitVector.Size()
*/
TBitExtentFinder::TBitExtentFinder(const CBitVector& aBitVector,uint32_t aStartPos, uint32_t aMaxLen)
                 :iBitVector(aBitVector)
{
    Init(aStartPos, aMaxLen);
}

//--------------------------------------------------------------------
/**
    Find the next extent in the vector

    @return true    if the extent is found.  Use ExtStartPos(), ExtLen(), ExtBitVal() to get the extent parameters
            false   when no more extents can be found
*/
bool TBitExtentFinder::FindExtent()
{
    if(iState == EFinished)
        return false;

    uint32_t pos = iCurrPos + iSeqLen;

    ASSERT(pos < iLastPos);

    iVal = iBitVector[pos];
    iCurrPos = pos;

    if(iBitVector.Find(pos, !iVal, CBitVector::ERight))
    {
        iSeqLen = pos - iCurrPos;
        iState = EFound;
    }
    else
    {
        iSeqLen = iBitVector.Size() - iCurrPos;
        iState = EFinished;
    }

    if(iCurrPos + iSeqLen >= iLastPos)
    {
        iSeqLen = iLastPos - iCurrPos;
        iState = EFinished;
    }

    return true;
}




