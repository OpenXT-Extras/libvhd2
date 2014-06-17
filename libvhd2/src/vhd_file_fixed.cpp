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

    @file imlementation of the FIXED VHD file stuff
*/

#include <unistd.h>
#include <errno.h>

#include "vhd.h"

//####################################################################
//#     CVhdFileFixed class implementation
//####################################################################

//--------------------------------------------------------------------

CVhdFileFixed::CVhdFileFixed(const TVhdFooter* apFooter)
              :CVhdFileBase(apFooter)
{
    DBG_LOG("CVhdFileFixed::CVhdFileFixed[0x%p]", this);

    //-- process footer
    ASSERT(Footer().IsValid());
    ASSERT(Footer().DiskType() == EVhd_Fixed);
}


//--------------------------------------------------------------------
/**
    Implements "Open" semantics for the fixed VHD file.
    Not much to do, merely changes the state to EOpened.

    @return standard error code, 0 on success.
*/
int  CVhdFileFixed::Open()
{
    DBG_LOG("CVhdFileFixed::Open()[0x%p]", this);

    if(State() != EInitialised)
    {
        ASSERT(0);
        return KErrAlreadyExists;
    }

    if(!Footer().IsValid() || Footer().DiskType() != EVhd_Fixed)
    {
        ASSERT(0);
        return KErrCorrupt;
    }

    //-- open the base object
    int nRes = CVhdFileBase::Open();
    if(nRes == KErrNone)
    {
        SetState(EOpened);
    }
    else
    {
        DBG_LOG("CVhdFileFixed::Open() error! code:%d", nRes);
    }

    return nRes;
}

//--------------------------------------------------------------------
/**
    Get some VHD parameters
    @param  aVhdParams out: filled in parameters structure
    @param  aParentNo       number of the parent VHD file. 0 refers to _this_ file
*/
int CVhdFileFixed::GetInfo(TVHD_Params& aVhdInfo, uint32_t aParentNo) const
{
    if(aParentNo != 0)
        return KErrNotFound;

    return CVhdFileBase::GetInfo(aVhdInfo, aParentNo);
}



//--------------------------------------------------------------------
/**
    Read a number of sectors from the VHD file. Sector numbers are logical, i.e. 0..VhdSizeInSectors()

	@param	aStartSector	starting logical sector.
	@param	aSectors		number of logical sectors to read 0..LONG_MAX
	@param	apBuffer		out: read data
    @param  aBufSize        buffer size in bytes

    @return	positive number of read sectors on success, negative value corresponding system error code otherwise.
*/
int CVhdFileFixed::ReadSectors(uint32_t aStartSector, int aSectors, void* apBuffer, uint32_t aBufSize)
{

    DBG_LOG("#--- CVhdFileFixed::ReadSectors[0x%p] startSec:%d, num:%d",this, aStartSector, aSectors);

    //-- check arguments and adjust number of sectors to read if necessary
    int nRes = DoCheckRW_Args(aStartSector, aSectors, aBufSize);
    if(nRes <= 0 )
        return nRes; //-- something is wrong with the arguments

    //-- simple linear read
    const uint32_t  KSectorsToRead     = nRes;
    const int       KBytesToRead = KSectorsToRead << SectorSzLog2();

    const int bytesRead = DoRaw_ReadData(aStartSector, KBytesToRead, apBuffer);

    if(bytesRead < 0)
        return bytesRead; //-- this is the error code

    ASSERT(bytesRead == KBytesToRead);

    return nRes;
}

//--------------------------------------------------------------------
/**
	Write a number of sectors to the VHD file. Sector numbers are logical, i.e. 0..VhdSizeInSectors()
	@param	aStartSector	starting logical sector.
	@param	aSectors		number of logical sectors to write 0..LONG_MAX
	@param	apBuffer		in: data to write
    @param  aBufSize        buffer size in bytes

	@return	positive number of written sectors on success, negative value corresponding system error code otherwise.
*/
int CVhdFileFixed::WriteSectors(uint32_t aStartSector, int aSectors, const void* apBuffer, uint32_t aBufSize)
{
    DBG_LOG("#--- CVhdFileFixed::WriteSectors[0x%p] startSec:%d, num:%d",this, aStartSector, aSectors);

    if(ReadOnly())
        return -EBADF;

    //-- check arguments and adjust number of sectors to write if necessary
    int nRes = DoCheckRW_Args(aStartSector, aSectors, aBufSize);
    if(nRes <= 0 )
        return nRes; //-- something is wrong with the arguments

    //-- simple linear write
    const uint32_t  KSectorsToWrite = nRes;
    const int       KBytesToWrite = KSectorsToWrite << SectorSzLog2();

    const int bytesWritten = DoRaw_WriteData(aStartSector, KBytesToWrite, apBuffer);
    if(bytesWritten < 0)
        return bytesWritten; //-- this is the error code

    ASSERT(bytesWritten == KBytesToWrite);

    return nRes;
}


//--------------------------------------------------------------------
/**
    Mark an extent of sectors as "TRIMmed" or "Discarded". Such sectors will be treated as no longer containing a valid information.
    Later on, some smart things can be done using information about "discarded" sectors. It depends on VHD type.

	@param	aStartSector	starting logical sector.
	@param	aSectors		number of logical sectors to "discard" 0..LONG_MAX

	@return	positive number of written sectors on success, negative value corresponding system error code otherwise.
*/
int CVhdFileFixed::DiscardSectors(uint32_t aStartSector, int aSectors)
{
    DBG_LOG("#--- CVhdFileFixed::DiscardSectors[0x%p] startSec:%d, num:%d",this, aStartSector, aSectors);

    if(ReadOnly())
        return -EBADF;

    //-- Nothing to do, actually. Simulate normal completion
    return KErrNone;
}


//--------------------------------------------------------------------
/**
    Check if VHD block is physically present in the file. It is mostly applicable for the VHD types with BAT

    @param      aLogicalBlockNumber logical block number
    @return     true if the block is physically present in the file
*/
bool CVhdFileFixed::IsBlockPresent(uint32_t aLogicalBlockNumber) const
{   //-- For fixed VHD "block" is always present.
    return true;
}

//--------------------------------------------------------------------
/**
    Get block's sector bitmap
    '1' bit in the bitmap corresponds to the "existing" sector in the block. meaning depends on the VHD type.

    @param      aLogicalBlockNumber logical block number
    @param      aSrcBitmap          out: block bitmap. All "blocks" in the fixed VHD are always present.
    @return     KErrNone on success, error code otherwise
*/
int CVhdFileFixed::GetBlockBitmap(uint32_t aLogicalBlockNumber, CBitVector& aSrcBitmap) const
{   //-- For fixed VHD "block" is always present, and all sectors in the block "present" as well.
    aSrcBitmap.Fill(1);
    return KErrNone;
}














