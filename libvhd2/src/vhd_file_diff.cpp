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

    @file imlementation of the Differencing VHD file stuff
*/

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>

#include "vhd.h"
#include "block_mng.h"


//####################################################################
//#     CVhdFileDiff class implementation
//####################################################################

//--------------------------------------------------------------------
/**
    constructor.
    @param  apFooter a valid VHD footer
    @param  apHeader a valid VHD header
*/
CVhdFileDiff::CVhdFileDiff(const TVhdFooter* apFooter, const TVhdHeader* apHeader)
             :CVhdDynDiffBase(apFooter, apHeader)
{
    iParent = NULL;

    DBG_LOG("CVhdFileDiff::CVhdFileDiff[0x%p]", this);
    //-- process footer
    ASSERT(Footer().IsValid());
    ASSERT(Footer().DiskType() == EVhd_Diff);
}

CVhdFileDiff::~CVhdFileDiff()
{
    delete iParent;
}

//--------------------------------------------------------------------
/**
    Implements "Open" semantics for the dynamic VHD file
    @return standard error code, 0 on success.
*/
int CVhdFileDiff::Open()
{
    DBG_LOG("CVhdFileDiff::Open()[0x%p]", this);

    int nRes = CVhdDynDiffBase::Open();
    if(nRes != KErrNone)
    {
        DBG_LOG("CVhdFileDiff::Open() error! code:%d", nRes);
        return nRes;
    }

    ASSERT(ipBAT && ipSectorMapper);
    SetState(EOpened);


    //-- try to locate parent VHD file
    ASSERT(!iParent);
    std::string strParentPath;
    nRes = DoFindParentFile(strParentPath);
    if(nRes != KErrNone)
    {
        if(ModeFlags() & VHDF_OPEN_IGNORE_PARENT)
        {//-- forced to ignore problems with the parent VHD
            DBG_LOG("Forced to ignore problems with parent VHD!");
            return KErrNone;
        }
        else
        {
            Close();
            return KErr_VhdDiff_NoParent;
        }
    }

    //-- try to open Parent VHD if immediate opening is requied.
    //-- if KDiffVhd_LazyOpenParent is true then the parent VHD will be opened on first attempt to read data from it
    if(!KDiffVhd_LazyOpenParent)
    {
        nRes = OpenParentVHD(strParentPath.c_str());
        if(nRes != KErrNone)
        {
            ASSERT(!iParent);
            DBG_LOG("Error opening parent VHD. code:%d!", nRes);
            Close();
            return KErr_VhdDiff_NoParent;
        }
    }


    //-- process VHDF_OPMODE_PURE_BLOCKS flag if required. This is to ensure that
    //-- sector bitmaps in all blocks have all bits set. This may require file modification and
    //-- can take some time. But further access to the file will be faster, because bitmaps won't be touched
    if(!ReadOnly() && BlockPureMode())
    {
        do
        {   //-- invalidate bitmaps cache, just in case
            InvalidateCache();

            //-- do the real job
            nRes = ProcessPureBlocksMode();
            if(nRes != KErrNone)
                break;

            //-- flush dirty metadata caches
            nRes = Flush();
            if(nRes != KErrNone)
                break;

            //-- close bitmap cache, it should not be needed at all afterwards.
            //-- keep BAT cache. if it is populated, it is likely to be needed
            ipSectorMapper->Close();

            //-- close and re-open parent(s). this should destroy their caches that we may not need in the future
            CloseParentVHD();

            if(!KDiffVhd_LazyOpenParent)
            {
                nRes = OpenParentVHD(strParentPath.c_str());
                if(nRes != KErrNone)
                {
                    ASSERT(!iParent);
                    DBG_LOG("Error opening parent VHD. code:%d!", nRes);
                    nRes = KErr_VhdDiff_NoParent;
                    break;
                }
            }

        }while(0);
    }

    if(nRes != KErrNone)
    {
        DBG_LOG("Opening file error! code:%d", nRes);
        InvalidateCache(true); //-- invalidate all caches, try not to flush dirty data on error
        Close();
    }

    return nRes;
}


//--------------------------------------------------------------------
/**
    Deallocate / release resources, close file handle.
    The caller must be sure that all dirty data are flushed. See Flush().

    @param  aForceClose     if true, ignores dirty data. not for a normal use
*/
void CVhdFileDiff::Close(bool aForceClose /*=false*/)
{
    CloseParentVHD();
    CVhdDynDiffBase::Close(aForceClose);
}


//--------------------------------------------------------------------
/**
    Flushes VHD file data and metadata to the media.
    @return standard error code, 0 on success.
*/
int CVhdFileDiff::Flush()
{
    int nRes = CVhdDynDiffBase::Flush();

    if(iParent)
        iParent->Flush(); //-- this is just in case; parents are opened RO

    return nRes;
}

//--------------------------------------------------------------------
/**
    Invalidates cache data. If the client will try to access data with cache invalid, it will result in
    re-reading data from the media to the cache.
    Does not deallocate cache RAM, just marks data as invalid.

    @param  aIgnoreDirty    if true, ignores dirty data. not for a normal use

    @pre the cache must not be dirty; @see Flush()
*/
void CVhdFileDiff::InvalidateCache(bool aIgnoreDirty /*=false*/)
{
    CVhdDynDiffBase::InvalidateCache(aIgnoreDirty);

    if(iParent)
        iParent->InvalidateCache(aIgnoreDirty);
}



