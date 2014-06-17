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

    @file imlementation of the DYNAMIC VHD file stuff
*/

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "vhd.h"
#include "block_mng.h"


//####################################################################
//#     CVhdFileDynamic class implementation
//####################################################################

//--------------------------------------------------------------------
/**
    constructor.
    @param  apFooter a valid VHD footer
    @param  apHeader a valid VHD header
*/
CVhdFileDynamic::CVhdFileDynamic(const TVhdFooter* apFooter, const TVhdHeader* apHeader)
                :CVhdDynDiffBase(apFooter, apHeader)
{
    DBG_LOG("CVhdFileDynamic::CVhdFileDynamic[0x%p]", this);
    //-- process footer
    ASSERT(Footer().IsValid());
    ASSERT(Footer().DiskType() == EVhd_Dynamic);
}

//--------------------------------------------------------------------
/**
    Implements "Open" semantics for the dynamic VHD file
    @return standard error code, 0 on success.
*/
int CVhdFileDynamic::Open()
{
    DBG_LOG("CVhdFileDynamic::Open()[0x%p]", this);

    int nRes = CVhdDynDiffBase::Open();
    if(nRes != KErrNone)
    {
        DBG_LOG("CVhdFileDynamic::Open() error! code:%d", nRes);
        return nRes;
    }

    ASSERT(ipBAT && ipSectorMapper);

    SetState(EOpened);

    //-- process VHDF_OPMODE_PURE_BLOCKS flag if required. This is to ensure that
    //-- sector bitmaps in all blocks have all bits set. This may require file modification and
    //-- can take some time. But further access to the file will be faster, because bitmaps won't be touched
    if(!ReadOnly() && BlockPureMode())
    {
        InvalidateCache(); //-- invalidate bitmaps cache, just in case
        nRes = ProcessPureBlocksMode();
        if(nRes == KErrNone)
        {
            nRes = Flush();
            ASSERT(nRes == KErrNone);

            //-- close bitmap cache, it should not be needed at all afterwards.
            //-- keep BAT cache if it is populated, it is likely to be needed
            ipSectorMapper->Close();
        }
        else
        {
            DBG_LOG("ProcessPureBlocksMode() error! code:%d", nRes);
            InvalidateCache(true); //-- invalidate all caches, try not to flush dirty data on error
            Close();
            return nRes;
        }
    }

    return nRes;
}

//--------------------------------------------------------------------
/**
    Get some VHD parameters
    @param  aVhdParams out: filled in parameters structure
    @param  aParentNo       number of the parent VHD file. 0 refers to _this_ file
*/
int CVhdFileDynamic::GetInfo(TVHD_Params& aVhdInfo, uint32_t aParentNo) const
{
    if(aParentNo != 0)
        return KErrNotFound;

    return CVhdDynDiffBase::GetInfo(aVhdInfo, aParentNo);
}


