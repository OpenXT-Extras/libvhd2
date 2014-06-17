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
    @file   Interoperability tests between a legacy libvhd and a new one.
    This test requires libvhd to be installed
*/



#include <stdio.h>
#include <assert.h>

#include "libvhd2_test.h"

#include "../include/libvhd2.h"

extern "C"
{//-- legacy libvhd header file. path is hardcoded for now.
#include "../../../Source/blktap/include/libvhd.h"
}

//--------------------------------------------------------------------
const uint KTestFlag_KeepFile       = 0x00000001;   //-- don't delete a file after doing the test
const uint KTestFlag_CheckPureMode  = 0x00000002;   //-- test VHDF_OPMODE_PURE_BLOCKS flag if applicable


//--------------------------------------------------------------------

//-- legacy libvhd context structure. used for testing.
static vhd_context_t libvhd_ctx;


//--------------------------------------------------------------------
/**
    Write a pseudo-random sequence of bytes to a previously opened VHD file. Legacy "libvhd" is used.

    @param  apVhdCtx        libvhd file context structure pointer
    @param  aStartSector    statring sector
    @param  aNumSectors     number of secttors to write
    @param  aSeqGen         ref. to the sequence generator. The caller is responsible for maintaininig its state

*/
static void LibVhd_WriteTestSequence(vhd_context_t* apVhdCtx, uint aStartSector, uint aNumSectors, TRndSequenceGen& aSeqGen)
{
    TEST_LOG("aStartSector:%d, aNumSectors:%d",  aStartSector, aNumSectors);

    test(apVhdCtx);
    test(aNumSectors > 0);

    int nRes;
    uint8_t buf[32*K1KiloByte];

    uint remSectors = aNumSectors;
    uint currSector = aStartSector;
    const uint bufSizeSectors = sizeof(buf) / KDefSecSize;

    while(remSectors)
    {
        const uint secToWrite = Min(remSectors, bufSizeSectors);
        aSeqGen.GenerateSequence(buf, secToWrite*KDefSecSize);

        nRes = vhd_io_write(apVhdCtx, (char*)buf, currSector, secToWrite);
        test_KErrNone(nRes);

        remSectors -= secToWrite;
        currSector += secToWrite;
    }
}

//--------------------------------------------------------------------
/**
    Read and verify a pseudo-random sequence of bytes from a VHD file.
    Legacy "libvhd" is used.

    @param  apVhdCtx        libvhd file context structure pointer
    @param  aStartSector    statring sector
    @param  aNumSectors     number of secttors to read/verify
    @param  aSeqGen         ref. to the sequence generator. The caller is responsible for maintaininig its state

    @return KErrNone if the read sequence is the same as generated by aSeqGen. KErrCorrupt otherwise
*/
static int LibVhd_CheckTestSequence(vhd_context_t* apVhdCtx, uint aStartSector, uint aNumSectors, TRndSequenceGen& aSeqGen)
{
    TEST_LOG("aStartSector:%d, aNumSectors:%d", aStartSector, aNumSectors);

    test(apVhdCtx);
    test(aNumSectors > 0);

    int nRes;
    uint8_t buf[32*K1KiloByte];

    uint remSectors = aNumSectors;
    uint currSector = aStartSector;

    const uint bufSizeSectors = sizeof(buf) / KDefSecSize;

    while(remSectors)
    {
        const uint secToRead = Min(remSectors, bufSizeSectors);

        FillZ(buf);

        nRes = vhd_io_read(apVhdCtx, (char*)buf, currSector, secToRead );
        test_KErrNone(nRes);

        nRes = aSeqGen.CheckSequence(buf, secToRead*KDefSecSize);
        if(nRes != KErrNone)
            return nRes;

        remSectors -= secToRead;
        currSector += secToRead;
    }

    return KErrNone;
}


//--------------------------------------------------------------------
/**
    Read a number of sectors from a VHD file and verify that they are filled with the given byte.
    Legacy "libvhd" is used.

    @param  apVhdCtx        libvhd file context structure pointer
    @param  aStartSector    statring sector
    @param  aNumSectors     number of secttors to read/verify
    @param  aFill           required filling byte

    @return KErrNone if the read sequence is the same as generated by aSeqGen. KErrCorrupt otherwise
*/
static int LibVhd_CheckFileFill(vhd_context_t* apVhdCtx, uint aStartSector, uint aNumSectors, uint8_t aFill)
{
    TEST_LOG("aStartSector:%d, aNumSectors:%d, fill:%d", aStartSector, aNumSectors,aFill);

    test(apVhdCtx);
    test(aNumSectors > 0);

    int nRes;
    uint8_t buf[32*K1KiloByte];

    uint remSectors = aNumSectors;
    uint currSector = aStartSector;

    const uint bufSizeSectors = sizeof(buf) / KDefSecSize;

    while(remSectors)
    {
        const uint secToRead = Min(remSectors, bufSizeSectors);

        buf[0] = ~aFill;

        nRes = vhd_io_read(apVhdCtx, (char*)buf, currSector, secToRead );
        test_KErrNone(nRes);

        if(!CheckFilling(buf, secToRead*KDefSecSize, aFill))
            return KErrCorrupt;


        remSectors -= secToRead;
        currSector += secToRead;
    }

    return KErrNone;
}