//--------------------------------------------------------------------
/**
    Find (by parent locator) and open parent VHD file in RO mode.

    @param  apParentFileName    if specified, it should be path to the existing parent VHD
                                if NULL, then the parent VHD will be sought by parent locators

    @return KErrNone if everything is OK, then iParent will be assingned a pointer to appropriare CVhdFileBase object
            negative error code otherwise, and iParent will be NULL

*/
int  CVhdFileDiff::OpenParentVHD(const char* apParentFileName/*= NULL*/) const
{
    DBG_LOG("CVhdFileDiff::OpenParentVHD()[0x%p]", this);

    ASSERT(State() == EOpened);

    if(iParent)
    {
        ASSERT(0);
        return KErrAlreadyExists;
    }

    int nRes;
    std::string strParentPath;

    //-- 1. Get fully-qualified path to parent by information from the parent locators.
    if(!apParentFileName)
    {
        nRes = DoFindParentFile(strParentPath);
        if(nRes!=KErrNone)
            return nRes;
    }
    else
        strParentPath = apParentFileName;

    //-- 2. try to open the parent VHD.
    const uint32_t parentModeFlags = ModeFlags() & (~VHDF_OPEN_RDWR); //-- parent must be opened RO

    CAutoClosePtr<CVhdFileBase> pVhdParent(CVhdFileBase::CreateFromFile(strParentPath.c_str(), parentModeFlags, nRes));

    if(!pVhdParent.get())
    {
        ASSERT(nRes < 0);
        return nRes;
    }

    //-- 3. try to open parent VHD file object
    nRes = pVhdParent->Open();
    if(nRes != KErrNone)
    {
        ASSERT(nRes < 0);
        return nRes;
    }

    //-- 4. check Parent's UUID, TimeStamp and geometry.
    /*
    strange enough, parent's time stamp seems to be ignored by VPC (though specs say that it should be checked). Don't check it.
    const uint32_t ts1 = Header().Parent_TimeStamp();
    const uint32_t ts2 = pVhdParent->Footer().TimeStamp();
    */

    const uuid_t& uuid1 = Header().Parent_UUID();       //-- Parent's UUID from this file header
    const uuid_t& uuid2 = pVhdParent->Footer().UUID();  //-- what is recorded in the header of the file we opened

    nRes = uuid_compare(uuid1, uuid2);
    const bool bParentMatch = (nRes == 0);

    if(!bParentMatch)
    {
        DBG_LOG("Parent & Diff VHDs UUID mismatch!");
        return KErr_VhdDiff_ParentId;
    }

    //-- check parent and child geometry matching.
    if(! DoValidateParentGeometry(pVhdParent.get()))
        return KErr_VhdDiff_Geometry;

    iParent = pVhdParent.release();

    return KErrNone;
}

//--------------------------------------------------------------------
/**
    Close parent VHD file, delete an object responcible to handle it and set iParent to NULL
*/
void CVhdFileDiff::CloseParentVHD() const
{
    DBG_LOG("CVhdFileDiff::CloseParentVHD()[0x%p]", this);

    if(!iParent)
        return;

    iParent->Close();
    delete iParent;
    iParent = NULL;
}


//--------------------------------------------------------------------
/**
    Try to find a parent VHD file using the information from VHD parent locators.
    Simply iterated through all parent locators of this file, trying to find an accessible parent VHD.


    @param  aParentRealName out: on success will contain a real name of the first parent file that can be accessed RO.
    @return KErrNone on success, negative error code otherwise
*/
int CVhdFileDiff::DoFindParentFile(std::string& aParentRealName) const
{
    DBG_LOG("CVhdFileDiff::DoFindParentFile()[0x%p]", this);

    int nRes;
    aParentRealName.clear();

    //-- ?? shall we try opening a parent VHD in the same directory where this VHD leaves (if locators didn't work)??
    //-- simply iterate through all parent locators finding the first one that points to the existing file
    uint i;
    for(i=0; i<TVhdHeader::KNumParentLoc; ++i)
    {
        nRes = DoReadParentLocator(i, aParentRealName, true);
        if(nRes != KErrNone)
            continue;

        DBG_LOG(" Trying loc[%d], '%s'", i, aParentRealName.c_str());

        if(aParentRealName.at(0) == KPathDelim)
        {//-- absolute path
        }
        else
        {//-- relative path,  make absolute from child's path and parent relative name. i.e. look in the directory where child lives.
            //-- need a modifiable buffer for lousy dirname() that can modify input buffer contents
            CDynBuffer buf(strlen(FilePath())+16);
            buf.FillZ();
            buf.Copy(0, strlen(FilePath()), FilePath());
            const char* pChildDir = dirname((char*)buf.Ptr());

            aParentRealName.insert(0, 1, KPathDelim);
            aParentRealName.insert(0, pChildDir);

        }

        //-- check if the file can be opened RO
        nRes = access(aParentRealName.c_str(), R_OK);
        DBG_LOG(" Tried loc[%d], path: '%s' Res:%d", i, aParentRealName.c_str(), nRes);
        if(nRes == KErrNone)
            break; //-- can be opened

    }//for(...)

    if(nRes == KErrNone && i<TVhdHeader::KNumParentLoc)
        return KErrNone;

    return KErrNotFound;
}

