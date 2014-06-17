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

    @file imlementation Differencing VHD file coalescing stuff
*/

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>

#include "vhd.h"
#include "block_mng.h"



//--------------------------------------------------------------------
/**

    Coalesce sectors into the given block.
    If some sector in this block has corresponding bitmap bit set to '0', an attempt will be made to copy it from the parent VHDs sub-chain.
    This may involve adding a block to this VHD if it wasn't present.

    @param  aLogicalBlockNumber     VHD _logical_ block number
    @param  aVhdChainLength         length of the parent VHDs chain to be looked for the missing sector. I.e. it is a number of parent VHDs to be traversed back.
*/
int CVhdFileDiff::DoCoalesceBlock(uint32_t aLogicalBlockNumber, uint32_t aVhdChainLength)
{
    DBG_LOG("CVhdFileDiff::DoCoalesceBlock[0x%p] aBlockNo:%d", this, aLogicalBlockNumber);

    int nRes;

    CBitVector tailBitmap;      //-- this block sectors bitmap
    tailBitmap.New(SectorsPerBlock());

    const bool blockPresent = IsBlockPresent(aLogicalBlockNumber);

    //-- 1. check block initial state
    if(blockPresent)
    {
        nRes = GetBlockBitmap(aLogicalBlockNumber, tailBitmap);
        if(nRes != KErrNone)
            return nRes;

        if(tailBitmap.IsFilledWith(1))
            return KErrNone; //-- the block is present in this VHD and all sectors are fully mapped; nothing to do
    }


    CBitVector coalesceBitmap;  //-- sector coalescing bitmap
    CBitVector scratchBitmap;   //-- scratch bitmap

    coalesceBitmap.New(SectorsPerBlock());
    scratchBitmap.New(SectorsPerBlock());

    //-- 2. walk down the chain of VHD files collecting block's sectors bitmaps
    coalesceBitmap.Fill(0);

    for(uint parentNo = 1; parentNo <= aVhdChainLength; ++parentNo)
    {
        //-- @todo this part can be optimised, if we collect a list of pointers to parent VHDs first (in the caller)
        //-- and use it instead of walking the list for every block
        const CVhdFileBase* pParentVhd = GetParentOpened(parentNo);

        //-- get parent's block information
        if(! pParentVhd->IsBlockPresent(aLogicalBlockNumber))
            continue; //-- given block doesn't exist in the parent VHD

        //-- get parent's block bitmap
        nRes = pParentVhd->GetBlockBitmap(aLogicalBlockNumber, scratchBitmap);
        if(nRes != KErrNone)
            return nRes;

        //-- merge block bitmap with previous parents'
        coalesceBitmap.Or(scratchBitmap);

        if(coalesceBitmap.IsFilledWith(1))
            break; //-- no need to go down the chain, all sectors in the block are already mapped

    }

    //-- 3. make a bitmap that shows which sectors must be copied from the parent
    //-- coalesceBitmap will contain bits set to '1' for the sectors that need to be copied from the parent VHD(s)
    tailBitmap.Invert();
    coalesceBitmap.And(tailBitmap);

    tailBitmap.Close();
    scratchBitmap.Close();

    //-- 4. copy requited sectors into this VHD
    if(coalesceBitmap.IsFilledWith(0))
        return KErrNone; //-- nothing to do

    //-- 5. append empty block if required.
    if(!blockPresent)
    {
        TBatEntry batEntry;
        nRes = AppendBlock(batEntry, false, false);
        if(nRes < 0)
            return nRes;

        //-- place entry to BAT cache
        nRes = ipBAT->WriteEntry(aLogicalBlockNumber, batEntry);
        if(nRes < 0)
            return nRes;

        nRes = ipBAT->Flush();
        if(nRes < 0)
            return nRes;

    }

    //-- 6. copy sectors (block bitmap isn't written yet)
    const TBatEntry KBlockStartSector = ipBAT->ReadEntry(aLogicalBlockNumber);
    ASSERT(BatEntryValid(KBlockStartSector));

    const uint32_t  startDataSec = KBlockStartSector + SBmp_SizeInSectors(); //-- data in the block start from this sector
    const uint      KSectorsPerBlock = SectorsPerBlock();

    if(coalesceBitmap.IsFilledWith(1))
    {//-- need to copy ALL parents' sectors into this block
        const uint32_t startSectorParentL = aLogicalBlockNumber << SectorsPerBlockLog2();
        const uint32_t startSectorChildP  = startDataSec;

        nRes = DoCopySectorsFromParent(startSectorParentL, startSectorChildP, KSectorsPerBlock);
        if(nRes < 0)
            return nRes;

        //-- set ALL bitmap bits
        const TSectorBitmapState sectBmpState = ipSectorMapper->SetSectorAllocBits(KBlockStartSector, 0, KSectorsPerBlock);
        if(sectBmpState == ESB_Invalid)
        {//-- something really bad happened
            ASSERT(0);
            return KErrCorrupt;
        }

    }
    else
    {//-- need a selective copy. Copy only sectors with corresponding '1' in the coalescing bitmap
        //-- find extents of '1' bits in the coalescing bitmap and copy corresponding sectors from the parents
        TBitExtentFinder extFinder(coalesceBitmap);

        for(;;)
        {
            if(!extFinder.FindExtent())
                break;

            if(!extFinder.ExtBitVal())
                continue; //-- an extent of '0's, not interested

            const uint32_t startSectorParentL = (aLogicalBlockNumber << SectorsPerBlockLog2()) + extFinder.ExtStartPos();
            const uint32_t startSectorChildP  = startDataSec + extFinder.ExtStartPos();

            nRes = DoCopySectorsFromParent(startSectorParentL, startSectorChildP, extFinder.ExtLen());
            if(nRes < 0)
                return nRes;

            //-- set corresponding bitmap bits
            const TSectorBitmapState sectBmpState = ipSectorMapper->SetSectorAllocBits(KBlockStartSector, extFinder.ExtStartPos(), extFinder.ExtLen());
            if(sectBmpState == ESB_Invalid)
            {//-- something really bad happened
                ASSERT(0);
                return KErrCorrupt;
            }
        }//for(;;)
    }

    //-- 7. finish
    ASSERT(ipBAT->State() == CBat::EClean);

    nRes = ipSectorMapper->Flush();
    if(nRes < 0)
        return nRes;

    return KErrNone;
}