//--------------------------------------------------------------------
/**
    Fixed VHD test scenario 1.
    - Create a fixed vhd file (libvhd2)
    - fill it entirely with a unique sequence
    - verify the sequence in the file using legacy libvhd
*/
static void Do_InteropTest_Vhd_Fixed_1(const char* aFileName, uint32_t aFilesizeBytes)
{
    TEST_LOG();

    //-- requested file size in bytes, can be slightly different after creation because CHS calculations
    const uint KReqFileSize_Sectors = aFilesizeBytes >> KDefSecSizeLog2;

    unlink(aFileName);

    int nRes;
    const uint KRndSeed1 = 0xdeadbeef;

    TRndSequenceGen seqGen(KRndSeed1);

    //-- 1. create a fixed VHD file (libvhd2)
    LibVhd_2_CreateVhd_Fixed(aFileName, KReqFileSize_Sectors);

    //-- 2 open file and get its geometry, it might slightly differ from the required.
    TVhdHandle hVhd;

    const uint vhdOpenFlags = VHDF_OPEN_RDWR|VHDF_OPEN_DIRECTIO;

    hVhd = VHD_Open(aFileName, vhdOpenFlags);
    test(hVhd >0);

    TVHD_ParamsStruct  vhdParams;
    nRes = VHD_Info(hVhd, &vhdParams);
    test_KErrNone(nRes);

    const uint KFileSize_Sectors = vhdParams.vhdSectors;
    test(KFileSize_Sectors > 0);
    test(KFileSize_Sectors <= KReqFileSize_Sectors);

    //-- 3. fill whole file with the unique sequence.
    seqGen.InitRndSeed(KRndSeed1);
    LibVhd_2_WriteTestSequence(hVhd, 0, KFileSize_Sectors, seqGen);

    //-- 4. close the file
    LibVhd_2_CloseVhd(hVhd);


    //-- 5 open the file with legacy libvhd and check its geometry
    nRes = vhd_open(&libvhd_ctx, aFileName, VHD_OPEN_RDONLY | VHD_OPEN_CACHED);
    test_KErrNone(nRes);

    {
    const uint geom = libvhd_ctx.footer.geometry;
    const uint secs = GEOM_GET_CYLS(geom) * GEOM_GET_HEADS(geom) * GEOM_GET_SPT(geom);
    test(secs == KFileSize_Sectors);
    }

    //-- 6. read all sectors and check the sequence using legacy libvhd
    seqGen.InitRndSeed(KRndSeed1);
    nRes = LibVhd_CheckTestSequence(&libvhd_ctx, 0, KFileSize_Sectors, seqGen);
    test_KErrNone(nRes);


    //-- 7. close the file
    vhd_close(&libvhd_ctx);

    unlink(aFileName);

}

//--------------------------------------------------------------------
/**
    Fixed VHD test scenario 2.
    - Create a fixed vhd file using legacy libvhd
    - fill it entirely with a unique sequence
    - verify the sequence in the file using libvhd2
*/
static void Do_InteropTest_Vhd_Fixed_2(const char* aFileName, uint32_t aFilesizeBytes)
{
    TEST_LOG();

    unlink(aFileName);

    int nRes;
    const uint KRndSeed1 = 0xdeadbeef;

    TRndSequenceGen seqGen(KRndSeed1);

    //-- 1. create a fixed VHD file using legacy libvhd
    nRes = vhd_create(aFileName, aFilesizeBytes, HD_TYPE_FIXED, 0, 0);
    test_KErrNone(nRes);

    //-- 2. open a file and get its parameters
    nRes = vhd_open(&libvhd_ctx, aFileName, VHD_OPEN_RDWR | VHD_OPEN_CACHED);
    test_KErrNone(nRes);

    const uint geom = libvhd_ctx.footer.geometry;
    const uint KFileSize_Sectors = GEOM_GET_CYLS(geom) * GEOM_GET_HEADS(geom) * GEOM_GET_SPT(geom);

    test(KFileSize_Sectors > 0);
    test(KFileSize_Sectors <= (aFilesizeBytes/KDefSecSize));

    //-- 3. fill whole file with the unique sequence using old libvhd
    seqGen.InitRndSeed(KRndSeed1);
    LibVhd_WriteTestSequence(&libvhd_ctx, 0, KFileSize_Sectors, seqGen);

    //-- 4. close the file
    vhd_close(&libvhd_ctx);


    //-- 5. open the file using libvhd2
    TVhdHandle hVhd;
    const uint vhdOpenFlags = VHDF_OPEN_RDONLY|VHDF_OPEN_DIRECTIO;

    hVhd = VHD_Open(aFileName, vhdOpenFlags);
    test(hVhd >0);

    TVHD_ParamsStruct  vhdParams;
    nRes = VHD_Info(hVhd, &vhdParams);
    test_KErrNone(nRes);

    test(vhdParams.vhdSectors == KFileSize_Sectors);

    //-- 6. read all sectors and check the sequence
    seqGen.InitRndSeed(KRndSeed1);
    nRes = LibVhd_2_CheckTestSequence(hVhd, 0, KFileSize_Sectors, seqGen);
    test_KErrNone(nRes);

    LibVhd_2_CloseVhd(hVhd);

    unlink(aFileName);
}




//--------------------------------------------------------------------
/**
    Check that certain sectors of the VHD file are filled with the given sequence.
    Legacy libvhd is used.

    @param  apVhdCtx        libvhd file context structure pointer
    @param  aSectorList     an array of structures describing which sectors to check
    @param  aFill           aSeqGen pseudo-random sequence generator, in a proper state

    @return KErrNone if the read sequence is the same as generated by aSeqGen. KErrCorrupt otherwise
*/
static int LibVhd_CheckSectorsFill(vhd_context_t* apVhdCtx, vector<TSectorsBase>& aSectorList, TRndSequenceGen& aSeqGen)
{
    TEST_LOG();

    int nRes;

    test(aSectorList.size());
    for(uint i=0; i<aSectorList.size(); ++i)
    {
        const TSectorsBase& secStr = aSectorList[i];

        test(secStr.iSecNum);
        test(secStr.Type() != TSectorsBase::EInvalid);

        //-- check zero-filling
        if(secStr.Type() == TSectorsBase::EEmpty)
        {
            nRes = LibVhd_CheckFileFill(apVhdCtx, secStr.iSecStart, secStr.iSecNum, 0);
            if(nRes != KErrNone)
            {
                TEST_LOG("check failed !");
                return nRes;
            }

        }

        //-- check filling with the test sequence
        if(secStr.Type() == TSectorsBase::EFilled)
        {
            nRes = LibVhd_CheckTestSequence(apVhdCtx, secStr.iSecStart, secStr.iSecNum, aSeqGen);
            if(nRes != KErrNone)
            {
                TEST_LOG("check failed !");
                return nRes;
            }
        }

    }

    return KErrNone;
}