//--------------------------------------------------------------------
/**
    Ensures that sector bitmaps in all present blocks have all bits set.
    Checks every bitmap; if finds '0' bit there, checks the corresponding sector contents and zero-fills it if necessary.
    As a result all bitmaps in the file will have all bits set.

    @return	KErrNone on success; negative error code otherwise
*/
int CVhdFileDynamic::ProcessPureBlocksMode()
{
    DBG_LOG("CVhdFileDynamic::ProcessPureBlocksMode[0x%p]",this);
    ASSERT(State() == EOpened);
    ASSERT((!ReadOnly() && BlockPureMode()));

    const uint KBlocks = Header().MaxBatEntries();
    const uint KBmpSizeInBits = SBmp_SizeInSectors() << (SectorSzLog2() + KBitsInByteLog2);

    CBitVector blkBitmap; //-- block sectors bitmap

    for(uint currBlock =0; currBlock<KBlocks; ++currBlock)
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

        //-- go through block bitmap, checking and zero-filling sectors that have corresponding '0' bits
        if(!blkBitmap.Size())
            blkBitmap.New(KBmpSizeInBits);

        pBitmap->GetAllocBitmap(blkBitmap); //-- get block's sector allocation bitmap

        TBitExtentFinder extFinder(blkBitmap);

        for(;;)
        {//-- find extents of '0' bits in the block bitmap and check/zero fill corresponding places in the physical file

            if(!extFinder.FindExtent())
                break;

            if(extFinder.ExtBitVal())
                continue; //-- extent of '1's, not interested

            const uint KFileSectorP = KBlockSector + SBmp_SizeInSectors() + extFinder.ExtStartPos();

            int nRes = DoRaw_CheckMediaFill(KFileSectorP, extFinder.ExtLen(), 0);
            if(nRes == KErrNone)
                continue;   //-- ok, whole sector is filled with 0
            else
                if(nRes == KErrNotFound)
                {//-- garbage in the sector, zero-fill it
                    nRes = DoRaw_FillMedia(KFileSectorP, extFinder.ExtLen(), 0);
                    if(nRes != KErrNone)
                        return nRes;
                }
            else
                return nRes; //-- some serious error happened;

        }

        //-- all sectors in this block either contain valid data or zero-filled. set all bits in the bitmap
        const TSectorBitmapState sectBmpState = ipSectorMapper->SetSectorAllocBits(KBlockSector, 0, KBmpSizeInBits);
        if(sectBmpState == ESB_Invalid)
        {//-- something really bad happened
            ASSERT(0);
            return KErrCorrupt;
        }

    }


    return KErrNone;
}