//--------------------------------------------------------------------
/**

    Coalesce data from the chain of the parent VHDs into this one.
    Walks through all blocks in this VHD and tries to copy missing sectors from the sub-cjain of parents.
    @see CVhdFileDiff::DoCoalesceBlock()


    @param  aVhdChainLength length of the parent VHDs chain to be looked for the missing sector. I.e. it is a number of parent VHDs to be traversed back.
*/
int CVhdFileDiff::CoalesceDataIn(uint32_t aVhdChainLength)
{
    DBG_LOG("CVhdFileDiff::CoalesceDataIn[0x%p] aVhdChainLength:%d", this, aVhdChainLength);
    ASSERT(aVhdChainLength);

    if(State() != EOpened || ReadOnly())
        return KErrAccessDenied;

    int nRes = KErrNone;
    const uint numBlocks = Header().MaxBatEntries();

    for(uint i=0; i<numBlocks; ++i)
    {
        nRes = DoCoalesceBlock(i, aVhdChainLength);
        if(nRes != KErrNone)
            break;
    }

    return nRes;
}


//--------------------------------------------------------------------
/**
    Change this VHD parent file to a new one. Quite dangerous operation, because it implies
    changing VHD header and parent locators. It can't be done atomically and there is a possibility
    to corrupt VHD metadata if write occurs.

    @post   all files apart from those that become a new chain, will be closed.

    @param      aNewParentFileName new parent file name. New parent VHD's parameters must match this differencing VHD ones.
    @return     KErrNone on success, negative error code otherwise

*/
int CVhdFileDiff::ChangeParentVHD(const char *aNewParentFileName)
{
    DBG_LOG("CVhdFileDiff::ChangeParentVHD[0x%p] Name:%s", this, aNewParentFileName);

    if(State() != EOpened || ReadOnly())
        return KErrAccessDenied;

    int nRes;

    //-- 0. close all caches and parent VHD
    nRes = Flush();
    if(nRes != KErrNone)
        return nRes;

    ipSectorMapper->Close();
    ipBAT->Close();

    CloseParentVHD();

    //-- 1. try to open a new parent VHD file in order to get its parameters
    const uint32_t parentModeFlags = ModeFlags() & (~VHDF_OPEN_RDWR); //-- parent must be opened RO
    CAutoClosePtr<CVhdFileBase> pVhdNewParent(CVhdFileBase::CreateFromFile(aNewParentFileName, parentModeFlags, nRes));
    if(!pVhdNewParent.get())
        return nRes;

    nRes = pVhdNewParent->Open();
    if(nRes != KErrNone)
        return nRes;

    //-- 2. check parent's VHD geomentry, it must match current VHD's
    if(! DoValidateParentGeometry(pVhdNewParent.get()))
        return KErr_VhdDiff_Geometry;


    CDynBuffer buf;

    //-- 3. check if we can change parent locators in this file to point to the new parent.
    //-- if there is no room in one of the parent locators, can't change anything. Adding new locators is a pain.
    //-- also fail if we find unsupported locator types, because we can't change them.
    {
        TParentLocatorEntry dummyEntry;

        for(uint i=0; i<TVhdHeader::KNumParentLoc; ++i)
        {
            const TParentLocatorEntry& ourEntry = Header().GetParentLocatorEntry(i);
            if(ourEntry.PlatCode() == TParentLocatorEntry::EPlatCode_NONE)
                continue;

            //-- generate a new parent locator and see if it fits into old one's place
            dummyEntry.Init(ourEntry.PlatCode());
            nRes = GenerateParentLocator(FilePath(), pVhdNewParent->FilePath(), dummyEntry, buf);
            if(nRes != KErrNone)
                return nRes;

            if(dummyEntry.DataSpace() > ourEntry.DataSpace())
            {
                DBG_LOG("existing parent locator[%d] can't be replaced with a new one!", i);
                return KErrGeneral;
            }
        }
    }

    //-- here is a dangerous part: need to change VHD header and re-write parent locators in this file.
    //-- as soon as it can't be done atomically, any write failure may corrupt this file.

    //-- 4.1 change parent name, parent UUID and TS in the object's header
    {
        const uuid_t&   uuidParent = pVhdNewParent->Footer().UUID();        //-- new Parent UUID
        const uint32_t  tsParent   = pVhdNewParent->Footer().TimeStamp();   //-- new Parent TimeStamp

        Header().SetParent_UUID(uuidParent);
        Header().SetParent_TimeStamp(tsParent);

        //-- encode parent's name in UTF16, Big Endian
        size_t uLen;
        buf.Resize(TVhdHeader::KPNameLen_bytes);
        nRes = ASCII_to_UNICODE(pVhdNewParent->FileName(), buf.Ptr(), TVhdHeader::KPNameLen_bytes, uLen, EUTF_16BE);
        if(nRes != KErrNone)
            return KErrBadName;

        Header().SetParent_UName(buf.Ptr(), uLen);
    }

    //-- 4.2 replace existing parent locators in the file and change parent locator entries in the existing header
    {
        TParentLocatorEntry newEntry;

        for(uint i=0; i<TVhdHeader::KNumParentLoc; ++i)
        {
            const TParentLocatorEntry& currEntry = Header().GetParentLocatorEntry(i);
            if(currEntry.PlatCode() == TParentLocatorEntry::EPlatCode_NONE)
                continue;

            DBG_LOG("changing parent locator[%d]", i);
            newEntry = currEntry; //-- make a copy of the existing entry

            //-- generate a new parent locator and change entry parameters (locator size)
            nRes = GenerateParentLocator(FilePath(), pVhdNewParent->FilePath(), newEntry, buf);
            if(nRes != KErrNone)
                return nRes;

            ASSERT((newEntry.DataSpace() <= currEntry.DataSpace()) && (newEntry.DataOffset() == currEntry.DataOffset()));

            //-- write entry to the file
            nRes = DoRaw_WriteData((newEntry.DataOffset() >> SectorSzLog2()), newEntry.DataSpace(), buf.Ptr());
            if(nRes != (int)newEntry.DataSpace())
                return nRes; //-- this will be negative error code.

            //-- replace the parent locator entry in the header
            Header().SetParentLocatorEntry(i, newEntry);
        }
    }

   //-- 4.3 write header to the file
    DBG_LOG("Writing new header");
    buf.Resize(TVhdHeader::KSize);
    Header().Externalise(buf.Ptr(), true);
    ASSERT(Header().IsValid());

    const uint KHdrSector = Footer().DataOffset() >> SectorSzLog2();
    nRes = DoRaw_WriteData(KHdrSector, TVhdHeader::KSize, buf.Ptr());
    if(nRes != (int)TVhdHeader::KSize)
        return nRes; //-- this will be negative error code.


    ASSERT(!iParent);

    return KErrNone;
}