//--------------------------------------------------------------------
/**
    Dynamic VHD test scenario 1.
    - Create an empty dynamic vhd file (libvhd2)
    - write several blocks of sectors inside blocks and crossing block boundaries
    - verify the results with legacy libvhd
*/
static void Do_InteropTest_Vhd_Dynamic_1(const char* aFileName, uint32_t aFilesizeBytes, uint aTestFlags)
{
    TEST_LOG();

    //-- requested file size in bytes, can be slightly different after creation because CHS calculations
    const uint KReqFileSize_Sectors = aFilesizeBytes >> KDefSecSizeLog2;

    unlink(aFileName);

    int nRes;
    const uint KRndSeed1 = 0xdeadbeef;
    TRndSequenceGen seqGen(KRndSeed1);

    vector<TSectorsBase> Sectors;

    //-- 1. create an empty dynamic VHD file (libvhd2)
    LibVhd_2_CreateVhd_Dynamic(aFileName, KReqFileSize_Sectors);

    //-- 2 open file and get its geometry, it might slightly differ from the required.
    TVhdHandle hVhd;

    uint vhdOpenFlags = VHDF_OPEN_RDWR|VHDF_OPEN_DIRECTIO;

    if(aTestFlags & KTestFlag_CheckPureMode)
        vhdOpenFlags |= VHDF_OPMODE_PURE_BLOCKS;


    hVhd = VHD_Open(aFileName, vhdOpenFlags);
    test(hVhd >0);

    TVHD_ParamsStruct  vhdParams;
    nRes = VHD_Info(hVhd, &vhdParams);
    test_KErrNone(nRes);

    const uint KFileSize_Sectors = vhdParams.vhdSectors;
    test(KFileSize_Sectors > 0);
    test(KFileSize_Sectors <= KReqFileSize_Sectors);

    seqGen.InitRndSeed(KRndSeed1);

    {
        TFilledSectors filledSectors;
        TEmptySectors  emptySectors;

        //-- make a write to the last sector of the Dynamic file. It should add a block
        filledSectors.iSecStart = KFileSize_Sectors-2;
        filledSectors.iSecNum   = 2;
        Sectors.push_back(filledSectors);

        emptySectors.iSecStart = (filledSectors.iSecStart >> KDefSecPerBlockLog2) << KDefSecPerBlockLog2;
        emptySectors.iSecNum   = filledSectors.iSecStart - emptySectors.iSecStart - 1;
        Sectors.push_back(emptySectors);


        //-- make a write that crosses block#0 and block#1 boundary; this will add 2 blocks
        filledSectors.iSecStart  = 0 + KDefSecPerBlock-2;
        filledSectors.iSecNum    = 4; //-- will cross sector boundary
        Sectors.push_back(filledSectors);

        emptySectors.iSecStart   = 0;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);

        emptySectors.iSecStart   = KDefSecPerBlock+2;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);


        //-- make a write that crosses 2 block boundaries starts in block#3, spans block#4 and ends in block#5
        filledSectors.iSecStart  = 3*KDefSecPerBlock + KDefSecPerBlock-2;
        filledSectors.iSecNum    = 2 + KDefSecPerBlock +2;
        Sectors.push_back(filledSectors);

        emptySectors.iSecStart   = 3*KDefSecPerBlock;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);

        emptySectors.iSecStart   = 5*KDefSecPerBlock+2;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);

        //-------------------------------

        //-- write a test sequence in designated places
        for(uint i=0; i<Sectors.size(); ++i)
        {
            if(Sectors[i].Type() == TSectorsBase::EFilled)
            {
                LibVhd_2_WriteTestSequence(hVhd, Sectors[i].iSecStart, Sectors[i].iSecNum, seqGen);
            }
        }
    }


    LibVhd_2_CloseVhd(hVhd);


    //-------------------------------

    //-- NN. open this file (legacy libvhd) and check data in sectors mentioned in "Sectors"
    nRes = vhd_open(&libvhd_ctx, aFileName, VHD_OPEN_RDONLY | VHD_OPEN_CACHED);
    test_KErrNone(nRes);

    {
    const uint secs = vhd_max_capacity(&libvhd_ctx) >> KDefSecSizeLog2;
    test(secs == KFileSize_Sectors);
    }

    test(Sectors.size());
    seqGen.InitRndSeed(KRndSeed1);

    nRes = LibVhd_CheckSectorsFill(&libvhd_ctx, Sectors, seqGen);
    test_KErrNone(nRes);

    //-- 7. close the file
    vhd_close(&libvhd_ctx);

    //-- delete file if required
    if(!(aTestFlags & KTestFlag_KeepFile))
        unlink(aFileName);
}