//--------------------------------------------------------------------
/**
    Get some VHD parameters
    @param  aVhdParams out: filled in parameters structure
    @param  aParentNo       number of the parent VHD file. 0 refers to _this_ file
*/
int CVhdFileDiff::GetInfo(TVHD_Params& aVhdInfo, uint32_t aParentNo) const
{
    int nRes = KErrNone;
    if(!iParent && KDiffVhd_LazyOpenParent)
    {//-- no parent VHD is opened. This can be because of "lazy parent opening". Try opening parent VHD
        nRes = OpenParentVHD();
    }

    if(aParentNo == 0)
    {//-- parent #0 means "this file"
        CVhdDynDiffBase::GetInfo(aVhdInfo, aParentNo);
        aVhdInfo.vhdParentName = (iParent) ? iParent->FilePath() : "" ;
        return KErrNone;
    }

    if(!iParent)
        return (ModeFlags() & VHDF_OPEN_IGNORE_PARENT) ? KErr_VhdDiff_NoParent : nRes;

    return iParent->GetInfo(aVhdInfo, aParentNo-1);
}


//--------------------------------------------------------------------
/**
    Prints out some information about VHD in a human-readable form.
    @param  aStr string where the information goes.
*/
void CVhdFileDiff::PrintInfo(std::string& aStr) const
{
    CVhdDynDiffBase::PrintInfo(aStr);

    std::string strLocator;

    //-- try to print out parent locators that can be found in the file
    for(uint i=0; i<TVhdHeader::KNumParentLoc; ++i)
    {
        int nRes = DoReadParentLocator(i, strLocator, false);
        if(nRes!= KErrNone)
        {
            if(nRes == KErrNotFound)
            {
                StrLog(&aStr, "Parent locator[%d]: None ", i);
            }
            else
            {
                StrLog(&aStr, "Error getting parent locator[%d]. Code:%d", i, nRes);
            }
        }
        else
        {
            const TParentLocatorEntry& locEntry = Header().GetParentLocatorEntry(i);

            const uint32_t locatorCode = locEntry.PlatCode();
            const char ch1 = U16High(U32High(locatorCode));
            const char ch2 = U16Low (U32High(locatorCode));
            const char ch3 = U16High(U32Low (locatorCode));
            const char ch4 = U16Low (U32Low (locatorCode));
            StrLog(&aStr, "Parent locator[%d] (PlatCode:0x%x, %c%c%c%c):'%s'", i, locatorCode, ch1,ch2,ch3,ch4 ,strLocator.c_str());
        }
    }


    if(!iParent && KDiffVhd_LazyOpenParent)
    {//-- no parent VHD is opened. This can be because of "lazy parent opening". Try opening parent VHD
        OpenParentVHD();
    }

    //-- @todo: print information about parent VHD here ???
    StrLog(&aStr, "Parent File:'%s'", (iParent) ? iParent->FilePath() : "NOT FOUND!!");

}

//--------------------------------------------------------------------
static void DoConvertWinPathToUnix(std::string& aPath)
{
    for(size_t i=0; i<aPath.length(); ++i)
    {//-- replace all DOS slashes to Unix ones
        if(aPath.at(i) == '\\')
            aPath.at(i) = KPathDelim;
    }

    //-- remove disk letter e.g. "c:" from absolute Win path. it will become absolute Unix-style path
    if(aPath.at(1) == ':')
        aPath.erase(0,2);
}

//--------------------------------------------------------------------
/**
    Read parent locator from the file and convert it to ASCII.
    Only known locator types are supported (see TParentLocatorEntry::TPlatCodes)

    @param  aIndex      parent locator entry index 0..7. Parent locator entry from the VHD header points to the parent locator data in the file
    @param  aLocator    out: parent locator string converted to ASCII.
    @param  aHackPathToUnix if true, will try to convert WIN or MacX path to UNIX standard (change slashes, etc.)

    @return KerrNone on success, negative error code otherwise
*/
int CVhdFileDiff::DoReadParentLocator(uint aIndex, std::string& aLocator, bool aHackPathToUnix) const
{
    ASSERT(Header().IsValid());
    ASSERT(State() == EOpened);

    aLocator.clear();
    const TParentLocatorEntry& locEntry = Header().GetParentLocatorEntry(aIndex);

    if(!locEntry.IsValid())
    {
        ASSERT(0);
        return KErrNotSupported;
    }

    if(locEntry.PlatCode() == TParentLocatorEntry::EPlatCode_NONE)
        return KErrNotFound;


    if(!locEntry.DataSpace() || !locEntry.DataLen() || !locEntry.DataOffset())
    {
        DBG_LOG("CVhdFileDiff::DoReadParentLocator[0x%p] Invalid PLocEntry[%d]", this, aIndex);
        locEntry.Dump(NULL, NULL);
        return KErrCorrupt;
    }

    if(locEntry.DataLen() >= PATH_MAX)
        return KErrNotSupported; //-- too long parent locator

    //-- specs don't say anything about sector-alignment of the parent locator;
    //-- I assume this should be the case
    if(locEntry.DataOffset() & (SectorSize()-1))
    {
        ASSERT(0);
        return KErrCorrupt;
    }

    //-- read raw parent locator data
    const uint32_t KParentLocSect = locEntry.DataOffset() >> SectorSzLog2();

    CDynBuffer bufIn(locEntry.DataLen()+16);
    bufIn.FillZ();

    int nRes = DoRaw_ReadData(KParentLocSect, locEntry.DataLen(), bufIn.Ptr());
    if((uint)nRes != locEntry.DataLen())
        return nRes; //-- this will be the error code

    //-- Win ASCII parent locator; Deprecated, but seems to be in use sometimes
    if(locEntry.PlatCode() == TParentLocatorEntry::EPlatCode_WI2R || locEntry.PlatCode() == TParentLocatorEntry::EPlatCode_WI2K)
    {
        aLocator.assign((const char*)bufIn.Ptr());

        if(aHackPathToUnix) //-- need to convert Win path to UNIX-style
            DoConvertWinPathToUnix(aLocator);

        return KErrNone;
    }

    //-- Win UTF16 parent locator; convert it from UTF16 to ASCII
    if(locEntry.PlatCode() == TParentLocatorEntry::EPlatCode_W2RU || locEntry.PlatCode() == TParentLocatorEntry::EPlatCode_W2KU)
    {
        CDynBuffer bufOut(4*bufIn.Size());

        nRes = UNICODE_to_ASCII(bufIn.Ptr(), locEntry.DataLen(), bufOut.Ptr(), bufOut.Size(), EUTF_16LE);
        if(nRes != KErrNone)
            return nRes;

        aLocator.assign((const char*)bufOut.Ptr());

        if(aHackPathToUnix) //-- need to convert Win path to UNIX-style
            DoConvertWinPathToUnix(aLocator);

        return KErrNone;
    }

    //-- MacX UTF8 parent locator; convert it to ASCII
    if(locEntry.PlatCode() == TParentLocatorEntry::EPlatCode_MACX)
    {
        CDynBuffer bufOut(2*bufIn.Size());

        nRes = UNICODE_to_ASCII(bufIn.Ptr(), locEntry.DataLen(), bufOut.Ptr(), bufOut.Size(), EUTF_8);
        if(nRes != KErrNone)
            return nRes;

        aLocator.assign((const char*)bufOut.Ptr());

        if(aHackPathToUnix)
        {//-- need to convert MacX path to UNIX-style
            const char token[] = "file://";
            if(aLocator.find(token) == 0)
                aLocator.erase(0, strlen(token));
        }

        return KErrNone;
    }

    //-- 'Mac' parent locator isn't supported
    return KErrNotSupported;
}