//--------------------------------------------------------------------
/**
    Utility function that coalesces data from the given number of parents to the opened VHD tail.
    - copies the data from the subchain of parents into the opened Tail
    - changes the Tails' parent reference to point to the new parent.
    - doesn't delete any files, leaves them where they are

    @pre    Tail VHD must be opened in RW mode
    @post   all files apart from those that become a new chain, will be closed.

    @param  apVhdTail       pointer to the object representing opened tail VHD
    @param  aChainLength    number of Tail's parents to coalesce into it.
*/
static int DoCoalesceChainIn(CVhdFileBase* apVhdTail, uint32_t aChainLength)
{
    DBG_LOG("apVhdTail:%p, aChainLength:%d", apVhdTail, aChainLength);

    int nRes;

    //-- get file name of the VHD that will become a new parent.
    //-- If it fails, we will avoid potentially long copying process
    std::string strNewParentFileName;

    {
        TVHD_Params vhdParams;
        nRes = apVhdTail->GetInfo(vhdParams, aChainLength+1);
        if(nRes != KErrNone)
        {
            DBG_LOG("Error getting %d parent info! code:%d", aChainLength, nRes);
            return nRes;
        }

        strNewParentFileName = vhdParams.vhdFileName;
    }

    //-- coalesce data from "aChainLength" parents into the given "Tail" file.
    //-- can take quite a lot of time, depending on the amount of data to copy
    nRes = apVhdTail->CoalesceDataIn(aChainLength);
    if(nRes != KErrNone)
    {
        DBG_LOG("CoalesceDataIn() error! code:%d", nRes);
        return nRes;
    }

    //-- change the Tail's parent.
    //-- this operation can't be performed atomically, because it implies changing several different bits of VHD metadata.
    //-- It can corrupt the VHD on write failure.
    //-- All previous parent VHD files will be closed after this call
    nRes = apVhdTail->ChangeParentVHD(strNewParentFileName.c_str());
    if(nRes != KErrNone)
    {
        DBG_LOG("Error changing VHD parent! code:%d", nRes);
        return nRes;
    }


    return KErrNone;
}




