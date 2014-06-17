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

    @file imlementation of the routines for creating VHD files
*/


#include <fcntl.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>

//#include <memory>
//using std::auto_ptr;

#include "vhd.h"


//--------------------------------------------------------------------
static void DoConvertUnixPathToWin(std::string& aPath)
{
    for(size_t i=0; i<aPath.length(); ++i)
    {//-- replace all DOS slashes to Unix ones
        if(aPath.at(i) == KPathDelim)
            aPath.at(i) = '\\';
    }
}


//--------------------------------------------------------------------
/**
    Converts 2 absolute file pathses into one relative to another.

    @param  aRealPathBase   fully-qualified real path to the reference ("Base") file
    @param  aRealPathDest   fully-qualified real path to the destination file
    @param  aRelPath        out: relative path from "aRealPathBase" to "aRealPathDest"

    For example:    base path,      dest path,      resulting relative path (base->dest)
                    /a/b/c/z.txt    /a/d/e.txt      ../../d/e.txt
                    /a/d/e.txt      /a/b/c/z.txt    ../b/c/z.txt
                    /a/b/c/z.txt    /a/b/c/e.txt    ./e.txt
*/
static void AbsolutePathToRelative(const char* aRealPathBase, const char* aRealPathDest, std::string& aRelPath)
{
    //-- the fact that parameters are real pathes to files simplifies implementation quite a lot.
    std::string strSrc (aRealPathBase);
    std::string strDest(aRealPathDest);
    aRelPath.clear();

    const char* pDestFile = NULL; //-- points to the leaf destination file name

    {//-- get rid of file names, leaving directories only
    size_t pos;
    pos = strSrc.rfind(KPathDelim);
    ASSERT(pos != std::string::npos);
    strSrc.erase(pos+1);

    pos = strDest.rfind(KPathDelim);
    ASSERT(pos != std::string::npos);
    strDest.erase(pos+1);
    pDestFile = aRealPathDest+pos+1;


    //-- get rid of the leading slashes indicating root dir.
    ASSERT(strSrc[0] == KPathDelim && strDest[0] == KPathDelim);
    strSrc.erase(0,1);
    strDest.erase(0,1);
    }

    //-- 2. skip common subdirectories from the left
    for(;;)
    {
        const size_t posSrc  = strSrc.find(KPathDelim);
        const size_t posDest = strDest.find(KPathDelim);

        if(posSrc == std::string::npos || posDest == std::string::npos || posSrc != posDest)
            break;

        if(strSrc.compare(0, posSrc, strDest, 0, posDest) != 0)
            break;

        //-- delete matching subdir
        strSrc.erase(0,  posSrc+1);
        strDest.erase(0, posDest+1);
    }

    if(strSrc.empty())
    {
        aRelPath = KCurrDir;
    }
    else
    {//-- add references to the parent directory as many times, as many subdirectories in the strSrc
        for(size_t pos=0;;)
        {
            pos = strSrc.find(KPathDelim, pos);
            if(pos == std::string::npos)
                break;

            ++pos;
            aRelPath += KParentDir;
        }
    }

    aRelPath += strDest;   //-- add destination directory's right part that differs from the source
    aRelPath += pDestFile; //-- add destination file name

}

//--------------------------------------------------------------------
/**
	Write a number of bytes to the file.
	@param  aFd             file descriptor
	@param	aStartPos	    starting position.
	@param	aBytesToWrite	number of bytes to write
	@param	apData		in: data to write

	@return	KErrNone on success, negative error code otherwise.
*/
static int DoWriteData(int aFd, uint64_t aStartPos, uint32_t aBytesToWrite, const void* apData)
{
    DBG_LOG("Fd:%d, aStartPos:%lld, aLen:%d", aFd, aStartPos, aBytesToWrite);

    const ssize_t bytesWritten = pwrite64(aFd, apData, aBytesToWrite, aStartPos);

    if(bytesWritten != (ssize_t)aBytesToWrite)
    {
        const int nRes = -errno;
        DBG_LOG("Error writing a file!, code:%d", nRes);
        return nRes;
    }

    return KErrNone;
}