//--------------------------------------------------------------------
/**
    Dynamic VHD test scenario 2.

    - Create an empty dynamic vhd file (legacy libvhd)
    - write several blocks of sectors inside blocks and crossing block boundaries
    - verify the results with libvhd2
*/
static void Do_InteropTest_Vhd_Dynamic_2(const char* aFileName, uint32_t aFilesizeBytes, uint aTestFlags)
{
    TEST_LOG();

    unlink(aFileName);

    int nRes;
    const uint KRndSeed1 = 0xdeadbeef;
    TRndSequenceGen seqGen(KRndSeed1);

    vector<TSectorsBase> Sectors;

    //-- 1. create a fixed VHD file using legacy libvhd
    nRes = vhd_create(aFileName, aFilesizeBytes, HD_TYPE_DYNAMIC, 0, 0);
    test_KErrNone(nRes);

    //-- 2. open a file and get its parameters
    nRes = vhd_open(&libvhd_ctx, aFileName, VHD_OPEN_RDWR | VHD_OPEN_CACHED);
    test_KErrNone(nRes);

    //const uint geom = libvhd_ctx.footer.geometry;
    //const uint KFileSize_Sectors = GEOM_GET_CYLS(geom) * GEOM_GET_HEADS(geom) * GEOM_GET_SPT(geom);
    const uint KFileSize_Sectors = vhd_max_capacity(&libvhd_ctx) >> KDefSecSizeLog2;


    test(KFileSize_Sectors > 0);
    test(KFileSize_Sectors <= (aFilesizeBytes/KDefSecSize));


    seqGen.InitRndSeed(KRndSeed1);

    {
        TFilledSectors filledSectors;
        TEmptySectors  emptySectors;

        //-- make a write to the last sector of the Dynamic file. It should add a block
        filledSectors.iSecStart = KFileSize_Sectors-2;
        filledSectors.iSecNum   = 2;
        Sectors.push_back(filledSectors);

        emptySectors.iSecStart = (filledSectors.iSecStart >> KDefSecPerBlockLog2) << KDefSecPerBlockLog2;
        emptySectors.iSecNum   = filledSectors.iSecStart - emptySectors.iSecStart - 1;
        Sectors.push_back(emptySectors);


        //-- make a write that crosses block#0 and block#1 boundary; this will add 2 blocks
        filledSectors.iSecStart  = 0 + KDefSecPerBlock-2;
        filledSectors.iSecNum    = 4; //-- will cross sector boundary
        Sectors.push_back(filledSectors);

        emptySectors.iSecStart   = 0;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);

        emptySectors.iSecStart   = KDefSecPerBlock+2;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);


        //-- make a write that crosses 2 block boundaries starts in block#3, spans block#4 and ends in block#5
        filledSectors.iSecStart  = 3*KDefSecPerBlock + KDefSecPerBlock-2;
        filledSectors.iSecNum    = 2 + KDefSecPerBlock +2;
        Sectors.push_back(filledSectors);

        emptySectors.iSecStart   = 3*KDefSecPerBlock;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);

        emptySectors.iSecStart   = 5*KDefSecPerBlock+2;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);

        //-------------------------------

        //-- write a test sequence in designated places
        for(uint i=0; i<Sectors.size(); ++i)
        {
            if(Sectors[i].Type() == TSectorsBase::EFilled)
            {
                LibVhd_WriteTestSequence(&libvhd_ctx, Sectors[i].iSecStart, Sectors[i].iSecNum, seqGen);
            }
        }
    }


    vhd_close(&libvhd_ctx);

    //----------------------------

    //-- open this file (libvhd2) and check data in sectors mentioned in "Sectors"
    TVhdHandle hVhd;
    uint vhdOpenFlags = VHDF_OPEN_RDONLY|VHDF_OPEN_DIRECTIO;

    hVhd = VHD_Open(aFileName, vhdOpenFlags);
    test(hVhd >0);

    TVHD_ParamsStruct  vhdParams;
    nRes = VHD_Info(hVhd, &vhdParams);
    test_KErrNone(nRes);

    test(vhdParams.vhdSectors == KFileSize_Sectors);

    test(Sectors.size());
    seqGen.InitRndSeed(KRndSeed1);

    nRes = LibVhd_2_CheckSectorsFill(hVhd, Sectors, seqGen);
    test_KErrNone(nRes);

    LibVhd_2_CloseVhd(hVhd);

    //-- test VHDF_OPMODE_PURE_BLOCKS flag. Open the file once again; it will modify its contents and check again
    if(aTestFlags & KTestFlag_CheckPureMode)
    {
        hVhd = VHD_Open(aFileName, vhdOpenFlags | VHDF_OPEN_RDWR | VHDF_OPMODE_PURE_BLOCKS);
        test(hVhd >0);

        test(Sectors.size());
        seqGen.InitRndSeed(KRndSeed1);

        nRes = LibVhd_2_CheckSectorsFill(hVhd, Sectors, seqGen);
        test_KErrNone(nRes);

        LibVhd_2_CloseVhd(hVhd);
    }




    //-- delete file if required
    if(!(aTestFlags & KTestFlag_KeepFile))
        unlink(aFileName);

}