//--------------------------------------------------------------------
/**
    Ensures that sector bitmaps in all present blocks have all bits set.
    Checks every bitmap; if finds '0' bit there, then it copies sectors from the parent VHDs
    As a result all bitmaps in the file will have all bits set.

    @return	KErrNone on success; negative error code otherwise
*/
int CVhdFileDiff::ProcessPureBlocksMode()
{
    DBG_LOG("CVhdFileDiff::ProcessPureBlocksMode[0x%p]",this);
    ASSERT(State() == EOpened);
    ASSERT((!ReadOnly() && BlockPureMode()));

    const uint KBlocks = Header().MaxBatEntries();
    const uint KBmpSizeInBits = SBmp_SizeInSectors() << (SectorSzLog2() + KBitsInByteLog2);

    CBitVector blkBitmap; //-- block sectors bitmap

    for(uint currBlock=0; currBlock<KBlocks; ++currBlock)
    {
        //-- get BAT entry.
        const TBatEntry KBlockSector = ipBAT->ReadEntry(currBlock);

        if(KBlockSector == KBatEntry_Unused)
            continue; //-- block isn't present

        ASSERT(BatEntryValid(KBlockSector));

        //-- checks if block's bitmap has all bits set
        const CSectorBmpPage* pBitmap = ipSectorMapper->GetSectorAllocBitmap(KBlockSector);
        if(!pBitmap)
            return KErrCorrupt;

        const TSectorBitmapState bmpState = pBitmap->State();
        ASSERT(bmpState == ESB_FullyMapped || bmpState == ESB_Clean || bmpState == ESB_FullyUnmapped);

        if(bmpState == ESB_FullyMapped)
            continue; //-- all bits are already set

        DBG_LOG(" Processing non-pure block:%d, bmpState:%d",currBlock, bmpState);

        //-- go through block bitmap, copying sectors from parent VHDs for corresponding '0' bits
        if(!blkBitmap.Size())
            blkBitmap.New(KBmpSizeInBits);

        pBitmap->GetAllocBitmap(blkBitmap); //-- get block's sector allocation bitmap

        TBitExtentFinder extFinder(blkBitmap);

        for(;;)
        {//-- find extents of '0' bits in the block bitmap and copy corresponding sectors from parent VHDs

            if(!extFinder.FindExtent())
                break;

            if(extFinder.ExtBitVal())
                continue; //-- extent of '1's, not interested

            const uint KFileSectorP   = KBlockSector + SBmp_SizeInSectors() + extFinder.ExtStartPos();
            const uint KParentSectorL = (currBlock<<SectorsPerBlockLog2())  + extFinder.ExtStartPos();

            //-- some sectors in the last block may not be in use
            if(KParentSectorL >= VhdSizeInSectors())
                    break;

            uint nSectorsToCopy = extFinder.ExtLen();
            if(KParentSectorL + nSectorsToCopy >= VhdSizeInSectors())
                nSectorsToCopy -= (KParentSectorL + nSectorsToCopy - VhdSizeInSectors());

            int nRes = DoCopySectorsFromParent(KParentSectorL, KFileSectorP, nSectorsToCopy);
            if(nRes != KErrNone)
                return nRes;

        }

        //-- all sectors in this block either contain valid data or zero-filled. set all bits in the bitmap
        const TSectorBitmapState sectBmpState = ipSectorMapper->SetSectorAllocBits(KBlockSector, 0, KBmpSizeInBits);
        if(sectBmpState == ESB_Invalid)
        {//-- something really bad happened
            ASSERT(0);
            return KErrCorrupt;
        }

    }//for(uint currBlock=0;...)

    return KErrNone;
}

