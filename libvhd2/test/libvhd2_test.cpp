/*
 * Copyright (c) 2013 Citrix Systems, Inc.
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

#include <unistd.h>
#include <stdio.h>
//#include <stdlib.h>
//#include <syslog.h>
#include <assert.h>
#include <string.h>

#include <string>
using std::string;

#include "libvhd2_test.h"

//#include "../include/libvhd2.h"

//--------------------------------------------------------------------
void TestCreate_VHD_Fixed()
{
    TVhdHandle hVhd;

    uint32_t flags = VHDF_OPEN_RDONLY;

    TVHD_ParamsStruct  params;
    memset(&params, 0, sizeof(params));


    params.vhdType = EVhd_Fixed;

    //params.vhdSectors = 1024;
    params.vhdDiskGeometry.Cylinders  = 963;
    params.vhdDiskGeometry.Heads      = 8;
    params.vhdDiskGeometry.SecPerTrack= 17;
    params.vhdFileName = "/home/dmitryl/Development/vhd_files/!!Fixed_New.vhd";

    hVhd = VHD_Create(&params);

    VHD_Info(hVhd, &params);

    VHD_Close(hVhd);
}


//--------------------------------------------------------------------
void TestCreate_VHD_Dynamic()
{
    int nRes;
    TVhdHandle hVhd;

    char buf[16384];
    memset(buf, 'Z', sizeof (buf));

    TVHD_ParamsStruct  params;
    memset(&params, 0, sizeof(params));

    const char fileName[] =  "/home/dmitryl/Development/vhd_files/!!Dynamic_New.vhd";


  #if 0

    unlink(fileName);

    uint32_t flags = VHDF_OPEN_RDONLY;



    params.vhdType = EVhd_Dynamic;

    //params.vhdSectors = 64*1048576/512;
    params.vhdSectors = 16*K1MegaByte / 512;
    params.vhdModeFlags = VHDF_OPEN_RDWR;
/*
    params.vhdDiskGeometry.Cylinders  = 481;
    params.vhdDiskGeometry.Heads      = 4;
    params.vhdDiskGeometry.SecPerTrack= 17;
*/

    //-- 1. create an empty dyn. VHD
    hVhd = VHD_Create(fileName, &params);
    VHD_Info(hVhd, &params);

    //-- 2. make several random writes to have blocks allocated + clean/dirty sectors
    nRes = VHD_WriteSectors(hVhd, params.vhdSectors-1, 1, buf, sizeof(buf));
    test(nRes==1);

    VHD_Close(hVhd);
 #endif

    //-- open the file with VHDF_OPMODE_PURE_BLOCKS flag

    hVhd = VHD_Open(fileName, VHDF_OPEN_RDWR | VHDF_OPMODE_PURE_BLOCKS);
    test(hVhd >0);

    VHD_Info(hVhd, &params);

    memset(buf, 'a', sizeof (buf));
    //nRes = VHD_WriteSectors(hVhd, params.vhdSectors-2, 1, buf, sizeof(buf));

    nRes = VHD_WriteSectors(hVhd, 0, 1, buf, sizeof(buf));
    test(nRes==1);


    VHD_Close(hVhd);

}


//--------------------------------------------------------------------
void TestCreate_VHD_Differencing()
{
    int nRes;

    char buf[16384];
    memset(buf, 'Z', sizeof (buf));


    TVhdHandle hVhd;
    //uint32_t flags = VHDF_OPEN_RDWR;

    TVHD_ParamsStruct  params;
    memset(&params, 0, sizeof(params));

    const char fileNameParent[] = "/home/dmitryl/Development/vhd_files/Differencing/Hard_Disk_variable.vhd";
    const char fileName[] =  "/home/dmitryl/Development/vhd_files/!!Diff_New.vhd";
    const char fileName1[] =  "/home/dmitryl/Development/vhd_files/!!Diff_New1.vhd";

    params.vhdFileName = fileName;
    params.vhdModeFlags = VHDF_OPEN_RDWR;
    params.vhdType = EVhd_Diff;
    params.vhdDiskGeometry.Cylinders  = 1;

    //params.vhdDiskGeometry.Cylinders  = 963;
    //params.vhdDiskGeometry.Heads      = 8;
    //params.vhdDiskGeometry.SecPerTrack= 17;

    params.vhdParentName = fileNameParent ;

unlink(fileName);
unlink(fileName1);

    hVhd = VHD_Create(&params);
    test(hVhd >0);

    nRes = VHD_Info(hVhd, &params);
    test_KErrNone(nRes);

    //-- 2. make several random writes to have blocks allocated + clean/dirty sectors
    nRes = VHD_WriteSectors(hVhd, params.vhdSectors-1, 1, buf, sizeof(buf));
    test(nRes==1);

    VHD_Close(hVhd);

//~~~~~~~~~~~~~~~~~
    params.vhdFileName = fileName1;
    params.vhdModeFlags = VHDF_OPEN_RDWR;
    params.vhdType = EVhd_Diff;
    params.vhdParentName = fileName;

    hVhd = VHD_Create(&params);
    test(hVhd >0);

    nRes = VHD_Info(hVhd, &params);
    test_KErrNone(nRes);

    nRes = VHD_ReadSectors(hVhd, params.vhdSectors-3, 3, buf, sizeof(buf));
    test(nRes==3);

/*
    int i;
    for(i=0; ; ++i)
    {
        if(VHD_ParentInfo(hVhd, &params, i) !=KErrNone)
            break;
    }
*/
VHD_Close(hVhd);
//~~~~~~~~~~~~~~~~~

    //-- open the file with VHDF_OPMODE_PURE_BLOCKS flag


    //hVhd = VHD_Open(fileName, VHDF_OPEN_RDWR | VHDF_OPMODE_PURE_BLOCKS);
    hVhd = VHD_Open(fileName1, VHDF_OPEN_RDWR );
    test(hVhd >0);

    //VHD_Info(hVhd, &params);

    int i;

    for(i=0; ; ++i)
    {
        if(VHD_ParentInfo(hVhd, &params, i) !=KErrNone)
            break;
    }



    nRes = VHD_ReadSectors(hVhd, params.vhdSectors-3, 3, buf, sizeof(buf));
    test(nRes==3);


    memset(buf, 'a', sizeof (buf));
    nRes = VHD_WriteSectors(hVhd, params.vhdSectors-2, 1, buf, sizeof(buf));
    test(nRes==1);

    memset(buf, 'b', sizeof (buf));
    nRes = VHD_WriteSectors(hVhd, 0, 1, buf, sizeof(buf));
    test(nRes==1);

    VHD_Close(hVhd);
}