//--------------------------------------------------------------------
/**
    Dynamic VHD  libvhd2/libvhd interoperability tests.
    Writes some data to the diff. VHD using libvhd2 and checks it using legacy libvhd

    @param  aFileName       Diff. VHD file name
    @param  aParentFileName name of the parent VHD file
*/
static void Do_InteropTest_Vhd_Diff_1(const char* aFileName, const char* aParentFileName, uint aTestFlags)
{
    TEST_LOG();

    int nRes;
    const uint KRndSeed1 = 0xdeadbeef;
    const uint KRndSeed2 = 0xface1734;

    TRndSequenceGen seqGen1(KRndSeed1);
    TRndSequenceGen seqGen2(KRndSeed2);

    TVhdHandle hVhd;

    vector<TSectorsBase> Sectors;

    unlink(aFileName);

    //-- 0. get parent's VHD parameters
    hVhd = VHD_Open(aParentFileName, VHDF_OPEN_RDONLY|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    TVHD_ParamsStruct  vhdParentParams;
    nRes = VHD_Info(hVhd, &vhdParentParams);
    test_KErrNone(nRes);

    LibVhd_2_CloseVhd(hVhd);

    //-- 1. create an empty differencing VHD file (libvhd2)
    LibVhd_2_CreateVhd_Diff(aFileName, aParentFileName);

    //-- 2. open file and get its geometry (libvhd)
    nRes = vhd_open(&libvhd_ctx, aFileName, VHD_OPEN_RDWR | VHD_OPEN_CACHED);
    test_KErrNone(nRes);

    //const uint geom = libvhd_ctx.footer.geometry;
    //const uint KFileSize_Sectors = GEOM_GET_CYLS(geom) * GEOM_GET_HEADS(geom) * GEOM_GET_SPT(geom);
    const uint KFileSize_Sectors = vhd_max_capacity(&libvhd_ctx) >> KDefSecSizeLog2;


    test(KFileSize_Sectors > 0);
    test(KFileSize_Sectors == vhdParentParams.vhdSectors);

    //-- 3. read and verify data from the empty diff. disk (initialised in LibVhd_2_CreateVhd_Diff())  using legacy libvhd.

    {//-- this data chunks were created by previous test call in the parent VHD
        TFilledSectors filledSectors;
        TEmptySectors  emptySectors;

        Sectors.clear();

        //-- make a write to the last sector of the Dynamic file. It should add a block
        filledSectors.iSecStart = KFileSize_Sectors-2;
        filledSectors.iSecNum   = 2;
        Sectors.push_back(filledSectors);

        emptySectors.iSecStart = (filledSectors.iSecStart >> KDefSecPerBlockLog2) << KDefSecPerBlockLog2;
        emptySectors.iSecNum   = filledSectors.iSecStart - emptySectors.iSecStart - 1;
        Sectors.push_back(emptySectors);

        //-- make a write that crosses block#0 and block#1 boundary; this will add 2 blocks
        filledSectors.iSecStart  = 0 + KDefSecPerBlock-2;
        filledSectors.iSecNum    = 4; //-- will cross sector boundary
        Sectors.push_back(filledSectors);

        emptySectors.iSecStart   = 0;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);

        emptySectors.iSecStart   = KDefSecPerBlock+2;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);


        //-- make a write that crosses 2 block boundaries starts in block#3, spans block#4 and ends in block#5
        filledSectors.iSecStart  = 3*KDefSecPerBlock + KDefSecPerBlock-2;
        filledSectors.iSecNum    = 2 + KDefSecPerBlock +2;
        Sectors.push_back(filledSectors);

        emptySectors.iSecStart   = 3*KDefSecPerBlock;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);

        emptySectors.iSecStart   = 5*KDefSecPerBlock+2;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);


        test(Sectors.size());
        seqGen1.InitRndSeed(KRndSeed1);

        nRes = LibVhd_CheckSectorsFill(&libvhd_ctx, Sectors, seqGen1);
        test_KErrNone(nRes);

    }

    vhd_close(&libvhd_ctx);

    //-- 4. write (overwrite) some data to the differencing VHD file (libvhd2)
    uint vhdOpenFlags = VHDF_OPEN_RDWR|VHDF_OPEN_DIRECTIO;

    if(aTestFlags & KTestFlag_CheckPureMode)
        vhdOpenFlags |= VHDF_OPMODE_PURE_BLOCKS;

    hVhd = VHD_Open(aFileName, vhdOpenFlags);
    test(hVhd >0);

    TVHD_ParamsStruct  vhdParams;
    nRes = VHD_Info(hVhd, &vhdParams);
    test_KErrNone(nRes);

    test(vhdParams.vhdType == EVhd_Diff);
    test(KFileSize_Sectors == vhdParams.vhdSectors);


    {//-- overwrite some data in the parent VHD file with a different random sequence (libvhd2)
        TFilledSectors filledSectors;

        Sectors.clear();

        //-- overwrite the last sector of the Dynamic file.
        filledSectors.iSecStart = KFileSize_Sectors-1;
        filledSectors.iSecNum   = 1;
        Sectors.push_back(filledSectors);

        //-- overwrite the first 2 sectors in block1
        filledSectors.iSecStart  = 1*KDefSecPerBlock;
        filledSectors.iSecNum    = 2;
        Sectors.push_back(filledSectors);

        //-- overwrite some sectors that cross block boundary
        filledSectors.iSecStart  = 3*KDefSecPerBlock + KDefSecPerBlock-1;
        filledSectors.iSecNum    = 2;
        Sectors.push_back(filledSectors);

        filledSectors.iSecStart  = 4*KDefSecPerBlock + 17;
        filledSectors.iSecNum    = 100;
        Sectors.push_back(filledSectors);

        filledSectors.iSecStart  = 4*KDefSecPerBlock + KDefSecPerBlock-1;
        filledSectors.iSecNum    = 2;
        Sectors.push_back(filledSectors);


        filledSectors.iSecStart  = 6*KDefSecPerBlock;
        filledSectors.iSecNum    = 25;
        Sectors.push_back(filledSectors);


        test(Sectors.size());
        seqGen2.InitRndSeed(KRndSeed2);
        for(uint i=0; i<Sectors.size(); ++i)
        {
            if(Sectors[i].Type() == TSectorsBase::EFilled)
            {
                LibVhd_2_WriteTestSequence(hVhd, Sectors[i].iSecStart, Sectors[i].iSecNum, seqGen2);
            }
        }

    }

    LibVhd_2_CloseVhd(hVhd);


    //-- 5. check new data chunks in the diff. VHD and old, non-overwritten chunks from the parent (libvhd)
    nRes = vhd_open(&libvhd_ctx, aFileName, VHD_OPEN_RDWR | VHD_OPEN_CACHED);
    test_KErrNone(nRes);

    //-- 5.1 check new data chunks that overwrote parent's contents
    seqGen2.InitRndSeed(KRndSeed2);
    nRes = LibVhd_CheckSectorsFill(&libvhd_ctx, Sectors, seqGen2);
    test_KErrNone(nRes);

    //-- 5.2 check data chunks that should remain intact (read from the parent VHD)
    {
        TFilledSectors filledSectors;

        seqGen1.InitRndSeed(KRndSeed1);

        Sectors.clear();
        filledSectors.iSecStart = KFileSize_Sectors-2;
        filledSectors.iSecNum   = 1;
        Sectors.push_back(filledSectors);

        nRes = LibVhd_CheckSectorsFill(&libvhd_ctx, Sectors, seqGen1);
        test_KErrNone(nRes);

        seqGen1.SkipSequence(1*KDefSecSize);

        Sectors.clear();
        filledSectors.iSecStart = 0 + KDefSecPerBlock-2;
        filledSectors.iSecNum   = 2;
        Sectors.push_back(filledSectors);

        nRes = LibVhd_CheckSectorsFill(&libvhd_ctx, Sectors, seqGen1);
        test_KErrNone(nRes);

        seqGen1.SkipSequence(2*KDefSecSize);

        Sectors.clear();
        filledSectors.iSecStart = 3*KDefSecPerBlock + KDefSecPerBlock-2;
        filledSectors.iSecNum   = 1;
        Sectors.push_back(filledSectors);

        nRes = LibVhd_CheckSectorsFill(&libvhd_ctx, Sectors, seqGen1);
        test_KErrNone(nRes);


        seqGen1.SkipSequence(2*KDefSecSize);

        Sectors.clear();
        filledSectors.iSecStart = 4*KDefSecPerBlock + 1;
        filledSectors.iSecNum   = 16;
        Sectors.push_back(filledSectors);

        nRes = LibVhd_CheckSectorsFill(&libvhd_ctx, Sectors, seqGen1);
        test_KErrNone(nRes);

        seqGen1.SkipSequence(100*KDefSecSize);

        Sectors.clear();
        filledSectors.iSecStart = 4*KDefSecPerBlock + 17 + 100;
        filledSectors.iSecNum   = KDefSecPerBlock - (17 + 100 +1);
        Sectors.push_back(filledSectors);

        nRes = LibVhd_CheckSectorsFill(&libvhd_ctx, Sectors, seqGen1);
        test_KErrNone(nRes);

        seqGen1.SkipSequence(2*KDefSecSize);

        Sectors.clear();
        filledSectors.iSecStart = 5*KDefSecPerBlock + 1;
        filledSectors.iSecNum   = 1;
        Sectors.push_back(filledSectors);

        nRes = LibVhd_CheckSectorsFill(&libvhd_ctx, Sectors, seqGen1);
        test_KErrNone(nRes);

    }

    vhd_close(&libvhd_ctx);

    unlink(aFileName);
}