//--------------------------------------------------------------------
/**
    Read a number of sectors from the Parent VHD file.
    Parameters and return value are the same as in ReadSectors()
*/
int CVhdFileDiff::DoReadSectorsFromParent(uint32_t aStartSector, int aSectors, void* apBuffer, uint32_t aBufSize)
{
    DBG_LOG("CVhdFileDiff::DoReadSectorsFromParent[0x%p] startSec:%d, num:%d",this, aStartSector, aSectors);

    if(!iParent)
    {   //-- no parent VHD is opened. This can be because of "lazy parent opening" or no parent found at all
        //-- try opening parent VHD
        int nRes = OpenParentVHD();
        if(nRes != KErrNone)
            return KErr_VhdDiff_NoParent;
    }

    ASSERT(iParent);
    return iParent->ReadSectors(aStartSector, aSectors, apBuffer, aBufSize);
}



//--------------------------------------------------------------------
/**
    Read a sector extent from given _single_ block in the VHD file.

    @param  aParams parameters, describing the operation. Some of them will be adjusted on completion.
    @return	KErrNone on success, negative value corresponding system error code otherwise.
*/
int CVhdFileDiff::DoReadSectorsFromBlock(TBlkOpParams &aParams)
{
    int nRes;

    const uint32_t KStartSectorL  = aParams.iCurrSectorL;
    const uint32_t KSectorsToRead = aParams.iNumSectors;
    const uint32_t KBytesToRead   = KSectorsToRead << SectorSzLog2();

    //-- get BAT entry.
    const TBatEntry KBlockSector = ipBAT->ReadEntry(aParams.iCurrBlock); //-- Block starting sector in the file

    if(KBlockSector == KBatEntry_Unused)
    {//-- 1. whole block isn't present, need to read data from the parent VHD
        nRes = DoReadSectorsFromParent(KStartSectorL, KSectorsToRead, aParams.ipData, KBytesToRead);
        if(nRes != (int)KSectorsToRead)
            return nRes; //-- it is negative error code here
    }
    else
    {//-- block is present in the VHD, read sectors. Sector bitmap contains '1' if the sector is in this file and '0' if it is in parent VHD
        ASSERT(BatEntryValid(KBlockSector));

        TSectorBitmapState bmpState = ESB_Invalid;
        const CSectorBmpPage* pBitmap = NULL;

        if(BlockPureMode())
        {//-- if we are operating in PURE mode, it is _guaranteed_ that the bitmap contains all bits set to 1
            //-- don't need to do anyting, sector bitmap cache must not be used
            ASSERT(ipSectorMapper->State() == CSectorMapper::EInvalid);
            bmpState = ESB_FullyMapped;
        }
        else
        {
            pBitmap = ipSectorMapper->GetSectorAllocBitmap(KBlockSector);
            if(!pBitmap)
                return KErrCorrupt;

            bmpState = pBitmap->State();
        }

        const uint32_t KBitmapSectors = SBmp_SizeInSectors(); //-- block allocation bitmap size, in sectors

        if(bmpState == ESB_FullyMapped)
        {//-- all '1' bits in the bitmap, read a chunk of sectors from this VHD
            const uint32_t startDataSecP = KBlockSector + KBitmapSectors + SectorInBlock(KStartSectorL);
            nRes = DoRaw_ReadData(startDataSecP, KBytesToRead, aParams.ipData);
            if(nRes >= 0)
                {ASSERT(nRes == (int)KBytesToRead);}
        }
        else if(bmpState == ESB_FullyUnmapped)
        {//-- all '0' bits in the bitmap, read a chunk of sectors from parent VHD
            nRes = DoReadSectorsFromParent(KStartSectorL, KSectorsToRead, aParams.ipData, KBytesToRead);
            if(nRes >= 0)
                {ASSERT(nRes == (int)KSectorsToRead);}

        }
        else
        {//-- a mixture of '1's and '0's in the bitmap, need a selective read
            ASSERT(bmpState == ESB_Clean || bmpState == ESB_Dirty);

            uint32_t sectorInBlock = SectorInBlock(KStartSectorL);                  //-- sector number _in_ the block
            uint32_t sectorP       = KBlockSector + KBitmapSectors + sectorInBlock; //-- Physical sector number in the _file_
            uint8_t* pBuf = aParams.ipData;  //-- local pointer in the user's buffer

            //-- try finding extents of '0's and '1's in the bitmap and read multiple of sectors corresponding to the contuguous block of the same bit value
            TBitExtentFinder extFinder(pBitmap->GetAllocBitmap_Raw(), sectorInBlock, KSectorsToRead);

            for(;;)
            {
                if(!extFinder.FindExtent())
                    break;

                const uint extSectors = extFinder.ExtLen(); //-- number of contiguous sectors in extent
                const uint extBytes   = extSectors << SectorSzLog2();

                if(extFinder.ExtBitVal())
                {//-- found an extent of '1's, read corresponding sectors from this VHD
                    nRes = DoRaw_ReadData(sectorP, extBytes, pBuf);
                    if(nRes >= 0)
                        {ASSERT(nRes == (int)extBytes);}
                }
                else
                {//-- found an extent of '0's, read corresponding sectors from parent VHDs
                    const uint32_t parentSectorL = KStartSectorL + (extFinder.ExtStartPos()-sectorInBlock);
                    nRes = DoReadSectorsFromParent(parentSectorL, extSectors, pBuf, extBytes);
                    if(nRes >= 0)
                        {ASSERT(nRes == (int)extSectors);}
                }

                sectorInBlock += extSectors;
                sectorP += extSectors;
                pBuf    += extBytes;

            }//for(;;)

        }

        if(nRes <0)
            return nRes;

    }

    //-- update parameters data
    aParams.iCurrSectorL += KSectorsToRead;
    aParams.ipData       += KBytesToRead;

    return KErrNone;
}