//--------------------------------------------------------------------
/**
    Read a sector extent from given _single_ block in the VHD file.

    @param  aParams parameters, describing the operation. Some of them will be adjusted on completion.
    @return	KErrNone on success, negative value corresponding system error code otherwise.
*/
int CVhdFileDynamic::DoReadSectorsFromBlock(TBlkOpParams &aParams)
{


    const uint32_t KStartSectorL  = aParams.iCurrSectorL;
    const uint32_t KSectorsToRead = aParams.iNumSectors;
    const uint32_t KBytesToRead   = KSectorsToRead << SectorSzLog2();

    //-- get BAT entry.
    const TBatEntry KBlockSector = ipBAT->ReadEntry(aParams.iCurrBlock); //-- Block starting sector in the file

    if(KBlockSector == KBatEntry_Unused)
    {//-- if whole block isn't present, then simulate reading zeroes
        FillZ(aParams.ipData, KBytesToRead);
    }
    else
    {//-- block is present in the VHD, read sectors. Sector bitmap contains '1' if the sector contains valid data and '0' if the data never been written there or discarded by TRIM
        ASSERT(BatEntryValid(KBlockSector));

        TSectorBitmapState bmpState = ESB_Invalid;
        const CSectorBmpPage* pBitmap = NULL;

        if(BlockPureMode())
        {//-- if we are operating in PURE mode, it is _guaranteed_ that the bitmap contains all bits set to 1 and all sectors contain valid data
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
            const uint32_t startDataSecP = KBlockSector + KBitmapSectors + SectorInBlock(KStartSectorL); //-- Physical sector number in the _file_
            const int nRes = DoRaw_ReadData(startDataSecP, KBytesToRead, aParams.ipData);
            if(nRes <0)
                return nRes;//-- this is the error code

            ASSERT(nRes == (int)KBytesToRead);
        }
        else if(bmpState == ESB_FullyUnmapped)
        {//-- all '0' bits in the bitmap, simulate reading zeros
            FillZ(aParams.ipData, KBytesToRead);
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
                    const int nRes = DoRaw_ReadData(sectorP, extBytes, pBuf);
                    if(nRes < 0)
                        return nRes;

                    ASSERT(nRes == (int)extBytes);
                }
                else
                {//-- found an extent of '0's, simulate reading zeros
                    FillZ(pBuf, extBytes);
                }

                sectorInBlock += extSectors;
                sectorP += extSectors;
                pBuf    += extBytes;

            }//for(;;)

        }

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
int CVhdFileDynamic::DoWriteSectorsToBlock(TBlkOpParams &aParams)
{

    int nRes;

    const uint32_t KStartSectorL  = aParams.iCurrSectorL;
    const uint32_t KSectorsToWrite= aParams.iNumSectors;
    const uint32_t KBytesToWrite  = KSectorsToWrite << SectorSzLog2();
    const uint32_t KBitmapSectors = SBmp_SizeInSectors(); //-- block allocation bitmap size, in sectors

    bool bSetAllBmpBits = false; //-- if true, then ALL block bitmap bits will be set

    //============================================================
    //-- get BAT entry, find out block starting sector
    TBatEntry blockSector = ipBAT->ReadEntry(aParams.iCurrBlock);


    if(blockSector == KBatEntry_Unused)
    {//-- the block isn't present; need to extend VHD file by one block

        //-- 1. append a block without zero-filling it

        //-- 1.1 find out if we need to have all bits in the alloc. bitmap set. If true, then sector bitmap will have all bits set
        bSetAllBmpBits = (BlockPureMode() || KDynVhd_CreateFullyMappedBlock)   //-- in 'pure mode' everything is done to avoid using bitmap caches for the performance sake
                          && (!TrimEnabled());                                 //-- when using TRIM '0' bits indicate sectors that can be discarded and should be read as zeros.


        nRes = AppendBlock(blockSector, bSetAllBmpBits, false);
        if(nRes < 0)
            return nRes;

        {//-- 2. zero-fill bits of the block if necessary
            const uint32_t chunk1_SecStart_P = blockSector + KBitmapSectors;//-- physical starting sector of chunk1
            const uint32_t chunk1_SecLen     = SectorInBlock(KStartSectorL);   //-- number of sectors in chunk1

            const uint32_t chunk2_SecStart_P = chunk1_SecStart_P + chunk1_SecLen + KSectorsToWrite; //-- physical starting sector of chunk2
            const uint32_t chunk2_SecLen     = SectorsPerBlock()- (chunk1_SecLen + KSectorsToWrite);//-- number of sectors in chunk2

            //-- 1. zero fill extent before block of data to be written later
            nRes = DoRaw_FillMedia(chunk1_SecStart_P, chunk1_SecLen, 0x00);
            if(nRes != KErrNone)
                return nRes;

            //-- 2. zero fill extent after block of our data
            nRes = DoRaw_FillMedia(chunk2_SecStart_P, chunk2_SecLen, 0x00);
            if(nRes != KErrNone)
                return nRes;
        }

        //-- 3. place entry to BAT cache
        nRes = ipBAT->WriteEntry(aParams.iCurrBlock, blockSector);
        if(nRes < 0)
        {
            ASSERT(0);
            return nRes;
        }

        aParams.iFlushMetadata = true; //-- indicate that the metadata caches need flushing
    }


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
    {
        //-- set the corresponding bits in the sector bitmap to indicate sectors written
        //-- if bSetAllBmpBits == true, then set all bits in the bitmap.
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
int CVhdFileDynamic::DiscardSectors(uint32_t aStartSector, int aSectors)
{
    DBG_LOG("#--- CVhdFileDynamic::DiscardSectors[0x%p] startSec:%d, num:%d",this, aStartSector, aSectors);

    if(ReadOnly())
        return -EBADF;

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
    Get block's sector bitmap
    '1' bit in the bitmap corresponds to the "existing" sector in the block, where data have been written.
    '0' corresponds to the sector that never been touched and containing all zeroes.

    @param      aLogicalBlockNumber logical block number
    @param      aSrcBitmap out: bit vector representing block bitmap.


    @return     KErrNone on success, error code otherwise
*/
int CVhdFileDynamic::GetBlockBitmap(uint32_t aLogicalBlockNumber, CBitVector& aSrcBitmap) const
{
    ASSERT(IsBlockPresent(aLogicalBlockNumber));

    if(SectorsPerBlock() !=  aSrcBitmap.Size())
        Fault(ESecMap_InvalidSectorNumber);

    //-- For the dynamic VHD and all sectors in the block are "present".
    //-- sectors with corresponding '0' in the bitmap must contain all zeroes
    aSrcBitmap.Fill(1);
    return KErrNone;
}