//--------------------------------------------------------------------
/**
    Dynamic VHD  libvhd2/libvhd interoperability tests.
    Writes some data to the diff. VHD using legacy libvhd and checks it using  libvhd2

    @param  aFileName       Diff. VHD file name
    @param  aParentFileName name of the parent VHD file
*/
static void Do_InteropTest_Vhd_Diff_2(const char* aFileName, const char* aParentFileName, uint aTestFlags)
{
    TEST_LOG();

    int nRes;
    const uint KRndSeed1 = 0xdeadbeef;
    const uint KRndSeed2 = 0xface1734;

    TRndSequenceGen seqGen1(KRndSeed1);
    TRndSequenceGen seqGen2(KRndSeed2);

    TVhdHandle hVhd;

    vector<TSectorsBase> Sectors;

    unlink(aFileName);


    //-- 0. get parent's VHD parameters
    hVhd = VHD_Open(aParentFileName, VHDF_OPEN_RDONLY|VHDF_OPEN_DIRECTIO);
    test(hVhd >0);

    TVHD_ParamsStruct  vhdParentParams;
    nRes = VHD_Info(hVhd, &vhdParentParams);
    test_KErrNone(nRes);

    LibVhd_2_CloseVhd(hVhd);

    //-- 1. create an empty differencing VHD file (legacy libvhd)
    nRes = vhd_snapshot(aFileName, 0, aParentFileName, 0, 0);
    test_KErrNone(nRes);


    //-- 2. open file and get its geometry
    hVhd = VHD_Open(aFileName, VHDF_OPEN_RDWR|VHDF_OPEN_DIRECTIO);
    test_KErrNone(nRes);

    TVHD_ParamsStruct  vhdParams;
    nRes = VHD_Info(hVhd, &vhdParams);
    test_KErrNone(nRes);

    const uint KFileSize_Sectors = vhdParams.vhdSectors;

    test(vhdParams.vhdType == EVhd_Diff);
    test(KFileSize_Sectors > 0);
    test(KFileSize_Sectors == vhdParentParams.vhdSectors);

    //-- 3. read and verify data from the empty diff. disk (initialised in LibVhd_2_CreateVhd_Diff())  using libvhd2.
    {//-- this data chunks were created by previous test call in the parent VHD
        TFilledSectors filledSectors;
        TEmptySectors  emptySectors;

        Sectors.clear();

        //-- make a write to the last sector of the Dynamic file. It should add a block
        filledSectors.iSecStart = KFileSize_Sectors-2;
        filledSectors.iSecNum   = 2;
        Sectors.push_back(filledSectors);

        emptySectors.iSecStart = (filledSectors.iSecStart >> KDefSecPerBlockLog2) << KDefSecPerBlockLog2;
        emptySectors.iSecNum   = filledSectors.iSecStart - emptySectors.iSecStart - 1;
        Sectors.push_back(emptySectors);

        //-- make a write that crosses block#0 and block#1 boundary; this will add 2 blocks
        filledSectors.iSecStart  = 0 + KDefSecPerBlock-2;
        filledSectors.iSecNum    = 4; //-- will cross sector boundary
        Sectors.push_back(filledSectors);

        emptySectors.iSecStart   = 0;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);

        emptySectors.iSecStart   = KDefSecPerBlock+2;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);


        //-- make a write that crosses 2 block boundaries starts in block#3, spans block#4 and ends in block#5
        filledSectors.iSecStart  = 3*KDefSecPerBlock + KDefSecPerBlock-2;
        filledSectors.iSecNum    = 2 + KDefSecPerBlock +2;
        Sectors.push_back(filledSectors);

        emptySectors.iSecStart   = 3*KDefSecPerBlock;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);

        emptySectors.iSecStart   = 5*KDefSecPerBlock+2;
        emptySectors.iSecNum     = KDefSecPerBlock-2;
        Sectors.push_back(emptySectors);


        test(Sectors.size());
        seqGen1.InitRndSeed(KRndSeed1);

        nRes = LibVhd_2_CheckSectorsFill(hVhd, Sectors, seqGen1);
        test_KErrNone(nRes);

    }

    LibVhd_2_CloseVhd(hVhd);

    //-- 4. write (overwrite) some data to the differencing VHD file (legacy libvhd)

    nRes = vhd_open(&libvhd_ctx, aFileName, VHD_OPEN_RDWR | VHD_OPEN_CACHED);
    test_KErrNone(nRes);

    {//-- overwrite some data in the parent VHD file with a different random sequence (legacy libvhd)
        TFilledSectors filledSectors;

        Sectors.clear();

        //-- overwrite the last sector of the Dynamic file.
        filledSectors.iSecStart = KFileSize_Sectors-1;
        filledSectors.iSecNum   = 1;
        Sectors.push_back(filledSectors);

        //-- overwrite the first 2 sectors in block1
        filledSectors.iSecStart  = 1*KDefSecPerBlock;
        filledSectors.iSecNum    = 2;
        Sectors.push_back(filledSectors);

        //-- overwrite some sectors that cross block boundary
        filledSectors.iSecStart  = 3*KDefSecPerBlock + KDefSecPerBlock-1;
        filledSectors.iSecNum    = 2;
        Sectors.push_back(filledSectors);

        filledSectors.iSecStart  = 4*KDefSecPerBlock + 17;
        filledSectors.iSecNum    = 100;
        Sectors.push_back(filledSectors);

        filledSectors.iSecStart  = 4*KDefSecPerBlock + KDefSecPerBlock-1;
        filledSectors.iSecNum    = 2;
        Sectors.push_back(filledSectors);


        filledSectors.iSecStart  = 6*KDefSecPerBlock;
        filledSectors.iSecNum    = 25;
        Sectors.push_back(filledSectors);


        test(Sectors.size());
        seqGen2.InitRndSeed(KRndSeed2);
        for(uint i=0; i<Sectors.size(); ++i)
        {
            if(Sectors[i].Type() == TSectorsBase::EFilled)
            {
                LibVhd_WriteTestSequence(&libvhd_ctx, Sectors[i].iSecStart, Sectors[i].iSecNum, seqGen2);
            }
        }

    }

    vhd_close(&libvhd_ctx);

    //-- 5. check new data chunks in the diff. VHD and old, non-overwritten chunks from the parent (libvhd2)
    uint vhdOpenFlags = VHDF_OPEN_RDWR|VHDF_OPEN_DIRECTIO;

    if(aTestFlags & KTestFlag_CheckPureMode)
        vhdOpenFlags |= VHDF_OPMODE_PURE_BLOCKS;


    hVhd = VHD_Open(aFileName, vhdOpenFlags);
    test_KErrNone(nRes);

    //-- 5.1 check new data chunks that replaced parent's contents
    seqGen2.InitRndSeed(KRndSeed2);
    nRes = LibVhd_2_CheckSectorsFill(hVhd, Sectors, seqGen2);
    test_KErrNone(nRes);

    //-- 5.2 check data chunks that should remain intact (read from the parent VHD)
    {
        TFilledSectors filledSectors;

        seqGen1.InitRndSeed(KRndSeed1);

        Sectors.clear();
        filledSectors.iSecStart = KFileSize_Sectors-2;
        filledSectors.iSecNum   = 1;
        Sectors.push_back(filledSectors);

        nRes = LibVhd_2_CheckSectorsFill(hVhd, Sectors, seqGen1);
        test_KErrNone(nRes);

        seqGen1.SkipSequence(1*KDefSecSize);

        Sectors.clear();
        filledSectors.iSecStart = 0 + KDefSecPerBlock-2;
        filledSectors.iSecNum   = 2;
        Sectors.push_back(filledSectors);

        nRes = LibVhd_2_CheckSectorsFill(hVhd, Sectors, seqGen1);
        test_KErrNone(nRes);

        seqGen1.SkipSequence(2*KDefSecSize);

        Sectors.clear();
        filledSectors.iSecStart = 3*KDefSecPerBlock + KDefSecPerBlock-2;
        filledSectors.iSecNum   = 1;
        Sectors.push_back(filledSectors);

        nRes = LibVhd_2_CheckSectorsFill(hVhd, Sectors, seqGen1);
        test_KErrNone(nRes);


        seqGen1.SkipSequence(2*KDefSecSize);

        Sectors.clear();
        filledSectors.iSecStart = 4*KDefSecPerBlock + 1;
        filledSectors.iSecNum   = 16;
        Sectors.push_back(filledSectors);

        nRes = LibVhd_2_CheckSectorsFill(hVhd, Sectors, seqGen1);
        test_KErrNone(nRes);

        seqGen1.SkipSequence(100*KDefSecSize);

        Sectors.clear();
        filledSectors.iSecStart = 4*KDefSecPerBlock + 17 + 100;
        filledSectors.iSecNum   = KDefSecPerBlock - (17 + 100 +1);
        Sectors.push_back(filledSectors);

        nRes = LibVhd_2_CheckSectorsFill(hVhd, Sectors, seqGen1);
        test_KErrNone(nRes);

        seqGen1.SkipSequence(2*KDefSecSize);

        Sectors.clear();
        filledSectors.iSecStart = 5*KDefSecPerBlock + 1;
        filledSectors.iSecNum   = 1;
        Sectors.push_back(filledSectors);

        nRes = LibVhd_2_CheckSectorsFill(hVhd, Sectors, seqGen1);
        test_KErrNone(nRes);

    }

    LibVhd_2_CloseVhd(hVhd);
    unlink(aFileName);
}