//--------------------------------------------------------------------
/**
    Write a sector extent to given _single_ block in the VHD file.

    @param  aParams parameters, describing the operation. Some of them will be adjusted on completion.
    @return	KErrNone on success, negative value corresponding system error code otherwise.
*/
int CVhdFileDiff::DoWriteSectorsToBlock(TBlkOpParams &aParams)
{

    int nRes;

    const uint32_t KStartSectorL  = aParams.iCurrSectorL;
    const uint32_t KSectorsToWrite= aParams.iNumSectors;
    const uint32_t KBytesToWrite  = KSectorsToWrite << SectorSzLog2();
    const uint32_t KBitmapSectors = SBmp_SizeInSectors(); //-- block allocation bitmap size, in sectors

    bool bSetAllBmpBits = false; //-- if true, then ALL block bitmap bits will be set


    //-- get BAT entry.
    TBatEntry blockSector = ipBAT->ReadEntry(aParams.iCurrBlock); //-- Block starting sector in the file

    if(blockSector == KBatEntry_Unused)
    {//-- the block isn't present; need to extend VHD file by one block

        //-- 1.1 find out if we need to have all bits in the alloc. bitmap set. If true, then sector bitmap will have all bits set
        bSetAllBmpBits = (BlockPureMode() || KDiffVhd_CreateFullyMappedBlock)  //-- in 'pure mode' everything is done to avoid using bitmap caches for the performance sake
                          && (!TrimEnabled());                                 //-- when using TRIM '0' bits indicate sectors that can be discarded and should be read as zeros.


        //-- 1. append an empty block with appropriate bitmap fill
        nRes = AppendBlock(blockSector, bSetAllBmpBits, false);
        if(nRes < 0)
            return nRes;

        const uint32_t chunk1_SecStart_L = (KStartSectorL >> SectorsPerBlockLog2())<<SectorsPerBlockLog2(); //-- logical starting sector of chunk1
        const uint32_t chunk1_SecStart_P = blockSector + KBitmapSectors;//-- physical starting sector of chunk1
        const uint32_t chunk1_SecLen     = SectorInBlock(KStartSectorL);   //-- number of sectors in chunk1

        const uint32_t chunk2_SecStart_L = chunk1_SecStart_L + chunk1_SecLen + KSectorsToWrite; //-- logical starting sector of chunk2
        const uint32_t chunk2_SecStart_P = chunk1_SecStart_P + chunk1_SecLen + KSectorsToWrite; //-- physical starting sector of chunk2
              uint32_t chunk2_SecLen     = SectorsPerBlock()- (chunk1_SecLen + KSectorsToWrite);//-- number of sectors in chunk2

        //-- check that chunk2 doesn't span over the VHD sectors space.
        if(chunk2_SecStart_L + chunk2_SecLen >= VhdSizeInSectors())
        {
            chunk2_SecLen -= (chunk2_SecStart_L + chunk2_SecLen - VhdSizeInSectors());
        }


        if(bSetAllBmpBits)
        {   //-- request to add a block that contains all relevant sectors from parent VHD(s)
            //-- copy the necessary data sectors from parents in order to have all allocation bitmap bits set to '1'

            //-- 1. copy extent before block of data to be written later
            nRes = DoCopySectorsFromParent(chunk1_SecStart_L, chunk1_SecStart_P, chunk1_SecLen);
            if(nRes != KErrNone)
                return nRes;

            //-- 2. copy extent after block of our data
            nRes = DoCopySectorsFromParent(chunk2_SecStart_L, chunk2_SecStart_P, chunk2_SecLen);
            if(nRes != KErrNone)
                return nRes;

            //-- all bits in the block bitmap will be set later on.
            bSetAllBmpBits = true;
        }
        else
        {//-- request to add an empty block
            if(KDiffVhd_ZeroFillAppendedBlock)
            {//-- need to zero-fill it
                //-- 1. zero fill extent before block of data to be written later
                nRes = DoRaw_FillMedia(chunk1_SecStart_P, chunk1_SecLen, 0x00);
                if(nRes != KErrNone)
                    return nRes;

                //-- 2. zero fill extent after block of our data
                nRes = DoRaw_FillMedia(chunk2_SecStart_P, chunk2_SecLen, 0x00);
                if(nRes != KErrNone)
                    return nRes;
            }
        }

        //-- place entry to BAT cache
        nRes = ipBAT->WriteEntry(aParams.iCurrBlock, blockSector);
        if(nRes < 0)
        {
            ASSERT(0);
            return nRes;
        }

        aParams.iFlushMetadata = true; //-- indicate that the metadata caches need flushing
    }//if(blockSector == KBatEntry_Unused)

    //============================================================
    //-- block is present in the VHD, write sectors.
    ASSERT(BatEntryValid(blockSector));

    const uint32_t startDataSecP = blockSector + KBitmapSectors + SectorInBlock(KStartSectorL);

    nRes = DoRaw_WriteData(startDataSecP, KBytesToWrite, aParams.ipData);
    if(nRes <0)
        return nRes;//-- this is the error code

    ASSERT(nRes == (int)KBytesToWrite);

    //============================================================
    //-- set corresponding bits in the bitmap
    if(BlockPureMode())
    {   //-- if we are operating in PURE mode, it is _guaranteed_ that the bitmap contains all bits set 1
        //-- don't need to do anyting, sector bitmap cache must not be used
        ASSERT(ipSectorMapper->State() == CSectorMapper::EInvalid);
    }
    else
    {   //-- set the corresponding bits in the sector bitmap to indicate sectors written
        //-- if bSetAllBmpBits == true, then all sectors from parent VHD(s) had already been copied here. Set all bits in the bitmap.
        const uint32_t startBit = bSetAllBmpBits ? 0                 : SectorInBlock(KStartSectorL);
        const uint32_t numBits  = bSetAllBmpBits ? SectorsPerBlock() : KSectorsToWrite;

        const TSectorBitmapState sectBmpState = ipSectorMapper->SetSectorAllocBits(blockSector, startBit, numBits);
        if(sectBmpState == ESB_Invalid)
        {//-- something really bad happened
            ASSERT(0);
            return KErrCorrupt;
        }
    }

    //-- update parameters data
    aParams.iCurrSectorL += KSectorsToWrite;
    aParams.ipData       += KBytesToWrite;

    return KErrNone;
}


