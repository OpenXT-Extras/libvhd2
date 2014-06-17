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
    @file  test TRIM or "discard" operations on Dynamic and differencing VHDs
*/


#include <unistd.h>
#include <stdio.h>

#include <assert.h>
#include <string.h>

#include <string>
using std::string;

#include "libvhd2_test.h"


//--------------------------------------------------------------------
/**
    Test core, common for the diff. and dynamic VHDs currently.

    @param  hVhd VHD file handle.
*/
static void DoTest_TRIM(TVhdHandle hVhd)
{
    TEST_LOG();

    int nRes;

    TVHD_ParamsStruct  params;
    memset(&params, 0, sizeof(params));

    nRes = VHD_Info(hVhd, &params);
    test_KErrNone(nRes);

    const uint32_t  KFileSizeSectors = params.vhdSectors;
    const uint32_t  KSectorsPerBlock = 1<< params.secPerBlockLog2;

    //================================================
    //-- 1. TRIMming empty file, shouldn't have any effect at all
    nRes = VHD_DiscardSectors(hVhd, 1, 1);
    test_KErrNone(nRes);

    nRes = LibVhd_2_CheckFileFill(hVhd, 1, 1, 0);
    test_KErrNone(nRes);

    //--
    nRes = VHD_DiscardSectors(hVhd, 0, KSectorsPerBlock);
    test_KErrNone(nRes);

    nRes = LibVhd_2_CheckFileFill(hVhd, 0, KSectorsPerBlock, 0);
    test_KErrNone(nRes);


    //--
    nRes = VHD_DiscardSectors(hVhd, KSectorsPerBlock+17, 4*KSectorsPerBlock);
    test_KErrNone(nRes);

    nRes = LibVhd_2_CheckFileFill(hVhd, KSectorsPerBlock+17, 4*KSectorsPerBlock, 0);
    test_KErrNone(nRes);

    //--
    nRes = VHD_DiscardSectors(hVhd, 0, KFileSizeSectors);
    test_KErrNone(nRes);

    nRes = LibVhd_2_CheckFileFill(hVhd, 0, KFileSizeSectors, 0);
    test_KErrNone(nRes);

    //================================================
    //-- 2. write several sectors; this will append 1 block; test TRIMming sectors in it and outside it.
    nRes = LibVhd_2_FillFile(hVhd, 0, 3, 'a');
    test_KErrNone(nRes);

    nRes = LibVhd_2_FillFile(hVhd, 3, 3, 'b');
    test_KErrNone(nRes);

    nRes = LibVhd_2_FillFile(hVhd, 1*KSectorsPerBlock-3, 3, 'c');
    test_KErrNone(nRes);

    //-- 2.1 discard sectors; this will meke them to be read as zeros (and reset corresponding bits in the alloc bitmap)
    nRes = VHD_DiscardSectors(hVhd, 1, 1);
    test_KErrNone(nRes);

    nRes = VHD_DiscardSectors(hVhd, 4, 1);
    test_KErrNone(nRes);

    nRes = VHD_DiscardSectors(hVhd, 1*KSectorsPerBlock-2, 1);
    test_KErrNone(nRes);

    nRes = VHD_DiscardSectors(hVhd, 4*KSectorsPerBlock, 100);
    test_KErrNone(nRes);

    //-- 2.1 check file contents before flushing and after
    for(int i=0; i<2; ++i)
    {
        nRes = LibVhd_2_CheckFileFill(hVhd, 0, 1, 'a');
        test_KErrNone(nRes);

        nRes = LibVhd_2_CheckFileFill(hVhd, 1, 1, 0);
        test_KErrNone(nRes);

        nRes = LibVhd_2_CheckFileFill(hVhd, 2, 1, 'a');
        test_KErrNone(nRes);

        nRes = LibVhd_2_CheckFileFill(hVhd, 3, 1, 'b');
        test_KErrNone(nRes);

        nRes = LibVhd_2_CheckFileFill(hVhd, 4, 1, 0);
        test_KErrNone(nRes);

        nRes = LibVhd_2_CheckFileFill(hVhd, 5, 1, 'b');
        test_KErrNone(nRes);


        nRes = LibVhd_2_CheckFileFill(hVhd, 1*KSectorsPerBlock-3, 1, 'c');
        test_KErrNone(nRes);

        nRes = LibVhd_2_CheckFileFill(hVhd, 1*KSectorsPerBlock-2, 1, 0);
        test_KErrNone(nRes);


        nRes = LibVhd_2_CheckFileFill(hVhd, 1*KSectorsPerBlock-1, 1, 'c');
        test_KErrNone(nRes);


        nRes = LibVhd_2_CheckFileFill(hVhd, 4*KSectorsPerBlock, 100, 0);
        test_KErrNone(nRes);

        nRes = VHD_Flush(hVhd);
        test_KErrNone(nRes);
    }

    //-- 2.2 TRIM whole file; this should zero-fill block0 alloc. bitmap
    nRes = VHD_DiscardSectors(hVhd, 0, KFileSizeSectors);
    test_KErrNone(nRes);

    nRes = LibVhd_2_CheckFileFill(hVhd, 0, KFileSizeSectors, 0);
    test_KErrNone(nRes);

    nRes = VHD_Flush(hVhd);
    test_KErrNone(nRes);

    nRes = LibVhd_2_CheckFileFill(hVhd, 0, KFileSizeSectors, 0);
    test_KErrNone(nRes);


    //================================================
    //-- 3. testing TRIM that spans several blocks

    //-- 3.1 write several sectors in various places; this will append blocks;
    nRes = LibVhd_2_FillFile(hVhd, KFileSizeSectors-17, 10, 'z');
    test_KErrNone(nRes);

    nRes = LibVhd_2_FillFile(hVhd, 3*KSectorsPerBlock+55, 55, 'e');
    test_KErrNone(nRes);

    nRes = LibVhd_2_FillFile(hVhd, 6*KSectorsPerBlock+5, 55, 'r');
    test_KErrNone(nRes);


    nRes = LibVhd_2_FillFile(hVhd, 1*KSectorsPerBlock, 25, 'q');
    test_KErrNone(nRes);


    nRes = LibVhd_2_FillFile(hVhd, 2*KSectorsPerBlock, KSectorsPerBlock, 'w');
    test_KErrNone(nRes);

    nRes = LibVhd_2_FillFile(hVhd, 0, KSectorsPerBlock, 'l');
    test_KErrNone(nRes);


    //-- 3.2 make TRIM that spans several blocks and check contents
    nRes = VHD_DiscardSectors(hVhd, 1*KSectorsPerBlock+2, 2*KSectorsPerBlock+60);
    test_KErrNone(nRes);

    //-- 3.3 check file contents before flushing and after
    for(int i=0; i<2; ++i)
    {
        nRes = LibVhd_2_CheckFileFill(hVhd, 0, KSectorsPerBlock, 'l');
        test_KErrNone(nRes);

        nRes = LibVhd_2_CheckFileFill(hVhd, 1*KSectorsPerBlock, 2, 'q');
        test_KErrNone(nRes);

        nRes = LibVhd_2_CheckFileFill(hVhd, 1*KSectorsPerBlock+2, 2*KSectorsPerBlock+60, 0);
        test_KErrNone(nRes);

        nRes = LibVhd_2_CheckFileFill(hVhd, 3*KSectorsPerBlock+62, 48, 'e');
        test_KErrNone(nRes);


        nRes = LibVhd_2_CheckFileFill(hVhd, 3*KSectorsPerBlock+62+48, KSectorsPerBlock-110, 0);
        test_KErrNone(nRes);

        nRes = LibVhd_2_CheckFileFill(hVhd, 4*KSectorsPerBlock, 2*KSectorsPerBlock, 0);
        test_KErrNone(nRes);

        nRes = LibVhd_2_CheckFileFill(hVhd, 6*KSectorsPerBlock, 5, 0);
        test_KErrNone(nRes);

        nRes = LibVhd_2_CheckFileFill(hVhd, 6*KSectorsPerBlock+5, 55, 'r');
        test_KErrNone(nRes);


        nRes = LibVhd_2_CheckFileFill(hVhd, KFileSizeSectors-17, 10, 'z');
        test_KErrNone(nRes);

        nRes = VHD_Flush(hVhd);
        test_KErrNone(nRes);
    }


    //-- 3.4 TRIM whole file; this should zero-fill existing blocks' alloc. bitmaps
    nRes = VHD_DiscardSectors(hVhd, 0, KFileSizeSectors);
    test_KErrNone(nRes);

    nRes = LibVhd_2_CheckFileFill(hVhd, 0, KFileSizeSectors, 0);
    test_KErrNone(nRes);

    nRes = VHD_Flush(hVhd);
    test_KErrNone(nRes);

    nRes = LibVhd_2_CheckFileFill(hVhd, 0, KFileSizeSectors, 0);
    test_KErrNone(nRes);


}



