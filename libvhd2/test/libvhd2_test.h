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
@file private header file for the libvhd2 test code
*/


#ifndef __LIBVHD2_TEST_H__
#define __LIBVHD2_TEST_H__

#include <stdint.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>

#include <string>
using std::string;

#include <vector>
using std::vector;

#include "../include/libvhd2.h"

//-----------------------------------------------------------------------------
//-- path to the directory where VHD files are created/deleted. Hardcoded for now
static const char KVhdFilesPath[] ="/home/dmitryl/Development/vhd_files/test/";

//-----------------------------------------------------------------------------

const unsigned int  K1KiloByteLog2 = 10;
const unsigned int  K1KiloByte = 1<<K1KiloByteLog2;
const unsigned int  K1MegaByte = 1<<20;

const uint32_t KDefSecSizeLog2 = 9;                 ///< Log2(default sector size)
const uint32_t KDefSecSize = 1 << KDefSecSizeLog2;  ///< default sector size
const uint32_t KDefSecPerBlockLog2 = 12;            ///< Log2(sectors per block) -> 2MB blocks
const uint32_t KDefSecPerBlock = 1<<KDefSecPerBlockLog2; ///< sectors per block -> 2MB blocks

//-- define compile-time assert
#define ASSERT_COMPILE(expr)    int __static_assert(int static_assert_failed[(expr)?1:-1])


//-- define logging macros
#define TEST_LOG(fmt, args...)	printf("%s(): "fmt,__func__, ##args); printf("\n");

//-----------------------------------------------------------------------------
//-- some macros to test various conditions

//-- tests if "arg" != 0. If it is, then aborts the program with log message
//#define test(arg) do {if(!(arg)) {syslog(LOG_DEBUG, "Test Fault! file:%s, line:%d",  __FILE__, __LINE__ ); abort();}} while(0);
#define test(arg) do {if(!(arg)) {printf("Test Failed! file:%s, line:%d\n",  __FILE__, __LINE__ ); abort();}} while(0);


//-- tests if "arg" == KErrNone (0). If it is not, then aborts the program with log message
//#define test_KErrNone(arg) do {if((arg)) {syslog(LOG_DEBUG, "Test Fault! res:%d, file:%s, line:%d", (arg), __FILE__, __LINE__ ); abort();}} while(0);
#define test_KErrNone(arg) do {if((arg)) {printf("Test Failed! res:%d, file:%s, line:%d\n", (arg), __FILE__, __LINE__ ); abort();}} while(0);

//-- tests if "arg1" == "arg2"  If it is not, then aborts the program with log message
//#define test_Val(arg1,arg2) do {if((arg1)!=(arg2)) {syslog(LOG_DEBUG, "Test Fault! val1:%d, val2:%d, file:%s, line:%d", (arg1),(arg2), __FILE__, __LINE__ ); abort();}} while(0);
#define test_Val(arg1,arg2) do {if((arg1)!=(arg2)) {printf("Test Failed! val1:%d, val2:%d, file:%s, line:%d\n", (arg1),(arg2), __FILE__, __LINE__ ); abort();}} while(0);

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


//--------------------------------------------------------------------
/**
    Helper class. Used for generating / verifying a pseudo-random sequence.
    Not very nice, because it is based on on rand()/srand() calls.
    In theory, needs to be a singleton class.
*/
class TRndSequenceGen
{
 public:
    TRndSequenceGen(uint aRndSeed)   {InitRndSeed(aRndSeed);}
    void InitRndSeed(uint aRndSeed);

    void GenerateSequence(void* apBuf, uint aBytes);
    int  CheckSequence(const void* apBuf, uint aBytes);
    void SkipSequence(uint aBytes);

 private:
    uint iSeed;

};

//--------------------------------------------------------------------
/** an ad-hoc test structure, describes a chunk of adjacent sectors */
struct TSectorsBase
{
    TSectorsBase() :iSecStart(0), iSecNum(0), iType(EInvalid)  {}

     enum TType
     {
        EInvalid,///< invalid type
        EFilled, ///< sectors filled with the test sequence
        EEmpty   ///< unpopulated sectors, must contain 0
     };

    uint iSecStart; ///< Starting sector of the chunk
    uint iSecNum;   ///< Number of filled sectors in the chunk.

    TType Type() const {return iType;}

 protected:
    TType iType;    ///< structure type

};



/** describes sectors chunk, filled with pseudo-random test sequence */
struct TFilledSectors : public TSectorsBase
{
    TFilledSectors() : TSectorsBase() {iType = EFilled ;}
};

/** describes sectors chunk, not filled with anything, must reads 0 */
struct TEmptySectors : public TSectorsBase
{
    TEmptySectors() : TSectorsBase() {iType = EEmpty ;}
};




//-----------------------------------------------------------------------------
//-- libvhd2 test utility functions.
bool CheckFilling(const void* apBuf, uint32_t aNumBytes, uint8_t aFillByte);

int LibVhd_2_CheckSectorsFill(TVhdHandle aVhdHandle, vector<TSectorsBase>& aSectorList, TRndSequenceGen& aSeqGen);
void LibVhd_2_CloseVhd(TVhdHandle& aVhdHandle);
void LibVhd_2_CreateVhd_Fixed(const char* aFileName, uint aSizeInSectors);
void LibVhd_2_CreateVhd_Dynamic(const char* aFileName, uint aSizeInSectors);
void LibVhd_2_CreateVhd_Diff(const char* aFileName, const char* aParentFileName);
void LibVhd_2_WriteTestSequence(TVhdHandle aVhdHandle, uint aStartSector, uint aNumSectors, TRndSequenceGen& aSeqGen);
int LibVhd_2_CheckTestSequence(TVhdHandle aVhdHandle, uint aStartSector, uint aNumSectors, TRndSequenceGen& aSeqGen);
int LibVhd_2_CheckFileFill(TVhdHandle aVhdHandle, uint aStartSector, uint aNumSectors, uint8_t aFill);
int LibVhd_2_FillFile(TVhdHandle aVhdHandle, uint aStartSector, uint aNumSectors, uint8_t aFill);

//-----------------------------------------------------------------------------

void InteropTest_Init();
void InteropTest_Vhd_Fixed();
void InteropTest_Vhd_Dynamic();
void InteropTest_Vhd_Diff();

void CoalesceTests_Init();
void CoalesceTests_Execute();

void TrimTests_Execute();


void Tests_Cleanup();


#endif //__LIBVHD2_TEST_H__