//--------------------------------------------------------------------
/**
    Mark an extent of sectors as "TRIMmed" or "Discarded". Such sectors will be treated as no longer containing a valid information.
    Later on, some smart things can be done using information about "discarded" sectors.
    E.g. VHD blocks that consist of "discarded" sectors can be taken out from the VHD (VHD compacting)

    The actual result of this API call is implementation-defined.

	@param	aStartSector	starting logical sector.
	@param	aSectors		number of logical sectors to "discard" 0..LONG_MAX

	@return	positive number of written sectors on success, negative value corresponding system error code otherwise.
*/
int CVhdFileDiff::DiscardSectors(uint32_t aStartSector, int aSectors)
{
    DBG_LOG("#--- CVhdFileDiff::DiscardSectors[0x%p] startSec:%d, num:%d",this, aStartSector, aSectors);

    ASSERT(TrimEnabled() && !BlockPureMode());

    //-- check arguments and adjust number of sectors to write if necessary
    int nRes = DoCheckRW_Args(aStartSector, aSectors, UINT_MAX);
    if(nRes <= 0 )
        return nRes; //-- something is wrong with the arguments

    uint32_t remSectors = (uint32_t)nRes; //-- total amount of sectors to mark as "discarded", possibly adjusted
    uint32_t currSectorL = aStartSector;  //-- current _logical_ sector number

    uint32_t currBlock = SectorToBlockNumber(aStartSector); //-- current block number we are dealing with
    uint32_t cntBlocks = SectorToBlockNumber(aStartSector + remSectors -1) - currBlock + 1; //-- number of blocks the range of sectors spans
    ASSERT(cntBlocks && cntBlocks <= (VhdSizeInSectors()>>SectorsPerBlockLog2()));

    for(;;)
    {
        --cntBlocks;

        //-- amount of sectors we can mark in the _current_ block
        const uint32_t KSectorsToMark = (cntBlocks) ? SectorsPerBlock()-SectorInBlock(currSectorL) //-- sectors span more than one block, access those ones that are in the current block only.
                                                    : remSectors;                                 //-- all sectors fit in one block

        //-- get BAT entry, find out block starting sector
        const TBatEntry blockSector = ipBAT->ReadEntry(currBlock);

        if(blockSector != KBatEntry_Unused)
        {   //-- the block is present,
            //-- mark range of sectors in the block as 'discarded' by resetting corresponding bits in the allocation bitmaps
            ASSERT(BatEntryValid(blockSector));

            const uint32_t startBit = SectorInBlock(currSectorL);
            const TSectorBitmapState sectBmpState = ipSectorMapper->ResetSectorAllocBits(blockSector, startBit, KSectorsToMark);
            if(sectBmpState == ESB_Invalid)
            {//-- something really bad happened
                ASSERT(0);
                return KErrCorrupt;
            }
        }

        //============================================================
        ASSERT(remSectors >= KSectorsToMark);
        remSectors  -= KSectorsToMark;
        currSectorL += KSectorsToMark;

        if(!cntBlocks)
            break;

        ++currBlock;

    };

    ASSERT(!remSectors);

    return KErrNone;
}