//--------------------------------------------------------------------
/**
    fill a portion of a file with some byte pattern.
    @param  aFd         file descriptor
    @param  aStartPos   Start position of the region to be filled
    @param  aLen        number of bytes to fill
    @param	aFill   	filling byte

    @return standard error code
*/
static int DoFillMedia(int aFd, uint64_t aStartPos, uint64_t aLen, uint8_t aFill)
{
    DBG_LOG("Fd:%d, aStartPos:%lld, aLen:%lld", aFd, aStartPos, aLen);

    ASSERT(aFd > 0);
    uint64_t remBytes =  aLen;

    const uint32_t KMaxBufSize = KDefScratchBufSize;    //-- max. buffer size for media filling
    const uint32_t KBufSize = Min(remBytes, (uint64_t)KMaxBufSize);

    CDynBuffer buf(KBufSize);
    buf.Fill(aFill);

    while(remBytes)
    {
        const int32_t bytesToWrite = Min((uint64_t)KBufSize, remBytes);
        const int nRes = DoWriteData(aFd, aStartPos, bytesToWrite, buf.Ptr());
        if(nRes != KErrNone)
            return nRes;

        aStartPos += bytesToWrite;
        remBytes  -= bytesToWrite;
    }

    return KErrNone;
}

//--------------------------------------------------------------------

/**
    Specialised function that generates Fixed VHD file layout.

    @param  aFd         file descriptor
    @param  aParams     VHD creation parameters.
    @param  aFooter     pre-populated VHD footer, see the caller. out: fully populated and correct footer.

    @return KErrNone if everything is OK, negative error code othewise.
*/
int DoGenerateVHD_Fixed(int aFd, const TVHD_Params& aParams, TVhdFooter& aFooter)
{
    ASSERT(aFooter.iDiskType == EVhd_Fixed);

    aFooter.iDataOffset = ULLONG_MAX;

    //-- for some reason even fixed VHD size is rounded up to a (2MB default) block size
    const uint32_t diskSizeInSectors = RoundUp_ToGranularity(aFooter.ChsToSectors(aFooter.iDiskGeometry), aParams.secPerBlockLog2);
    const uint64_t diskSize = ((uint64_t)diskSizeInSectors) << aParams.secSizeLog2;

    aFooter.iOrgSize = diskSize;
    aFooter.iCurrSize= diskSize;

    //-- finish up footer. After externalisation it must be valid
    uint8_t buf[TVhdFooter::KSize];
    aFooter.Externalise(buf, true);
    ASSERT(aFooter.IsValid());

    //-- write the footer to the last sector of the file, this will expand file to necessary size
    const int nRes = DoWriteData(aFd, diskSize, TVhdFooter::KSize, buf);
    if(nRes != KErrNone)
        return nRes;

    //-- zero-fill the file if required
    if(aParams.vhdModeFlags & VHDF_CREATE_FIXED_NO_ZERO_FILL)
        return KErrNone;


    return DoFillMedia(aFd, 0, diskSize, 0);
}

