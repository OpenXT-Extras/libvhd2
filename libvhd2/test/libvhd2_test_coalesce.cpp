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
    @file  test coalescing VHD chains
*/


#include <unistd.h>
#include <stdio.h>

#include <assert.h>
#include <string.h>

#include <string>
using std::string;

#include "libvhd2_test.h"

//--------------------------------------------------------------------
static const uint KRndSeed1 = 0xdeadbeef;
static const uint KRndSeed2 = 0xdeadbaaa;
static const uint KRndSeed3 = 0xfacebeef;
static const uint KRndSeed4 = 0x0acedead;

static string strFileName_Head(KVhdFilesPath);  //-- Head file name    [3] in the chain
static string strFileName_Diff1(KVhdFilesPath); //-- Diff1 file name   [2] in the chain
static string strFileName_Diff2(KVhdFilesPath); //-- Diff2 file name   [1] in the chain
static string strFileName_Tail(KVhdFilesPath);  //-- Tail file name    [0] in the chain

//-- requested file size in sectors, can be slightly different after creation because CHS calculations
const uint KReqFileSize_Sectors = (16*K1MegaByte)>>KDefSecSizeLog2;

//--------------------------------------------------------------------
/**
    Check contents of the VHD chain starting from "Head"
    @param  aFileName file name
    @return KErrNone if check passed OK and the test sequence in the file and all its predecessors matched the pattern
*/
static int CheckContents_Head(const char* aFileName)
{
    TEST_LOG("aFileName:%s", aFileName);

    TVhdHandle hVhd;
    TVHD_ParamsStruct  vhdParams;
    int nRes;

    TRndSequenceGen seqGen1(KRndSeed1);

    //-- "Head" file, 1 file chain
    seqGen1.InitRndSeed(KRndSeed1);
    hVhd = VHD_Open(strFileName_Head.c_str(), VHDF_OPEN_RDONLY|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    nRes = VHD_Info(hVhd, &vhdParams);
    test_KErrNone(nRes);


    const uint KFileSizeSectors = vhdParams.vhdSectors; //-- real size in sectors

    //-- check empty space
    nRes = LibVhd_2_CheckFileFill(hVhd, 0, (4*KDefSecPerBlock)-4, 0);
    if(nRes != KErrNone)
        return nRes;

    nRes = LibVhd_2_CheckFileFill(hVhd, (4*KDefSecPerBlock)+4, (KDefSecPerBlock-4) + 2*KDefSecPerBlock, 0);
    if(nRes != KErrNone)
        return nRes;

    //-- check test sequence
    nRes = LibVhd_2_CheckTestSequence(hVhd, KFileSizeSectors-8, 8, seqGen1);
    if(nRes != KErrNone)
        return nRes;

    nRes = LibVhd_2_CheckTestSequence(hVhd, (4*KDefSecPerBlock)-4, 8, seqGen1);
    if(nRes != KErrNone)
        return nRes;

    LibVhd_2_CloseVhd(hVhd);

    return KErrNone;
}

//--------------------------------------------------------------------
/**
    Check contents of the VHD chain starting from "Diff1"
    @param  aFileName file name
    @return true if check passed OK and the test sequence in the file and all its predecessors matched the pattern
*/
static int CheckContents_Diff1(const char* aFileName)
{
    TEST_LOG("aFileName:%s", aFileName);

    TVhdHandle hVhd;
    TVHD_ParamsStruct  vhdParams;
    int nRes;

    TRndSequenceGen seqGen1(KRndSeed1);
    TRndSequenceGen seqGen2(KRndSeed2);


    hVhd = VHD_Open(aFileName, VHDF_OPEN_RDONLY|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    nRes = VHD_Info(hVhd, &vhdParams);
    test_KErrNone(nRes);

    const uint KFileSizeSectors = vhdParams.vhdSectors; //-- real size in sectors


    //-- check empty space
    nRes = LibVhd_2_CheckFileFill(hVhd, 0, 1*KDefSecPerBlock, 0);
    if(nRes != KErrNone)
        return nRes;

    nRes = LibVhd_2_CheckFileFill(hVhd, 1*KDefSecPerBlock+8, 2*KDefSecPerBlock-8 + KDefSecPerBlock-4, 0);
    if(nRes != KErrNone)
        return nRes;

    nRes = LibVhd_2_CheckFileFill(hVhd, 4*KDefSecPerBlock+4, 2*KDefSecPerBlock + KDefSecPerBlock-4, 0);
    if(nRes != KErrNone)
        return nRes;


    seqGen1.InitRndSeed(KRndSeed1);

    //-- this part is inherited from the parent(s)
    nRes = LibVhd_2_CheckTestSequence(hVhd, KFileSizeSectors-8, 6, seqGen1);
    if(nRes != KErrNone)
        return nRes;

    seqGen1.SkipSequence(2*KDefSecSize);

    nRes = LibVhd_2_CheckTestSequence(hVhd, (4*KDefSecPerBlock)-4, 8, seqGen1);
    if(nRes != KErrNone)
        return nRes;

    //-- original part
    seqGen2.InitRndSeed(KRndSeed2);
    nRes = LibVhd_2_CheckTestSequence(hVhd, KFileSizeSectors-2, 2, seqGen2);
    if(nRes != KErrNone)
        return nRes;

    nRes = LibVhd_2_CheckTestSequence(hVhd, 1*KDefSecPerBlock, 8, seqGen2);
    if(nRes != KErrNone)
        return nRes;

    LibVhd_2_CloseVhd(hVhd);

    return KErrNone;
}

//--------------------------------------------------------------------
/**
    Check contents of the VHD chain starting from "Diff2"
    @param  aFileName file name
    @return KErrNone if check passed OK and the test sequence in the file and all its predecessors matched the pattern
*/
static int CheckContents_Diff2(const char* aFileName)
{
    TEST_LOG("aFileName:%s", aFileName);

    TVhdHandle hVhd;
    TVHD_ParamsStruct  vhdParams;
    int nRes;

    TRndSequenceGen seqGen1(KRndSeed1);
    TRndSequenceGen seqGen2(KRndSeed2);
    TRndSequenceGen seqGen3(KRndSeed3);

    hVhd = VHD_Open(aFileName, VHDF_OPEN_RDONLY|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    nRes = VHD_Info(hVhd, &vhdParams);
    test_KErrNone(nRes);

    const uint KFileSizeSectors = vhdParams.vhdSectors; //-- real size in sectors


    //-- check empty space
    nRes = LibVhd_2_CheckFileFill(hVhd, 0, 1*KDefSecPerBlock, 0);
    if(nRes != KErrNone)
        return nRes;

    //-- check empty space
    nRes = LibVhd_2_CheckFileFill(hVhd, 5*KDefSecPerBlock, 1*KDefSecPerBlock, 0);
    if(nRes != KErrNone)
        return nRes;



    //-- this part is inherited from the parent(s)
    seqGen1.InitRndSeed(KRndSeed1);
    nRes = LibVhd_2_CheckTestSequence(hVhd, KFileSizeSectors-8, 6, seqGen1);
    if(nRes != KErrNone)
        return nRes;

    seqGen1.SkipSequence(2*KDefSecSize);

    nRes = LibVhd_2_CheckTestSequence(hVhd, (4*KDefSecPerBlock)-4, 2, seqGen1);
    if(nRes != KErrNone)
        return nRes;

    seqGen1.SkipSequence(2*KDefSecSize);

    nRes = LibVhd_2_CheckTestSequence(hVhd, (4*KDefSecPerBlock), 4, seqGen1);
    if(nRes != KErrNone)
        return nRes;

    seqGen2.InitRndSeed(KRndSeed2);

    nRes = LibVhd_2_CheckTestSequence(hVhd, KFileSizeSectors-2, 2, seqGen2);
    if(nRes != KErrNone)
        return nRes;

    nRes = LibVhd_2_CheckTestSequence(hVhd, 1*KDefSecPerBlock, 8, seqGen2);
    if(nRes != KErrNone)
        return nRes;

    //-- original part
    seqGen3.InitRndSeed(KRndSeed3);

    nRes = LibVhd_2_CheckTestSequence(hVhd, 4*KDefSecPerBlock-2, 2, seqGen3);
    if(nRes != KErrNone)
        return nRes;

    nRes = LibVhd_2_CheckTestSequence(hVhd, 6*KDefSecPerBlock, 8, seqGen3);
    if(nRes != KErrNone)
        return nRes;

    nRes = LibVhd_2_CheckTestSequence(hVhd, 2*KDefSecPerBlock, 8, seqGen3);
    if(nRes != KErrNone)
        return nRes;

    LibVhd_2_CloseVhd(hVhd);

    return KErrNone;
}

//--------------------------------------------------------------------
/**
    Check contents of the VHD chain starting from "Tail"
    @param  aFileName file name
    @return KErrNone if check passed OK and the test sequence in the file and all its predecessors matched the pattern
*/
static int CheckContents_Tail(const char* aFileName)
{
    TEST_LOG("aFileName:%s", aFileName);

    TVhdHandle hVhd;
    TVHD_ParamsStruct  vhdParams;
    int nRes;

    TRndSequenceGen seqGen1(KRndSeed1);
    TRndSequenceGen seqGen2(KRndSeed2);
    TRndSequenceGen seqGen3(KRndSeed3);
    TRndSequenceGen seqGen4(KRndSeed4);


    hVhd = VHD_Open(aFileName, VHDF_OPEN_RDONLY|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    nRes = VHD_Info(hVhd, &vhdParams);
    test_KErrNone(nRes);

    const uint KFileSizeSectors = vhdParams.vhdSectors; //-- real size in sectors


    //-- check empty space
    nRes = LibVhd_2_CheckFileFill(hVhd, 0, 1*KDefSecPerBlock, 0);
    if(nRes != KErrNone)
        return nRes;


    //-- this part is inherited from the parent(s)
    seqGen1.InitRndSeed(KRndSeed1);
    nRes = LibVhd_2_CheckTestSequence(hVhd, KFileSizeSectors-8, 1, seqGen1);
    if(nRes != KErrNone)
        return nRes;

    seqGen1.SkipSequence(1*KDefSecSize);

    nRes = LibVhd_2_CheckTestSequence(hVhd, KFileSizeSectors-6, 4, seqGen1);
    if(nRes != KErrNone)
        return nRes;

    seqGen1.SkipSequence(2*KDefSecSize);

    nRes = LibVhd_2_CheckTestSequence(hVhd, (4*KDefSecPerBlock)-4, 2, seqGen1);
    if(nRes != KErrNone)
        return nRes;

    seqGen1.SkipSequence(2*KDefSecSize);

    nRes = LibVhd_2_CheckTestSequence(hVhd, (4*KDefSecPerBlock), 4, seqGen1);
    if(nRes != KErrNone)
        return nRes;


    seqGen2.InitRndSeed(KRndSeed2);
    nRes = LibVhd_2_CheckTestSequence(hVhd, KFileSizeSectors-2, 2, seqGen2);
    if(nRes != KErrNone)
        return nRes;

    seqGen2.SkipSequence(1*KDefSecSize);

    nRes = LibVhd_2_CheckTestSequence(hVhd, 1*KDefSecPerBlock+1, 7, seqGen2);
    if(nRes != KErrNone)
        return nRes;


    seqGen3.InitRndSeed(KRndSeed3);
    nRes = LibVhd_2_CheckTestSequence(hVhd, 4*KDefSecPerBlock-2, 2, seqGen3);
    if(nRes != KErrNone)
        return nRes;

    nRes = LibVhd_2_CheckTestSequence(hVhd, 6*KDefSecPerBlock, 8, seqGen3);
    if(nRes != KErrNone)
        return nRes;

    nRes = LibVhd_2_CheckTestSequence(hVhd, 2*KDefSecPerBlock, 1, seqGen3);
    if(nRes != KErrNone)
        return nRes;

    seqGen3.SkipSequence(1*KDefSecSize);

    nRes = LibVhd_2_CheckTestSequence(hVhd, 2*KDefSecPerBlock+2, 6, seqGen3);
    if(nRes != KErrNone)
        return nRes;

    seqGen4.InitRndSeed(KRndSeed4);

    //-- original part
    nRes = LibVhd_2_CheckTestSequence(hVhd, KFileSizeSectors-7,  1, seqGen4);
    if(nRes != KErrNone)
        return nRes;

    nRes = LibVhd_2_CheckTestSequence(hVhd, 5*KDefSecPerBlock,   8, seqGen4);
    if(nRes != KErrNone)
        return nRes;

    nRes = LibVhd_2_CheckTestSequence(hVhd, 2*KDefSecPerBlock+1, 1, seqGen4);
    if(nRes != KErrNone)
        return nRes;

    nRes = LibVhd_2_CheckTestSequence(hVhd, 1*KDefSecPerBlock,   1, seqGen4);
    if(nRes != KErrNone)
        return nRes;

    LibVhd_2_CloseVhd(hVhd);

    return KErrNone;
}



//--------------------------------------------------------------------
/**
    Create a VHD chain from 4 files:
    [Head]<-[Diff1]<-[Diff2]<-[Tail]

    @param  aEmptyFiles if true, then all files will be empty (if applicable).
                        otherwise they will contain some sectors with pseudo-random test sequence.

    @param  aTestContents if true, then the VHD chain contents will be checked
*/
static void CreateVhdChain(bool aEmptyFiles, bool aTestContents)
{
    TEST_LOG();

    TVhdHandle hVhd;
    TVHD_ParamsStruct  vhdParams;

    int nRes;


    unlink(strFileName_Head.c_str());
    unlink(strFileName_Diff1.c_str());
    unlink(strFileName_Diff2.c_str());
    unlink(strFileName_Tail.c_str());

    //-- 1. set up "Head" file [3]
    LibVhd_2_CreateVhd_Dynamic(strFileName_Head.c_str(), KReqFileSize_Sectors);
    TRndSequenceGen seqGen1(KRndSeed1);

    hVhd = VHD_Open(strFileName_Head.c_str(), VHDF_OPEN_RDWR|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    nRes = VHD_Info(hVhd, &vhdParams);
    test_KErrNone(nRes);

    const uint KFileSizeSectors = vhdParams.vhdSectors; //-- real size in sectors

    if(!aEmptyFiles)
    {
        LibVhd_2_WriteTestSequence(hVhd, KFileSizeSectors-8, 8, seqGen1);
        LibVhd_2_WriteTestSequence(hVhd, (4*KDefSecPerBlock)-4, 8, seqGen1);
    }

    LibVhd_2_CloseVhd(hVhd);

    //-- 2. set up "Diff1" file  [2]
    LibVhd_2_CreateVhd_Diff(strFileName_Diff1.c_str(), strFileName_Head.c_str());

    hVhd = VHD_Open(strFileName_Diff1.c_str(), VHDF_OPEN_RDWR|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    nRes = VHD_Info(hVhd, &vhdParams);
    test_KErrNone(nRes);
    test(vhdParams.vhdSectors == KFileSizeSectors);

    TRndSequenceGen seqGen2(KRndSeed2);

    if(!aEmptyFiles)
    {
        LibVhd_2_WriteTestSequence(hVhd, KFileSizeSectors-2, 2, seqGen2);
        LibVhd_2_WriteTestSequence(hVhd, 1*KDefSecPerBlock, 8, seqGen2);

    }

    LibVhd_2_CloseVhd(hVhd);


    //-- 3. set up "Diff2" file  [1]
    LibVhd_2_CreateVhd_Diff(strFileName_Diff2.c_str(), strFileName_Diff1.c_str());

    hVhd = VHD_Open(strFileName_Diff2.c_str(), VHDF_OPEN_RDWR|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    nRes = VHD_Info(hVhd, &vhdParams);
    test_KErrNone(nRes);
    test(vhdParams.vhdSectors == KFileSizeSectors);

    TRndSequenceGen seqGen3(KRndSeed3);

    if(!aEmptyFiles)
    {
        LibVhd_2_WriteTestSequence(hVhd, 4*KDefSecPerBlock-2, 2, seqGen3);
        LibVhd_2_WriteTestSequence(hVhd, 6*KDefSecPerBlock, 8, seqGen3);
        LibVhd_2_WriteTestSequence(hVhd, 2*KDefSecPerBlock, 8, seqGen3);
    }

    LibVhd_2_CloseVhd(hVhd);

    //-- 4. set up a leaf "Tail" file  [0]
    LibVhd_2_CreateVhd_Diff(strFileName_Tail.c_str(), strFileName_Diff2.c_str());

    hVhd = VHD_Open(strFileName_Tail.c_str(), VHDF_OPEN_RDWR|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    nRes = VHD_Info(hVhd, &vhdParams);
    test_KErrNone(nRes);
    test(vhdParams.vhdSectors == KFileSizeSectors);

    TRndSequenceGen seqGen4(KRndSeed4);

    if(!aEmptyFiles)
    {
        LibVhd_2_WriteTestSequence(hVhd, KFileSizeSectors-7,  1, seqGen4);
        LibVhd_2_WriteTestSequence(hVhd, 5*KDefSecPerBlock,   8, seqGen4);
        LibVhd_2_WriteTestSequence(hVhd, 2*KDefSecPerBlock+1, 1, seqGen4);
        LibVhd_2_WriteTestSequence(hVhd, 1*KDefSecPerBlock,   1, seqGen4);
    }

    //-- 5. walk down the chain, just in case
    nRes = VHD_ParentInfo(hVhd, &vhdParams, 0); //-- "Tail", index == 0
    test_KErrNone(nRes);

    test(vhdParams.vhdType == EVhd_Diff);
    test(strFileName_Tail.compare(vhdParams.vhdFileName) == 0 );
    test(strFileName_Diff2.compare(vhdParams.vhdParentName) == 0 );

    nRes = VHD_ParentInfo(hVhd, &vhdParams, 1); //-- "Diff2", index == 1
    test_KErrNone(nRes);

    test(vhdParams.vhdType == EVhd_Diff);
    test(strFileName_Diff2.compare(vhdParams.vhdFileName) == 0 );
    test(strFileName_Diff1.compare(vhdParams.vhdParentName) == 0 );

    nRes = VHD_ParentInfo(hVhd, &vhdParams, 2); //-- "Diff1", index == 2
    test_KErrNone(nRes);

    test(vhdParams.vhdType == EVhd_Diff);
    test(strFileName_Diff1.compare(vhdParams.vhdFileName) == 0 );
    test(strFileName_Head.compare(vhdParams.vhdParentName) == 0 );

    nRes = VHD_ParentInfo(hVhd, &vhdParams, 3); //-- "Head", index == 3
    test_KErrNone(nRes);

    test(vhdParams.vhdType == EVhd_Dynamic);
    test(strFileName_Head.compare(vhdParams.vhdFileName) == 0 );
    test(strlen(vhdParams.vhdParentName) == 0 );

    nRes = VHD_ParentInfo(hVhd, &vhdParams, 4); //-- index == 4, no more files in the chain
    test_Val(nRes, KErrNotFound);



    //-- verify files contents in the chain
    if(aTestContents)
    {
        //-- "Head" file, 1 file chain
        nRes = CheckContents_Head(strFileName_Head.c_str());
        test_KErrNone(nRes);

        //-- 2. "Diff1" file, 2 files chain
        nRes = CheckContents_Diff1(strFileName_Diff1.c_str());
        test_KErrNone(nRes);

        //-- 3. "Diff2" file, 3 files chain
        nRes = CheckContents_Diff2(strFileName_Diff2.c_str());
        test_KErrNone(nRes);

        //-- 4. "Tail" file, 3 files chain
        nRes = CheckContents_Tail(strFileName_Tail.c_str());
        test_KErrNone(nRes);

    }


    LibVhd_2_CloseVhd(hVhd);

}

//--------------------------------------------------------------------
/**
    Test coalescing VHD chain directly into the opened "Tail" VHD.
    Not a safe way to do this
*/
static void TestCoalesce_IntoTail()
{
    TEST_LOG();
    int nRes;

    TVhdHandle hVhd;

    //==================================================================
    //-- Create a chain of VHD files with some sectors in each containing
    //-- pseudo-random test sequence. Some sectors in child VHDs override
    //-- parent's contents
    CreateVhdChain(false, true);

    //==================================================================

    //-- coalesce 1 parent into the opened Tail
    hVhd = VHD_Open(strFileName_Tail.c_str(), VHDF_OPEN_RDWR|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    nRes = VHD_CoalesceChain(hVhd, 1, 0);
    test_KErrNone(nRes);

    VHD_Close(hVhd);

    //-- check "Tail" file, now it contains data from [T] + [diff2]
    //-- "diff2" file should be deleted
    nRes = CheckContents_Tail(strFileName_Tail.c_str());
    test_KErrNone(nRes);

    nRes = access(strFileName_Tail.c_str(), R_OK);
    test_KErrNone(nRes);

    nRes = access(strFileName_Diff2.c_str(), R_OK);
    test_Val(nRes, -1);

    nRes = access(strFileName_Diff1.c_str(), R_OK);
    test_KErrNone(nRes);

    nRes = access(strFileName_Head.c_str(), R_OK);
    test_KErrNone(nRes);


    //-- coalesce 2 parents into the Tail
    CreateVhdChain(false, false);

    hVhd = VHD_Open(strFileName_Tail.c_str(), VHDF_OPEN_RDWR|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    nRes = VHD_CoalesceChain(hVhd, 2, 0);
    test_KErrNone(nRes);

    VHD_Close(hVhd);

    //-- check "Tail" file, now it contains data from [T] + [diff2] + [diff1]
    //-- "diff2", ""diff1" files should be deleted
    nRes = CheckContents_Tail(strFileName_Tail.c_str());
    test_KErrNone(nRes);

    nRes = access(strFileName_Tail.c_str(), R_OK);
    test_KErrNone(nRes);

    nRes = access(strFileName_Diff2.c_str(), R_OK);
    test_Val(nRes, -1);

    nRes = access(strFileName_Diff1.c_str(), R_OK);
    test_Val(nRes, -1);

    nRes = access(strFileName_Head.c_str(), R_OK);
    test_KErrNone(nRes);



    //-- coalesce whole chain into the Tail (except Head)
    CreateVhdChain(false, false);

    hVhd = VHD_Open(strFileName_Tail.c_str(), VHDF_OPEN_RDWR|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    nRes = VHD_CoalesceChain(hVhd, 0, 0);
    test_KErrNone(nRes);


    VHD_Close(hVhd);

    //-- check "Tail" file, now it contains data from [T] + [diff2] + [diff1]
    nRes = CheckContents_Tail(strFileName_Tail.c_str());
    test_KErrNone(nRes);

    //-- check "Tail" file, now it contains data from [T] + [diff2] + [diff1]
    //-- "diff2", ""diff1" files should be deleted
    nRes = CheckContents_Tail(strFileName_Tail.c_str());
    test_KErrNone(nRes);

    nRes = access(strFileName_Tail.c_str(), R_OK);
    test_KErrNone(nRes);

    nRes = access(strFileName_Diff2.c_str(), R_OK);
    test_Val(nRes, -1);

    nRes = access(strFileName_Diff1.c_str(), R_OK);
    test_Val(nRes, -1);

    nRes = access(strFileName_Head.c_str(), R_OK);
    test_KErrNone(nRes);


}

//--------------------------------------------------------------------
/**
    Test coalescing VHD chain via using intermediate VHD file
    that replaces one of the parents.
    This way should be more-or less safe if write failure occurs.

*/
static void TestCoalesce_Safely()
{
    TEST_LOG();
    int nRes;

    TVhdHandle hVhd;

    //==================================================================
    //-- Create a chain of VHD files with some sectors in each containing
    //-- pseudo-random test sequence. Some sectors in child VHDs override
    //-- parent's contents
    CreateVhdChain(false, true);

    //==================================================================

    //-- coalesce 2 files "diff2" and "diff1" into a new file that will become "diff2"
    //-- neither "Head" nor "Tail" are changed at all
    //-- this also means that the "tail" VHD can be opened RO
    hVhd = VHD_Open(strFileName_Tail.c_str(), VHDF_OPEN_RDONLY|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    nRes = VHD_CoalesceChain(hVhd, 2, 1);
    test_KErrNone(nRes);

    VHD_Close(hVhd);

    //-- check "Tail" file, now it contains data from [T] + All parents'
    //-- "diff1" file should be deleted, "diff2" file should contain data from [Diff2 + Diff1]
    nRes = CheckContents_Tail(strFileName_Tail.c_str());
    test_KErrNone(nRes);

    nRes = CheckContents_Diff2(strFileName_Diff2.c_str());
    test_KErrNone(nRes);

    nRes = CheckContents_Head(strFileName_Head.c_str());
    test_KErrNone(nRes);


    nRes = access(strFileName_Tail.c_str(), R_OK);
    test_KErrNone(nRes);

    nRes = access(strFileName_Diff2.c_str(), R_OK);
    test_KErrNone(nRes);

    nRes = access(strFileName_Diff1.c_str(), R_OK);
    test_Val(nRes, -1);

    nRes = access(strFileName_Head.c_str(), R_OK);
    test_KErrNone(nRes);

    //-- coalesce Whole VHD chain (excluting Head into the 1st parent of the Tail"
    CreateVhdChain(false, false);

    hVhd = VHD_Open(strFileName_Tail.c_str(), VHDF_OPEN_RDONLY|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    nRes = VHD_CoalesceChain(hVhd, 0, 1);
    test_KErrNone(nRes);

    VHD_Close(hVhd);

    //-- check "Tail" file, now it contains data from [T] + All parents'
    //-- "diff1" file should be deleted, "diff2" file should contain data from [Diff2 + Diff1]
    nRes = CheckContents_Tail(strFileName_Tail.c_str());
    test_KErrNone(nRes);

    nRes = CheckContents_Diff2(strFileName_Diff2.c_str());
    test_KErrNone(nRes);

    nRes = CheckContents_Head(strFileName_Head.c_str());
    test_KErrNone(nRes);


    nRes = access(strFileName_Tail.c_str(), R_OK);
    test_KErrNone(nRes);

    nRes = access(strFileName_Diff2.c_str(), R_OK);
    test_KErrNone(nRes);

    nRes = access(strFileName_Diff1.c_str(), R_OK);
    test_Val(nRes, -1);

    nRes = access(strFileName_Head.c_str(), R_OK);
    test_KErrNone(nRes);

}

//--------------------------------------------------------------------
/**
    Initialise tests
*/
void CoalesceTests_Init()
{
    //-- make fully qualified names to VHD files in the chain
    strFileName_Head  += "Head_test_coalesce.vhd";
    strFileName_Diff1 += "Diff1_test_coalesce.vhd";
    strFileName_Diff2 += "Diff2_test_coalesce.vhd";
    strFileName_Tail  += "Tail_test_coalesce.vhd";
}


//--------------------------------------------------------------------
/** Execute coalescing tests */
void CoalesceTests_Execute()
{
    TEST_LOG();
    TestCoalesce_IntoTail();
    TestCoalesce_Safely();
}