//--------------------------------------------------------------------
/**
    Test TRIM operations on a Dynamic file
*/
static void TestTRIM_VHD_Dynamic()
{
    TEST_LOG();

    //-- 1. create dynamic VHD
    std::string strFileName = KVhdFilesPath;
    strFileName += "!!Dynamic_New.vhd";
    const char* fileName = strFileName.c_str();

    int nRes;
    TVhdHandle hVhd;

    unlink(fileName);

    TVHD_ParamsStruct  params;
    memset(&params, 0, sizeof(params));

    params.vhdType = EVhd_Dynamic;
    params.vhdSectors = 16*K1MegaByte / 512;
    params.vhdFileName = fileName;

    hVhd = VHD_Create(&params);
    test(hVhd >0);
    VHD_Close(hVhd);

    //-- 1.1 test VHD open flags compatibility
    //-- open VHD file w/o enabling TRIM API.
    hVhd = VHD_Open(fileName, VHDF_OPEN_RDWR | VHDF_OPMODE_PURE_BLOCKS);
    test(hVhd >0);

    nRes = VHD_DiscardSectors(hVhd, 0, 1); //-- should fail
    test(nRes == KErrNotSupported);
    VHD_Close(hVhd);


    //-- open VHD with incompatible flags
    hVhd = VHD_Open(fileName, VHDF_OPEN_RDWR | VHDF_OPMODE_PURE_BLOCKS |VHDF_OPEN_ENABLE_TRIM);
    test(hVhd == KErrArgument); //-- should fail
    //VHD_Close(hVhd);

    //-- open VHD with incompatible flags
    hVhd = VHD_Open(fileName, VHDF_OPEN_RDWR |VHDF_OPEN_ENABLE_TRIM);
    test(hVhd >0); //-- should be OK

    //-- 2. Do TRIM tests
    DoTest_TRIM(hVhd);


    //-- 3. close and delete the file
    VHD_Close(hVhd);
    unlink(fileName);
}