//--------------------------------------------------------------------
/**
    Specialised function that generates Dynamic VHD file layout.

    @param  aFd         file descriptor
    @param  aParams     VHD creation parameters.
    @param  aFooter     pre-populated VHD footer, see the caller. out: fully populated and correct footer.

    @return KErrNone if everything is OK, negative error code othewise.
*/
int DoGenerateVHD_Dynamic(int aFd, TVHD_Params& aParams, TVhdFooter& aFooter)
{
    ASSERT(aFooter.iDiskType == EVhd_Dynamic);

    const uint32_t KHdrStartSec = 1; //-- VHD header start sector
    const uint32_t KBatStartSec = 3; //-- BAT start sector

    aFooter.iDataOffset = KHdrStartSec << aParams.secSizeLog2; //-- VHD header offset

    {//-- Round up VHD file size to the Block size (2MB by default)
        const uint32_t diskSizeInSectors = RoundUp_ToGranularity(aFooter.ChsToSectors(aFooter.iDiskGeometry), aParams.secPerBlockLog2);
        const uint64_t diskSize = ((uint64_t)diskSizeInSectors) << aParams.secSizeLog2;

        aFooter.iOrgSize = diskSize;
        aFooter.iCurrSize= diskSize;
    }

    //-- 1. populate VHD Header with the specified parameters.
    TVhdHeader vhdHeader;
    if(!vhdHeader.Init(aParams))
    {
        DBG_LOG("invalid Header parameters!");
        return KErrArgument;
    }

    vhdHeader.iBatOffset = KBatStartSec << aParams.secSizeLog2; //-- BAT Offset

    //-- 2. calculate BAT parameters
    ASSERT(vhdHeader.iMaxBatEntries);
    const uint32_t KBatSizeInSectors = 1 + ((vhdHeader.iMaxBatEntries - 1) >> aParams.secSizeLog2); //-- rounded-up multiples of sector
    const uint32_t KBatFillBytes = vhdHeader.iMaxBatEntries * sizeof(TBatEntry); //-- amount of bytes to fil with 0xFF, "unallocated" BAT entries.

    int nRes;

    //-- 3. write footer, its copy and header
    CDynBuffer buf(TVhdFooter::KSize);

    aFooter.Externalise(buf.Ptr(), true);
    ASSERT(aFooter.IsValid());

    //-- 3.1 footer copy, beginning of the file
    nRes = DoWriteData(aFd, 0, TVhdFooter::KSize, buf.Ptr());
    if(nRes != KErrNone)
        return nRes;

    //-- 3.2 footer, last sector of the file
    const uint32_t KFooterSec = KBatStartSec + KBatSizeInSectors;
    nRes = DoWriteData(aFd, KFooterSec << aParams.secSizeLog2, TVhdFooter::KSize, buf.Ptr());
    if(nRes != KErrNone)
        return nRes;

    //-- 3.3 header
    buf.Resize(TVhdHeader::KSize);
    vhdHeader.Externalise(buf.Ptr(), true);
    ASSERT(vhdHeader.IsValid());

    nRes = DoWriteData(aFd, KHdrStartSec << aParams.secSizeLog2, TVhdHeader::KSize, buf.Ptr());
    if(nRes != KErrNone)
        return nRes;

    //-- 4. write BAT. zero-fill whole BAT and then write FFs to the area occupied by BAT entries
    nRes = DoFillMedia(aFd, KBatStartSec << aParams.secSizeLog2, KBatSizeInSectors << aParams.secSizeLog2, 0);
    if(nRes != KErrNone)
        return nRes;

    nRes = DoFillMedia(aFd, KBatStartSec << aParams.secSizeLog2, KBatFillBytes, 0xFF);
    if(nRes != KErrNone)
        return nRes;

    return KErrNone;

}