typedef std::vector<std::string> TNameArray;

//--------------------------------------------------------------------
/**
    Utility function that makes the best effrot to delete files left over from coalescing the chain.
    @param  aNameArray an array file names
*/
static void DoDeleteStrayParents(const TNameArray& aNameArray)
{
    for(size_t i=0; i<aNameArray.size(); ++i)
    {
        const char* pFileName = aNameArray.at(i).c_str();
        DBG_LOG("deleting stray parent VHD file:'%s'", pFileName);

        if(unlink(pFileName) == -1)
        {
            int nRes = -errno;
            DBG_LOG(" !!can't delete this file! err:%d", nRes);
            (void) nRes;
        }
    }
}


//--------------------------------------------------------------------
/**
    Coalesce a sub-chain of VHDs into the given tail and remove the files that no longer needed.
    This operation is potentially dangerous, because involves changing reference to the parent VHD on a apVhdTail.
    if write write failure occurs during changing ref. to parent, whole VHD chain can become corrupted.

    @param  apVhdTail       pointer to the object representing opened tail VHD
    @param  aChainLength    number of Tail's parents to coalesce into it.

    @return KErrNone on success, negative error code otherwise
*/
int CoalesceChain_IntoTail(CVhdFileBase* apVhdTail, uint32_t aChainLength)
{
    DBG_LOG("apVhdTail:%p, aChainLength:%d", apVhdTail, aChainLength);
    int nRes;

    ASSERT(aChainLength > 0);

    TNameArray strayParents;
    TVHD_Params vhdParams;

    //-- walk the VHD chain from the "Tail" towards "Head" checking that all necessary VHDs present
    //-- and collecting names of the files to be deleted
    for(uint i=1; i<=aChainLength; ++i)
    {
        nRes = apVhdTail->GetInfo(vhdParams, i);
        if(nRes != KErrNone)
        {
            DBG_LOG("Error getting VHD parent info! Parent number:%d, res:%d", i, nRes);
            return nRes;
        }

        strayParents.push_back(vhdParams.vhdFileName);
    }

    //-- coalesce data into the tail and change tail's parent to (aChainLength+1)
    //-- all parent VHD files that are unlinked from the old "Tail" are closed now
    nRes = DoCoalesceChainIn(apVhdTail, aChainLength);
    if(nRes != KErrNone)
        return nRes; //-- can't do much here, it's not possible to roll back to the original state

    //-- delete parent VHDs that are not in use
    DoDeleteStrayParents(strayParents);

    return KErrNone;
}