//--------------------------------------------------------------------
/**
    Copy a number of sectors from parent VHD(s) to this one. All sectors must reside in the same block.

	@param	aStartSectorParentL	starting _logical_ sector of the parent VHD
	@param  aStartSectorChildP  starting _physical_ sector in the real file where data will be written to
	@param  aSectors            Number of sectors to copy

	@return	KErrNone on success, negative value corresponding system error code otherwise.
*/
int CVhdFileDiff::DoCopySectorsFromParent(uint32_t aStartSectorParentL, uint32_t aStartSectorChildP, uint32_t aSectors)
{
    DBG_LOG("#--- CVhdFileDiff::DoCopySectorsFromParent[0x%p] aStartSectorParentL:%d, aStartSectorChildP:%d, aSectors:%d",this, aStartSectorParentL, aStartSectorChildP, aSectors);

    ASSERT(State() == EOpened && !ReadOnly());
    ASSERT(aSectors <= SectorsPerBlock());
    ASSERT((aStartSectorParentL & (SectorsPerBlock()-1)) + aSectors <= SectorsPerBlock()); //-- ensure that we work within 1 block only

    if(!aSectors)
        return KErrNone;

    const uint KMaxBufSize = KDefScratchBufSize; //-- max. buffer size in bytes
    const uint KBufSizeSectors = Min(aSectors, (KMaxBufSize >> SectorSzLog2()));
    const uint KBufSize = KBufSizeSectors << SectorSzLog2();

    CDynBuffer buf(KBufSize);
    int nRes;

    while(aSectors)
    {
        const uint sectorsToRead = Min(KBufSizeSectors, aSectors);

        //-- read _logical_ sectors from the parent VHD(s)
        nRes = DoReadSectorsFromParent(aStartSectorParentL, sectorsToRead, buf.Ptr(), KBufSize);
        if(nRes <0)
            return nRes;//-- this is the error code

        ASSERT(nRes == (int)sectorsToRead);

        //-- write _physical_ sectors to the file
        const uint bytesToWrite = sectorsToRead << SectorSzLog2();

        nRes = DoRaw_WriteData(aStartSectorChildP, bytesToWrite, buf.Ptr());
        if(nRes <0)
            return nRes;//-- this is the error code

        ASSERT(nRes == (int)bytesToWrite);

        aStartSectorChildP  += sectorsToRead; //-- they don't cross block boundary
        aStartSectorParentL += sectorsToRead;
        aSectors -= sectorsToRead;
    };


    return KErrNone;
}


//--------------------------------------------------------------------
/**
    Get block's sector allocation bitmap
    '1' bit in the bitmap corresponds to the "existing" sector in the block that overrides the sector from the parent VHD
    '0' corresponds to the sector that should be read from the parent

    @param      aLogicalBlockNumber logical block number
    @param      aSrcBitmap out: bit vector representing block bitmap.

    @return     KErrNone on success, error code otherwise
*/
int CVhdFileDiff::GetBlockBitmap(uint32_t aLogicalBlockNumber, CBitVector& aSrcBitmap) const
{
    if(SectorsPerBlock() !=  aSrcBitmap.Size())
        Fault(ESecMap_InvalidSectorNumber);

    const TBatEntry blockSector = ipBAT->ReadEntry(aLogicalBlockNumber);

    ASSERT(BatEntryValid(blockSector));

    //-- get block bitmap. This may cause bitmap cache population
    const CSectorBmpPage* pBitmap = ipSectorMapper->GetSectorAllocBitmap(blockSector);

    if(!pBitmap)
        return KErrCorrupt;

    return pBitmap->GetAllocBitmap(aSrcBitmap);
}


//--------------------------------------------------------------------
/**
    Get a pointer to the object representing a parent VHD file.
    If the parent VHD isn't opened, makes the best effort to open it.

    @param  aParentNo parent VHD number counted from the "Tail" towards "Head"
            0 means "this" VHD, 1 - first parent, 2 parent of the 1st parent etc.

    @return pointer to the parent VHD, NULL on error
*/
const CVhdFileBase* CVhdFileDiff::GetParentOpened(uint32_t aParentNo)
{
    if(aParentNo == 0)
        return this;

    if(!iParent)
    {//-- open parent VHD
        const int nRes = OpenParentVHD();
        if(nRes!= KErrNone)
            return NULL;
    }

    ASSERT(iParent);
    return iParent->GetParentOpened(aParentNo - 1);
}

//--------------------------------------------------------------------
/**
    A helper method that checks if the parent VHD's geometry matches this one.
    @param  apParentVhd pointer to the parent VHD object

    @return true if everything is OK, false on error
*/
bool CVhdFileDiff::DoValidateParentGeometry(const CVhdFileBase* apParentVhd) const
{
    DBG_LOG("CVhdFileDiff::DoValidateParentGeometry[0x%p] parent:[0x%p]",this, apParentVhd);

    //-- check number of sectors
    if(VhdSizeInSectors() > apParentVhd->VhdSizeInSectors())
    {
        DBG_LOG("Parent VHD is too small! %d - %d",VhdSizeInSectors(), apParentVhd->VhdSizeInSectors());
        return false;
    }

    if( VhdSizeInSectors() != apParentVhd->VhdSizeInSectors() ||
        Footer().DiskGeometry() != apParentVhd->Footer().DiskGeometry())
    {//-- not lethal in theory, but worth a warning at least
        DBG_LOG("!!! Warning !!! Parent VHD can be used, but its parameters are differrent !!!");
        //return false;
    }

    //-- check the block size and number of blocks
    if(apParentVhd->VhdType() == EVhd_Dynamic || apParentVhd->VhdType() == EVhd_Diff)
    {
        const CVhdDynDiffBase* pVhdParent = (const CVhdDynDiffBase*)apParentVhd;

        if(SectorsPerBlock() != pVhdParent->SectorsPerBlock())
        {
            DBG_LOG("Parent VHD block size mismatch!");
            return false;
        }

        if(Header().MaxBatEntries() > pVhdParent->Header().MaxBatEntries())
        {
            DBG_LOG("Parent VHD has less blocks than required!");
            return false;
        }

    }

    return true;
}