//--------------------------------------------------------------------
/**
    Generate parent locator entry and parent locator data.

    @param  aThisFileName   fully-qualified absolute path to this VHD file
    @param  aParentFileName fully-qualified absolute path to the parent VHD file
    @param  aLocatorEntry   locator entry object. aLocatorEntry.iCode must contain supported locator code to be used to generate othe information
    @param  aLocatorData    out: buffer, containit locator data; its size is a multiple of a sector size

    @return KErrNone on success
*/
int GenerateParentLocator(const char* aThisFileName, const char* aParentFileName, TParentLocatorEntry& aLocatorEntry, CDynBuffer& aLocatorData)
{

    const uint KSectorSizeLog2 = KDefSecSizeLog2;

    //-- work out path to parent VHD file
    std::string strParentPath_Abs(aParentFileName); //-- ASCII absolute path to parent
    std::string strParentPath_Rel;                  //-- ASCII relative path to parent

    AbsolutePathToRelative(aThisFileName, aParentFileName, strParentPath_Rel);

    DoConvertUnixPathToWin(strParentPath_Abs);
    DoConvertUnixPathToWin(strParentPath_Rel);

    uint32_t    dataSpace;
    uint32_t    dataLen;
    int nRes;

    switch(aLocatorEntry.PlatCode())
    {
     case TParentLocatorEntry::EPlatCode_WI2R:
        //-- Wi2r locator, ASCII relative path
        dataLen = strParentPath_Rel.length();
        dataSpace = RoundUp_ToGranularity(dataLen, KSectorSizeLog2);
        aLocatorData.Resize(dataSpace);
        aLocatorData.FillZ();
        aLocatorData.Copy(0, dataLen, strParentPath_Rel.c_str());
     break;

     case TParentLocatorEntry::EPlatCode_WI2K:
        //-- Wi2k locator, ASCII absolute path
        dataLen = strParentPath_Abs.length();
        dataSpace = RoundUp_ToGranularity(dataLen, KSectorSizeLog2);
        aLocatorData.Resize(dataSpace);
        aLocatorData.FillZ();
        aLocatorData.Copy(0, dataLen, strParentPath_Abs.c_str());
     break;

     case TParentLocatorEntry::EPlatCode_W2RU:
        //-- W2ru locator, UTF16 LE relative path
        aLocatorData.Resize(8*K1KiloByte);
        nRes = ASCII_to_UNICODE(strParentPath_Rel.c_str(), aLocatorData.Ptr(), aLocatorData.Size(), dataLen, EUTF_16LE);
        if(nRes != KErrNone)
            return KErrBadName;

        dataSpace = RoundUp_ToGranularity(dataLen, KSectorSizeLog2);
     break;

     case TParentLocatorEntry::EPlatCode_W2KU:
        //-- W2ku locator, UTF16 LE absolute path
        aLocatorData.Resize(8*K1KiloByte);
        nRes = ASCII_to_UNICODE(strParentPath_Abs.c_str(), aLocatorData.Ptr(), aLocatorData.Size(), dataLen, EUTF_16LE);
        if(nRes != KErrNone)
            return KErrBadName;

        dataSpace = RoundUp_ToGranularity(dataLen, KSectorSizeLog2);

     break;

     default:
     DBG_LOG("Unsupported Parent Locator type: %d!", aLocatorEntry.PlatCode());
     return KErrNotSupported;

    };// switch(aLocatorType)

    aLocatorEntry.SetDataLen(dataLen);
    aLocatorEntry.SetDataSpace(dataSpace);

    return KErrNone;
}




