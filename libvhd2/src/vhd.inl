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
    @file inline methods implementation for the things defined in vhd.h file
*/


#ifndef __VHD_INL__
#define __VHD_INL__


//####################################################################
/**
    Get disk type from the footer data.
    Though TWhdType enum contains supported types only, this method can return any value that is recorded in the VHD footer.
    It is responsibility of the upper level to decide what to do with the type.
*/
TVhdType TVhdFooter::DiskType() const
{
    ASSERT(ChkSumValid());
    return (TVhdType)iDiskType;
}


uint64_t TVhdFooter::DataOffset() const
{
    ASSERT(ChkSumValid());
    return iDataOffset;
}


uint64_t TVhdFooter::CurrDiskSizeInBytes()  const
{
    ASSERT(ChkSumValid());
    return iCurrSize;
}

inline uint32_t TVhdFooter::DiskGeometry() const
{
    ASSERT(ChkSumValid());
    return iDiskGeometry;
}


//####################################################################
/**
    Get a pointer to the CVhdFileBase object by its VHD handle. Takes O(1) time.
    @param  aVhdHandle a valid VHD handle
    @return pointer to the associated object; NULL if the handle isn't valid
*/
CVhdFileBase* CHandleMapper::GetPtrByHandle(TVhdHandle aVhdHandle) const
{
    --aVhdHandle;
    if(aVhdHandle < 0 || aVhdHandle >= (int)MaxClients())
    {
        Fault(EIndexOutOfRange);
    }

    return iPtrArray[aVhdHandle];
}

//####################################################################

/**
    @return true if the VHD volume is opened as Read-only, @see VHDF_OPEN_RDONLY, VHDF_OPEN_RDWR
*/
bool CVhdFileBase::ReadOnly() const
{
    ASSERT(State()==EOpened);
    return !(iModeFlags & VHDF_OPEN_RDWR);
}

/**
    @return true if the VHD is operated in "pure block mode"
*/
bool CVhdFileBase::BlockPureMode() const
{
    ASSERT(State()==EOpened);
    return (iModeFlags & VHDF_OPMODE_PURE_BLOCKS);
}

/**
    @return true if the TRIM operations are enabled
*/
bool CVhdFileBase::TrimEnabled() const
{
    ASSERT(State()==EOpened);
    return (iModeFlags & VHDF_OPEN_ENABLE_TRIM);
}

//####################################################################
/** @return Log2(sectors per block) for dynamic & diff. VHDs*/
uint32_t CVhdDynDiffBase::SectorsPerBlockLog2() const
{
    ASSERT(State()==EOpened);
    ASSERT(iSectPerBlockLog2 > KDefSecSizeLog2);

    return iSectPerBlockLog2;
}

/** @return sectors per block for dynamic & diff. VHDs*/
uint32_t CVhdDynDiffBase::SectorsPerBlock() const
{
    return 1<<SectorsPerBlockLog2();
}

//--------------------------------------------------------------------
/**
    @return VHD Size in Sectors, calculated from CHS value in the Footer
*/
uint32_t CVhdFileBase::VhdSizeInSectors() const
{
    ASSERT(State() == EOpened);
    ASSERT(iVhdSizeSec > 1);
    return iVhdSizeSec;
}

//-----------------------------------------------------------------------------
/**
    @param  aSectorNumber absolute sector number
    @return VHD block number this sector belongs to
*/
uint32_t CVhdDynDiffBase::SectorToBlockNumber(uint32_t aSectorNumber) const
{
    ASSERT(State()==EOpened);
    return aSectorNumber >> SectorsPerBlockLog2();
}

/**
    @param  aSectorNumber absolute sector number
    @return relative sector number within a block = (0.. SectorsPerBlock()-1)
*/
uint32_t CVhdDynDiffBase::SectorInBlock(uint32_t aSectorNumber) const
{
    ASSERT(State()==EOpened);
    return (aSectorNumber & (SectorsPerBlock() - 1));
}

//--------------------------------------------------------------------
/** @return true if the entry aEntry looks valid */
bool CVhdDynDiffBase::BatEntryValid(TBatEntry aEntry) const
{
    ASSERT(State() == EOpened);

    const uint32_t batSectors   = ((iHeader.MaxBatEntries()*sizeof(TBatEntry)) + SectorSize() - 1) >> SectorSzLog2(); //-- BAT size in sectors
    const uint32_t lowSecBound  = batSectors + (iHeader.BatOffset() >> SectorSzLog2());  //-- 1st sector after BAT

    return (aEntry >= lowSecBound);
}



#endif //__VHD_INL__