//--------------------------------------------------------------------
/**
    initialise interoperability tests framework
*/
void InteropTest_Init()
{
    TEST_LOG();
    libvhd_set_log_level(1); //-- enable logging from the legacy libvhd
}

//--------------------------------------------------------------------
/**
    Fixed VHD  libvhd2/libvhd interoperability tests.
*/
void InteropTest_Vhd_Fixed()
{
    TEST_LOG();

    string strFileName(KVhdFilesPath); //-- test file name
    strFileName += "fixed_test.vhd";
    const char* const KFileName = strFileName.c_str();

    const uint KReqFileSize_Bytes = 6*K1MegaByte; //-- requested file size in bytes, can be slightly different after creation because CHS calculations

    Do_InteropTest_Vhd_Fixed_1(KFileName, KReqFileSize_Bytes);
    Do_InteropTest_Vhd_Fixed_2(KFileName, KReqFileSize_Bytes);

}

//--------------------------------------------------------------------
/**
    Dynamic VHD  libvhd2/libvhd interoperability tests.
*/
void InteropTest_Vhd_Dynamic()
{
    TEST_LOG();

    string strFileName(KVhdFilesPath); //-- test file name
    strFileName += "dynamic_test.vhd";
    const char* const KFileName = strFileName.c_str();

    const uint KReqFileSize_Bytes = 16*K1MegaByte; //-- requested file size in bytes, can be slightly different after creation because CHS calculations

    //-- basic interoprability tests
    Do_InteropTest_Vhd_Dynamic_1(KFileName, KReqFileSize_Bytes, 0);
    Do_InteropTest_Vhd_Dynamic_2(KFileName, KReqFileSize_Bytes, 0);

    //-- test the same operations but with VHDF_OPMODE_PURE_BLOCKS flag
    Do_InteropTest_Vhd_Dynamic_1(KFileName, KReqFileSize_Bytes, KTestFlag_CheckPureMode);
    Do_InteropTest_Vhd_Dynamic_2(KFileName, KReqFileSize_Bytes, KTestFlag_CheckPureMode);

}