//--------------------------------------------------------------------
/**
    Specialised function that generates Differencing VHD file layout.

    @param  aFd         file descriptor
    @param  aParams     VHD creation parameters.
    @param  aFooter     pre-populated VHD footer, see the caller. out: fully populated and correct footer.
    @param  aFileName   file name of the file being created

    @return KErrNone if everything is OK, negative error code othewise.
*/
int DoGenerateVHD_Differencing(int aFd, TVHD_Params& aParams, TVhdFooter& aFooter)
{
    ASSERT(aFooter.iDiskType == EVhd_Diff);

    int nRes;

    const uint32_t KSectorSizeLog2 = aParams.secSizeLog2;
    const uint32_t KHdrStartSec = 1; //-- VHD header start sector
    const uint32_t KBatStartSec = 3; //-- BAT start sector

    //-- check VHD parent file. It must be accessible. Open it to retrieve the necessary parameters
    //-- CAutoClosePtr will provide calling pVhdParent->Close() and deletion upon leaving the scope.
    CAutoClosePtr<CVhdFileBase> pVhdParent (CVhdFileBase::CreateFromFile(aParams.vhdParentName, VHDF_OPEN_RDONLY, nRes));
    if(!pVhdParent.get())
    {
        DBG_LOG("Can't open the parent VHD file!");
        return KErr_VhdDiff_NoParent;
    }

    nRes = pVhdParent->Open();
    if(nRes != KErrNone)
        return nRes;

    //-- get all necessary parameters from the parent (Parent Name, UUID, geometry)
    ASSERT(aParams.vhdSectors == 0 && aParams.vhdDiskGeometry.Cylinders == 0);

    aFooter.iDiskGeometry = pVhdParent->Footer().DiskGeometry();//-- diff. VHD inherits parent's geometry
    aFooter.iDataOffset = KHdrStartSec << KSectorSizeLog2;      //-- VHD header offset

    aParams.vhdSectors = aFooter.ChsToSectors(aFooter.iDiskGeometry);

    {//-- Round up VHD file size to the Block size (2MB by default)
        const uint32_t diskSizeInSectors = RoundUp_ToGranularity(aFooter.ChsToSectors(aFooter.iDiskGeometry), aParams.secPerBlockLog2);
        const uint64_t diskSize = ((uint64_t)diskSizeInSectors) << KSectorSizeLog2;

        aFooter.iOrgSize = diskSize;
        aFooter.iCurrSize= diskSize;
    }

    //-- populate VHD Header with required parameters.

    if(pVhdParent->VhdType() != EVhd_Fixed)
    {//-- inherit block size from the parent, if it is variable  or differencing VHD
        const CVhdDynDiffBase* p = (const CVhdDynDiffBase*)pVhdParent.get();
        aParams.secPerBlockLog2 = p->SectorsPerBlockLog2();
    }
    else
    {
        aParams.secPerBlockLog2 = KDefSecPerBlockLog2; //-- parent is a fixed VHD
    }


    TVhdHeader vhdHeader;
    if(!vhdHeader.Init(aParams))
    {
        DBG_LOG("invalid Header parameters!");
        return KErrArgument;
    }

    vhdHeader.iBatOffset = KBatStartSec << aParams.secSizeLog2; //-- BAT Offset

    uuid_copy(vhdHeader.iParent_UUID, pVhdParent->Footer().UUID()); //-- parent's UUID
    vhdHeader.iParentTimeStamp = pVhdParent->Footer().TimeStamp();  //-- parent's time stamp

    //-- encode parent's name in UTF16, Big Endian
    size_t uLen;
    nRes = ASCII_to_UNICODE(pVhdParent->FileName(), vhdHeader.iParentUName, TVhdHeader::KPNameLen_bytes, uLen, EUTF_16BE);
    if(nRes != KErrNone)
    {
        return KErrBadName;
    }

    //-- calculate BAT parameters
    ASSERT(vhdHeader.iMaxBatEntries);
    const uint32_t KBatSizeInSectors = 1 + ((vhdHeader.iMaxBatEntries - 1) >> KSectorSizeLog2); //-- rounded-up multiples of sector
    const uint32_t KBatFillBytes = vhdHeader.iMaxBatEntries * sizeof(TBatEntry); //-- amount of bytes to fil with 0xFF, "unallocated" BAT entries.


    CDynBuffer buf;

    //-- start sector of the parent locators area. reserve 1 sector jsut in case
    const uint32_t KLocatorsStartSec =  KBatStartSec + KBatSizeInSectors + 1;
    uint32_t locatorSectors = 0; //-- total amount of sectors taken by parent locators

    //-- create  Wi2r, Wi2k, W2ru, W2ku parent locators
    //-- create locator entries at the end of the array. On VHD opening they are scanned from the top; easier to change something later (add a locator that will override existing)
    {
        //-- get fully-qualified absolute path to this file
        buf.Resize(PATH_MAX);
        const char* p = realpath(aParams.vhdFileName, (char*)buf.Ptr());
        ASSERT(p)
        std::string strThisFilePath(p);

        const char* KThisFilePath = strThisFilePath.c_str(); //-- fully-qualified absolute path to this file
        const char* KParentPath   = pVhdParent->FilePath();  //-- fully-qualified absolute path to the parent file file

        TParentLocatorEntry* pLocEntry;


        //-- write Wi2r locator [7], ASCII relative path
        pLocEntry = &vhdHeader.iParentLoc[7];
        pLocEntry->Init(TParentLocatorEntry::EPlatCode_WI2R);
        nRes = GenerateParentLocator(KThisFilePath, KParentPath, *pLocEntry, buf);
        if(nRes != KErrNone)
            return nRes;

        pLocEntry->SetDataOffset((KLocatorsStartSec + locatorSectors) << KSectorSizeLog2);
        locatorSectors += (pLocEntry->DataSpace() >> KSectorSizeLog2);

        nRes = DoWriteData(aFd, pLocEntry->DataOffset(), pLocEntry->DataSpace(), buf.Ptr());
        if(nRes != KErrNone)
            return nRes;


        //-- write Wi2k locator [6], ASCII absolute path
        pLocEntry = &vhdHeader.iParentLoc[6];
        pLocEntry->Init(TParentLocatorEntry::EPlatCode_WI2K);
        nRes = GenerateParentLocator(KThisFilePath, KParentPath, *pLocEntry, buf);
        if(nRes != KErrNone)
            return nRes;

        pLocEntry->SetDataOffset((KLocatorsStartSec + locatorSectors) << KSectorSizeLog2);
        locatorSectors += (pLocEntry->DataSpace() >> KSectorSizeLog2);

        nRes = DoWriteData(aFd, pLocEntry->DataOffset(), pLocEntry->DataSpace(), buf.Ptr());
        if(nRes != KErrNone)
            return nRes;


        //-- write W2ru locator [5], UTF16 LE relative path
        pLocEntry = &vhdHeader.iParentLoc[5];
        pLocEntry->Init(TParentLocatorEntry::EPlatCode_W2RU);

        nRes = GenerateParentLocator(KThisFilePath, KParentPath, *pLocEntry, buf);
        if(nRes != KErrNone)
            return nRes;

        pLocEntry->SetDataOffset((KLocatorsStartSec + locatorSectors) << KSectorSizeLog2);
        locatorSectors += (pLocEntry->DataSpace() >> KSectorSizeLog2);

        nRes = DoWriteData(aFd, pLocEntry->DataOffset(), pLocEntry->DataSpace(), buf.Ptr());
        if(nRes != KErrNone)
            return nRes;


        //-- write W2ku locator [4] UTF16 LE absolute path
        pLocEntry = &vhdHeader.iParentLoc[4];
        pLocEntry->Init(TParentLocatorEntry::EPlatCode_W2KU);

        nRes = GenerateParentLocator(KThisFilePath, KParentPath, *pLocEntry, buf);
        if(nRes != KErrNone)
            return nRes;

        pLocEntry->SetDataOffset((KLocatorsStartSec + locatorSectors) << KSectorSizeLog2);
        locatorSectors += (pLocEntry->DataSpace() >> KSectorSizeLog2);

        nRes = DoWriteData(aFd, pLocEntry->DataOffset(), pLocEntry->DataSpace(), buf.Ptr());
        if(nRes != KErrNone)
            return nRes;

    }

    //========= write footers, header =========

    //-- 1. footer and footer copy
    buf.Resize(TVhdFooter::KSize);
    aFooter.Externalise(buf.Ptr(), true);
    ASSERT(aFooter.IsValid());

    const uint32_t KFooterSec = KLocatorsStartSec + locatorSectors;
    const uint32_t KFooterCopySec = 0;

    nRes = DoWriteData(aFd, KFooterSec << KSectorSizeLog2, TVhdFooter::KSize, buf.Ptr());
    if(nRes != KErrNone)
        return nRes;

    nRes = DoWriteData(aFd, KFooterCopySec << KSectorSizeLog2, TVhdFooter::KSize, buf.Ptr());
    if(nRes != KErrNone)
        return nRes;

    //-- 2. Header
    buf.Resize(TVhdHeader::KSize);
    vhdHeader.Externalise(buf.Ptr(), true);
    ASSERT(vhdHeader.IsValid());

    nRes = DoWriteData(aFd, KHdrStartSec << KSectorSizeLog2, TVhdHeader::KSize, buf.Ptr());
    if(nRes != KErrNone)
        return nRes;


    //========= Create BAT =========
    //-- zero-fill whole BAT and then write FFs to the area occupied by BAT entries
    nRes = DoFillMedia(aFd, KBatStartSec << KSectorSizeLog2, KBatSizeInSectors << KSectorSizeLog2, 0);
    if(nRes != KErrNone)
        return nRes;

    nRes = DoFillMedia(aFd, KBatStartSec << KSectorSizeLog2, KBatFillBytes, 0xFF);
    if(nRes != KErrNone)
        return nRes;


    return KErrNone;
}


