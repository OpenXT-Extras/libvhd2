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
    @file some useful utilities inline functions implementation
*/


#ifndef __UTILS_INL__
#define __UTILS_INL__


//-----------------------------------------------------------------------------
/**
    Indicates if a number passed in is a power of two.
    @return non-zero value if aVal is a power of 2, 0 otherwise
*/
inline uint32_t IsPowerOf2(uint32_t aVal)
{
    if (!aVal)
    {
        ASSERT(0);
        return 0;
    }

    return !(aVal & (aVal-1));
}

inline uint32_t IsPowerOf2_64(uint64_t aVal)
{
    if (!aVal)
    {
        ASSERT(0);
        return 0;
    }

    return !(aVal & (aVal-1));
}

//-----------------------------------------------------------------------------
/**
    Calculates the log2 of a number
    This is the explicitly inlined version. Extensive using it may result in a code bloat.

    @param aNum Number to calulate the log two of
    @return The log two of the number passed in
*/
inline uint32_t Log2_inline(uint32_t aVal)
{
    ASSERT_COMPILE(sizeof(uint32_t) == 4);
    ASSERT(aVal);

    uint32_t bitPos=31;

    if(!(aVal >> 16)) {bitPos-=16; aVal<<=16;}
    if(!(aVal >> 24)) {bitPos-=8;  aVal<<=8 ;}
    if(!(aVal >> 28)) {bitPos-=4;  aVal<<=4 ;}
    if(!(aVal >> 30)) {bitPos-=2;  aVal<<=2 ;}
    if(!(aVal >> 31)) {bitPos-=1;}

    return bitPos;
}

//-----------------------------------------------------------------------------
/**
    Rounds up aVal to the 2^aGranularityLog2
    For example: RoundUp(0x08, 2) == 0x08; RoundUp(0x08, 3) == 0x08; RoundUp(0x08, 4) == 0x10; RoundUp(0x19, 4) == 0x20

    @return rounded-up value
*/
inline uint32_t RoundUp_ToGranularity(uint32_t aVal, uint32_t aGranularityLog2)
    {
    ASSERT(aGranularityLog2 < 32);

    if( (aVal & ((1<<aGranularityLog2)-1)) == 0)
        return aVal;

    aVal >>= aGranularityLog2;
    aVal++;
    aVal <<= aGranularityLog2;

    return aVal;
    }


//####################################################################
//# class CDynBuffer implementation
//####################################################################


CDynBuffer::CDynBuffer(size_t aSize/*=0*/)
{
    Resize(aSize);
}

CDynBuffer::~CDynBuffer()
{
}

/**
    change the buffer size
    @param aNewSize new size.
*/
void CDynBuffer::Resize(size_t aNewSize)
{
    iBuffer.reserve(aNewSize);
    iBuffer.resize(aNewSize);
}

/**
    Fill whole buffer with some byte
    @param aFill byte to fill
*/
void CDynBuffer::Fill(uint8_t aFill)
{
    memset(Ptr(), aFill, Size());
}

/**
    Fills a part of the buffer with the given byte
    @param  aIndexFrom  starting index
    @param  aNumBytes   number of bytes to fill
    @param  aFill       byte to fill
*/
void CDynBuffer::Fill(size_t aIndexFrom, size_t aNumBytes, uint8_t aFill)
{
    if(aIndexFrom >= Size() || (aIndexFrom + aNumBytes) > Size())
    {
        ASSERT(0);
        iBuffer.at(aIndexFrom + aNumBytes); //-- will throw a proper exception
    }

    memset(Ptr()+aIndexFrom, aFill, aNumBytes);
}


/**
    Copy some data into the buffer.
    @param  aIndexFrom  starting index
    @param  aNumBytes   number of bytes to copy
    @param  apSrc       pointer to source data
*/
void CDynBuffer::Copy(size_t aIndexFrom, size_t aNumBytes, const void* apSrc)
{
    if(aIndexFrom >= Size() || (aIndexFrom + aNumBytes) > Size())
    {
        ASSERT(0);
        iBuffer.at(aIndexFrom + aNumBytes); //-- will throw a proper exception
    }

    memcpy(Ptr()+aIndexFrom, apSrc, aNumBytes);
}

/**
    Zero-fill whole buffer
*/
void CDynBuffer::FillZ()
{
    Fill(0);
}

/** @return size of the buffer in bytes */
size_t CDynBuffer::Size() const
{
    return iBuffer.size();
}

/**
    Get a raw pointer to the beginning of the buffer. Size of the buffer can be obtained with Size()
    Very dangerous, try not to go outside the buffer.
*/
uint8_t* CDynBuffer::Ptr()
{
    return &iBuffer[0];
}

const uint8_t* CDynBuffer::Ptr() const
{
    return &iBuffer[0];
}




//####################################################################
//# class CBitVector implementation
//####################################################################

/** @return size of the vector (number of bits) */
inline uint32_t CBitVector::Size() const
    {
    return iNumBits;
    }

/**
    Get a bit by index

    @param aIndex  index in a bit vector
    @return 0 if the bit at pos aIndex is 0, not zero otherwise
    @panic EIndexOutOfRange if aIndex is out of range
*/
inline uint32_t CBitVector::operator[](uint32_t aIndex) const
    {
    if(aIndex >= iNumBits)
        Panic(EIndexOutOfRange);

    return (ipData[WordNum(aIndex)] & (1<<BitInWord(aIndex)));
    }

/**
    Set a bit at pos aIndex to '1'
    @param aIndex  index in a bit vector
    @panic EIndexOutOfRange if aIndex is out of range
*/
inline void CBitVector::SetBit(uint32_t aIndex)
    {
    if(aIndex >= iNumBits)
        Panic(EIndexOutOfRange);

    ipData[WordNum(aIndex)] |= (1<<BitInWord(aIndex));
    }

/**
    Set a bit at pos aIndex to '0'
    @param aIndex  index in a bit vector
    @panic EIndexOutOfRange if aIndex is out of range
*/
inline void CBitVector::ResetBit(uint32_t aIndex)
    {
    if(aIndex >= iNumBits)
        Panic(EIndexOutOfRange);

    ipData[WordNum(aIndex)] &= ~(1<<BitInWord(aIndex));
    }

/**
    Invert a bit at pos aIndex
    @param aIndex  index in a bit vector
    @panic EIndexOutOfRange if aIndex is out of range
*/
inline void CBitVector::InvertBit(uint32_t aIndex)
    {
    if(aIndex >= iNumBits)
        Panic(EIndexOutOfRange);

    ipData[WordNum(aIndex)] ^= (1<<BitInWord(aIndex));
    }

/**
    Set bit value at position aIndex
    @param aIndex  index in a bit vector
    @panic EIndexOutOfRange if aIndex is out of range
*/
inline void CBitVector::SetBitVal(uint32_t aIndex, uint32_t aVal)
    {
    if(aVal)
        SetBit(aIndex);
    else
        ResetBit(aIndex);
    }


inline uint32_t CBitVector::MaskLastWord(uint32_t aVal) const
{
    const uint32_t shift = (32-(iNumBits & 0x1F)) & 0x1F;
    return (aVal << shift) >> shift; //-- mask unused high bits
}

inline uint32_t CBitVector::WordNum(uint32_t aBitPos)  const
{
    return aBitPos >> 5;
}

inline uint32_t CBitVector::BitInWord(uint32_t aBitPos) const
{
    return aBitPos & 0x1F;
}







#endif // __UTILS_INL__