//--------------------------------------------------------------------
/**
    Differencing VHD  libvhd2/libvhd interoperability tests.

*/
void InteropTest_Vhd_Diff()
{
    TEST_LOG();

    string strParentFileName(KVhdFilesPath); //-- parent file name
    strParentFileName += "dynamic_test.vhd";

    string strFileName(KVhdFilesPath); //-- test file name
    strFileName += "diff_test.vhd";
    const char* const KFileName = strFileName.c_str();

    const uint KReqFileSize_Bytes = 16*K1MegaByte; //-- requested file size in bytes, can be slightly different after creation because CHS calculations

    //-- use this test to create a dynamic VHD file with some data in it
    Do_InteropTest_Vhd_Dynamic_2(strParentFileName.c_str(), KReqFileSize_Bytes, KTestFlag_KeepFile);
    Do_InteropTest_Vhd_Diff_1(KFileName, strParentFileName.c_str(), 0);

    //-- use this test to create a dynamic VHD file with some data in it
    Do_InteropTest_Vhd_Dynamic_2(strParentFileName.c_str(), KReqFileSize_Bytes, KTestFlag_KeepFile);
    Do_InteropTest_Vhd_Diff_2(KFileName, strParentFileName.c_str(), 0);


    //-- test the same operations but with VHDF_OPMODE_PURE_BLOCKS flag


    //-- use this test to create a dynamic VHD file with some data in it
    Do_InteropTest_Vhd_Dynamic_2(strParentFileName.c_str(), KReqFileSize_Bytes, KTestFlag_KeepFile | KTestFlag_CheckPureMode);
    Do_InteropTest_Vhd_Diff_1(KFileName, strParentFileName.c_str(), KTestFlag_CheckPureMode);

    //-- use this test to create a dynamic VHD file with some data in it
    Do_InteropTest_Vhd_Dynamic_2(strParentFileName.c_str(), KReqFileSize_Bytes, KTestFlag_KeepFile | KTestFlag_CheckPureMode);
    Do_InteropTest_Vhd_Diff_2(KFileName, strParentFileName.c_str(), KTestFlag_CheckPureMode);


    unlink(strParentFileName.c_str());
}