//--------------------------------------------------------------------
/**
    Generate VHD file based on provided parameters. @see TVHD_Params.
    Does not overwrite the existing file, fails in this case.
    If file is generated successfully, it is closed and needs to be reopened if further access to it is needed.
    If some error occured, the incompleted file is deleted.

    @param aFileName fully-qualified file name. file shouldn't exist before.
    @param aParams  in: desired VHD parameters; out: real parameters, they can differ from the initial

    @return KErrNone if everything is OK, negative error code othewise.
*/
int CVhdFileBase::GenerateFile(TVHD_Params& aParams)
{
    DBG_LOG("aFileName:%s, parameters:", aParams.vhdFileName);
    aParams.Dump();


    //-- 0. Check generic parameters
    aParams.secSizeLog2 = KDefSecSizeLog2; //-- just override, different sector sizes are not supported anyway

    if(!aParams.secPerBlockLog2)
        aParams.secPerBlockLog2 = KDefSecPerBlockLog2;
    else
    if(aParams.secPerBlockLog2 < 8 || aParams.secPerBlockLog2 > 19)
    {//-- blocks < 128K and > 64MB are not supported
        DBG_LOG("invalid secPerBlockLog2:%d", aParams.secPerBlockLog2);
        return KErrArgument;
    }


    //-- 1. populate VHD Footer with the specified parameters.
    //-- TVhdFooter::Init() can optionally change the parameters passed as an argument
    TVhdFooter footer;
    if(!footer.Init(aParams))
    {
        DBG_LOG("invalid Footer parameters!");
        return KErrArgument;
    }

    int nRes;

    //-- 2. create and lock a new file
    const int openFlags = O_LARGEFILE | O_RDWR | O_CREAT | O_EXCL | O_DIRECT;
    const mode_t openMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

    const int fd = open(aParams.vhdFileName, openFlags, openMode);
    if(fd < 0)
    {
        nRes = -errno;
        DBG_LOG("Error opening the file! code:%d", nRes);
        return nRes;
    }

    //-- 3. try to acquire write file lock
    flock64 fl;

    fl.l_type   = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0; //-- lock whole file from start to the end
    fl.l_pid    = getpid();

    if(fcntl(fd, F_SETLK, &fl) == -1)
    {//-- fail. close and delete the file
        nRes = -errno;
        DBG_LOG("Error locking the the file! code:%d", nRes);
        close(fd);
        unlink(aParams.vhdFileName);
        return nRes;
    }

    //-- 4. genetate file layout depending on VHD type
    switch(aParams.vhdType)
    {
        case EVhd_Fixed:
        nRes = DoGenerateVHD_Fixed(fd, aParams, footer);
        break;

        case EVhd_Dynamic:
        nRes = DoGenerateVHD_Dynamic(fd, aParams, footer);
        break;

        case EVhd_Diff:
        nRes = DoGenerateVHD_Differencing(fd, aParams, footer);
        break;

        default:
        nRes = KErrArgument;
        ASSERT(0);
        break;

    }; //switch(aParams.vhdType)

    //-- close the file
    close(fd);

    //-- check the result.
    if(nRes != KErrNone)
    {
        DBG_LOG("Error generating file layout! code:%d. Deleting the file...", nRes);
        unlink(aParams.vhdFileName);
    }


    return nRes;
}
