static void Do_Interop_Tests()
{
    InteropTest_Init();
    InteropTest_Vhd_Fixed();
    InteropTest_Vhd_Dynamic();
    InteropTest_Vhd_Diff();
}




//~~~~~~~~~~~~~~~~~~~~~~~~~~~~

int main(int argc, char *argv[])
{

    TrimTests_Execute();


    //---------------------------------------
    /*
    int nRes;
    TVhdHandle hVhd;


    const char KFName[] = "/home/dmitryl/Development/logs/ndvm.vhd";

    uint32_t flags = VHDF_OPEN_RDONLY;

    hVhd = VHD_Open(KFName, flags );
    test(hVhd >0 );


    char buf[65536];

    VHD_PrintInfo(hVhd, buf, sizeof(buf));
    printf(buf);


    nRes = VHD_ReadSectors(hVhd, 637824, 8, buf, sizeof(buf));


    VHD_Close(hVhd);
    */

    //---------------------------------------

 #if 1
    CoalesceTests_Init();
    CoalesceTests_Execute();
    Tests_Cleanup();
 #endif

    Do_Interop_Tests();
    Tests_Cleanup();

    //-- todo: check double close, duble flush, invalidate & flush, etc


    //TestCreate_VHD_Fixed();
    //TestCreate_VHD_Dynamic();
    //TestCreate_VHD_Differencing();

/*

    int nRes;
    TVhdHandle hVhd1;

    uint32_t flags = VHDF_OPEN_RDONLY;

   // hVhd1 = VHD_Create("/home/dmitryl/Development/vhd_files/!!Dyn_New.vhd", &params);
    //hVhd1 = VHD_Open("/home/dmitryl/Development/vhd_files/Dynamic_DOS.vhd", flags );

    //TVhdHandle hVhd2 = VHD_Open("/home/dmitryl/Development/vhd_files/Fixed_DOS.vhd", flags | VHD_OPEN_RDWR);

//getchar();

    //VHD_Close(hVhd2);

    //hVhd1 = VHD_Open("/home/dmitryl/Development/vhd_files/Fixed_DOS.vhd", flags);
    //hVhd1 = VHD_Open("/home/dmitryl/Development/vhd_files/Dynamic_DOS.vhd", VHD_OPEN_CACHED);

    hVhd1 = VHD_Open("/home/dmitryl/Development/vhd_files/Differencing/Diff1_NonEmpty.vhd", flags | VHDF_OPEN_RDWR);

    //hVhd1 = VHD_Open("/home/dmitryl/Development/vhd_files/Differencing/Diff1_Empty.vhd", flags | VHD_OPEN_RDWR);

    //hVhd1 = VHD_Open("/home/dmitryl/Development/vhd_files/!!Diff_New.vhd", flags | VHD_OPEN_RDWR);

    //hVhd1 = VHD_Open("/home/dmitryl/Development/vhd_files/Differencing/Diff1_Empty.vhd", flags | VHD_OPEN_IGNORE_PARENT);




    char buf[16384];
    memset(buf, 'Z', sizeof (buf));

    //TVHD_Info param;
    //VHD_Info(hVhd1, &param);

    //VHD_PrintInfo(hVhd1, buf, sizeof(buf));
    //printf(buf);

    nRes = VHD_ReadSectors(hVhd1, 17, 5, buf, sizeof(buf));
    //nRes = VHD_ReadSectors(hVhd1, 4095, 3, buf, sizeof(buf));

//    nRes = VHD_ReadSectors(hVhd1, 0, 3, buf, sizeof(buf));

    //nRes = VHD_WriteSectors(hVhd1, 0, 2, buf, sizeof(buf));

    //nRes = VHD_WriteSectors(hVhd1, 2, 3, buf, sizeof(buf));

    nRes = VHD_WriteSectors(hVhd1, 4095, 3, buf, sizeof(buf));

    //int nRes = VHD_ReadSectors(hVhd1, 4095, 3, buf, sizeof(buf));
    //int nRes = VHD_ReadSectors(hVhd1, 1, 4096+4, buf, 100*1048576); //!! fake


    VHD_Close(hVhd1);
*/


   return 0;
}