//--------------------------------------------------------------------
/**
    Test TRIM operations on a differencing file
*/
static void TestTRIM_VHD_Diff()
{
    TEST_LOG();

    //-- for the current implementation of TRIM on differencing VHDs reuse test code from
    //-- testing Dinamic VHDs trimming.
    //-- for this create an _empty_ Dynamic parent VHD. Discarded in Diff VHD sectors will be read from the parent, which
    //-- is the empty dynamic VHD, returning all zeros. Thus, the effect of reading discaded data will be the same for Dynamic & Diff VHD types.
    //-- later on this behaviour can change along with the test code.

    int nRes;
    TVhdHandle hVhd;


    //-- 1. create empty dynamic VHD, this will be a parent
    std::string strParentFileName = KVhdFilesPath;
    strParentFileName += "!!Dynamic_New.vhd";
    const char* fileNameParent = strParentFileName.c_str();
    unlink(fileNameParent);

    TVHD_ParamsStruct  params;
    memset(&params, 0, sizeof(params));


    params.vhdType = EVhd_Dynamic;
    params.vhdSectors = 16*K1MegaByte / 512;
    params.vhdFileName = fileNameParent;

    hVhd = VHD_Create(&params);
    test(hVhd >0);
    VHD_Close(hVhd);


    //-- 2. create differencing VHD to work with
    std::string strFileName = KVhdFilesPath;
    strFileName += "!!Diff_New.vhd";
    const char* fileName = strFileName.c_str();
    unlink(fileName);

    memset(&params, 0, sizeof(params));
    params.vhdType = EVhd_Diff;
    params.vhdFileName = fileName;
    params.vhdParentName = fileNameParent;

    hVhd = VHD_Create(&params);
    test(hVhd >0);
    VHD_Close(hVhd);

    //-- 1.1 test VHD open flags compatibility
    //-- open VHD file w/o enabling TRIM API.
    hVhd = VHD_Open(fileName, VHDF_OPEN_RDWR | VHDF_OPMODE_PURE_BLOCKS);
    test(hVhd >0);

    nRes = VHD_DiscardSectors(hVhd, 0, 1); //-- should fail
    test(nRes == KErrNotSupported);
    VHD_Close(hVhd);


    //-- open VHD with incompatible flags
    hVhd = VHD_Open(fileName, VHDF_OPEN_RDWR | VHDF_OPMODE_PURE_BLOCKS |VHDF_OPEN_ENABLE_TRIM);
    test(hVhd == KErrArgument); //-- should fail
    //VHD_Close(hVhd);

    //-- open VHD with incompatible flags
    hVhd = VHD_Open(fileName, VHDF_OPEN_RDWR |VHDF_OPEN_ENABLE_TRIM);
    test(hVhd >0); //-- should be OK


    //-- 2. Do TRIM tests
    DoTest_TRIM(hVhd);

    //-- 3. close and delete files
    VHD_Close(hVhd);
    unlink(fileName);
    unlink(fileNameParent);
}




//--------------------------------------------------------------------
/** Execute TRIM tests */
void TrimTests_Execute()
{
    TEST_LOG();
    TestTRIM_VHD_Dynamic();
    TestTRIM_VHD_Diff();
}