//--------------------------------------------------------------------
/**
    "Safer" coalescing routine. instead of coalescing data directly into opened VHD tail, it creates a temporary
    VHD file, coalesces data into it and then atomically replaces necessary parent VHD with a temporary one.
    If the file system provides atomic file renaming, then VHD chain is not going to be corrupted on a write failure.

    @param  apVhdTail       pointer to the object representing opened tail VHD
    @param  aChainLength    number of VHD files to coalesce into chain[aChainIdxResult], including itself
    @param  aChainIdxResult index of the VHD file in the chain (counted from the opened Tail) that will have coalesced data.

    @return KErrNone on success, negative error code otherwise
*/
int CoalesceChain_Safely(CVhdFileBase* apVhdTail, uint32_t aChainLength, uint32_t aChainIdxResult)
{

    DBG_LOG("apVhdTail:%p, aChainLength:%d, aChainIdxResult:%d", apVhdTail, aChainLength, aChainIdxResult);
    int nRes;

    if(aChainIdxResult == 0 || aChainLength < 1)
    {
        ASSERT(0);
        return KErrArgument;
    }

    //-- 1. create an empty differencing VHD file that will be used for coalescind data from the sub-chain
    //-- This file's parent VHD is chain[aChainIdxResult]

    std::string  strTmpFileName;   //-- temp. VHD file name used for coalesced data from the subchain
    std::string  strResultFileName;//-- chain[aChainIdxResult] file name.
    uuid_t       uuid_Result;      //-- chain[aChainIdxResult] UUID.

    TVHD_Params vhdParams;

    nRes = apVhdTail->GetInfo(vhdParams, aChainIdxResult);
    if(nRes != KErrNone)
    {
        DBG_LOG("Error getting VHD parent info! Parent number:%d, res:%d", aChainIdxResult, nRes);
        return nRes;
    }

    strResultFileName = vhdParams.vhdFileName;  //-- store file name of the resultant file
    uuid_copy(uuid_Result, vhdParams.vhdUUID);  //-- store its uuid

    {//-- make temp. VHD file name
        strTmpFileName = strResultFileName;
        size_t pos = strTmpFileName.rfind(KPathDelim);

        ASSERT(pos != std::string::npos);
        strTmpFileName.erase(pos+1);

        char buf1[64];
        char buf2[80];

        uuid_unparse_lower(uuid_Result, buf1);
        snprintf(buf2, sizeof(buf2), "coalesce_%d_from_%s.tmp", aChainLength, buf1);
        strTmpFileName += buf2;
    }

    //-- 2. walk the VHD chain from the "aChainIdxResult" towards "Head" checking that all necessary VHDs present
    //-- and collecting names of the files to be deleted
    TNameArray strayParents;

    for(uint i=aChainIdxResult+1; i<aChainIdxResult+aChainLength; ++i)
    {
        nRes = apVhdTail->GetInfo(vhdParams, i);
        if(nRes != KErrNone)
        {
            DBG_LOG("Error getting VHD parent info! Parent number:%d, res:%d", i, nRes);
            return nRes;
        }

        strayParents.push_back(vhdParams.vhdFileName);
    }

    //-- 3. Try creating a temporary differencing VHD with the parent [aChainIdxResult] and coalesce sub-chain into it.
    do
    {
        unlink(strTmpFileName.c_str());

        //-- 3.1 create an empty diff. file
        FillZ(vhdParams);

        vhdParams.vhdType       = EVhd_Diff;
        vhdParams.vhdModeFlags  = VHDF_OPEN_RDWR | VHDF_OPEN_DIRECTIO;
        vhdParams.vhdFileName   = strTmpFileName.c_str();
        vhdParams.vhdParentName = strResultFileName.c_str();
        uuid_copy(vhdParams.vhdUUID, uuid_Result);

        nRes =CVhdFileBase::GenerateFile(vhdParams);
        if(nRes != KErrNone)
            break;

        //-- 3.2 open this file
        CAutoClosePtr<CVhdFileBase> pVhdTmp(CVhdFileBase::CreateFromFile(vhdParams.vhdFileName, vhdParams.vhdModeFlags, nRes));
        if(!pVhdTmp.get())
        {
            ASSERT(nRes < 0);
            break;
        }

        ASSERT(nRes == KErrNone);

        nRes = pVhdTmp->Open();
        if(nRes != KErrNone)
            break;

        //-- 3.3 coalesce sub-chain into this file and change temp file parent
        nRes = DoCoalesceChainIn(pVhdTmp.get(), aChainLength);
        if(nRes != KErrNone)
            break;

        nRes = pVhdTmp->Flush();
        if(nRes != KErrNone)
            break;

        //-- CAutoClosePtr will automatically close the and delete object pointed by pVhdTmp

    }while(0);


    if(nRes != KErrNone)
    {//-- Some problem. delete the temporary file and leave everything the way it was
        DBG_LOG("Can't create temp. file for coalescing data! code:%d Deleting it...", nRes);
        unlink(strTmpFileName.c_str());
        return nRes;
    }

    //-- ??? there is a way (in theory) to rename files while keeping the chain opened.
    //-- ??? this may help to keep the files locked until the end. Is it a better solution?

    //-- 3.1 Close the existing VHD chain; it will close all files in the chain
    apVhdTail->Close();

    //-- 3.1 rename a temporary file to have the name of chain[aChainIdxResult].
    //-- if the file system guarantees file renaming to be atomic, then there is no way to corrupt VHD chain.
    nRes = rename(strTmpFileName.c_str(), strResultFileName.c_str());
    if(nRes != 0)
    {//-- Some problem. delete the temporary file and leave everything the way it was
        nRes = -errno;
        DBG_LOG("Error renamind temp. file! code:%d Deleting it...", nRes);
        unlink(strTmpFileName.c_str());
        return nRes;
    }


    //-- 3.2 re-open the new chain
    nRes = apVhdTail->Open();
    if(nRes != KErrNone)
    {
        DBG_LOG("Can't reopen the new chain! code:%d", nRes);
        return nRes;
    }

    //-- 4. delete remaining stray parent VHD.
    DoDeleteStrayParents(strayParents);

    return KErrNone;
}











