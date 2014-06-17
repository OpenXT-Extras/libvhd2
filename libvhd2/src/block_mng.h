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
    @file VHD block management classes that handle BAT, sector bitmaps etc.
*/


#ifndef __BLOCK_MANAGEMENT_H__
#define __BLOCK_MANAGEMENT_H__

#include "vhd.h"

#include <list>
using std::list;

//--------------------------------------------------------------------
/**
    This class represents a VHD Block Allocation Table (BAT), implements BAT caching etc.
    Not intended for derivation.

    @todo !!!! Currently implements very simple WB-caching, it caches WHOLE BAT in one block. ?? make a paged cache to reduce media hits?
    or mmap BAT and use system paging ?
*/
class CBat
{
 public:
    CBat(CVhdDynDiffBase& aVhd);
   ~CBat();

    TBatEntry ReadEntry(uint32_t aIndex);
    int WriteEntry(uint32_t aIndex, TBatEntry aEntry);

    int Flush();
    void Close(bool aForceClose = false);
    void InvalidateCache(bool aIgnoreDirty = false);

    /** object state, mostly BAT cache state */
    enum TState
    {
        EInvalid, ///< Invalid state, BAT cache can't be used
        EClean,   ///< Valid state, BAT cache is clean and coherent with the media
        EDirty    ///< Valid state, BAT cache is dirty
    };

    TState State() const            {return iState;}

 private:
    CBat(const CBat&);
    CBat& operator=(const CBat&);

 private:
    //----------------

    bool StateValid() const         {return State()==EClean || State()==EDirty;}
    void SetState(TState aState)    {iState = aState;}


    inline bool BatIndexValid(uint32_t aIndex) const;

    uint32_t SectorSzLog2() const {return KDefSecSizeLog2;}
    uint32_t SectorSize()   const {return KDefSecSize;}

    void CreateBatCache();
    int  ReadBAT();
    int  WriteBAT();

 private:

    CVhdDynDiffBase&    iVhd;       ///< ref. to the object representing a Dynamic or Differencing VHD file
    uint32_t            iBatSector; ///< BAT position in the file (sector number)
    uint32_t            iMaxEntries;///< Max. entries in BAT

    TState              iState;     ///< object state
    uint32_t*           iBatBuffer; ///< BAT buffer that caches whole BAT ??? make paged cache ??

};


//--------------------------------------------------------------------

/** Sector bitmap state */
enum TSectorBitmapState
{
    ESB_Invalid,        ///< Invalid state, bitmap isn't initialised or not in the cache
    ESB_Clean,          ///< Bitmap buffer is clean and coherent with the media
    ESB_Dirty,          ///< Bitmap buffer is dirty, needs flushing onto the media
    ESB_FullyMapped,    ///< Bitmap contains all '1's, contents isn't cached
    ESB_FullyUnmapped   ///< Bitmap contains all '0's, contents isn't cached
};

class CSectorBmpPage;

//--------------------------------------------------------------------
/**
    Provides interface to the Sector Allocation Bitmaps in the VHD file.
    Maintains the bitmaps LRU cache.
    Not intended for derivation.
*/
class CSectorMapper
{

 public:
    CSectorMapper(CVhdDynDiffBase& aVhd);
   ~CSectorMapper();

    int Flush();
    void InvalidateCache(bool aIgnoreDirty = false);
    void Close(bool aForceClose = false);

    //----- sector allocation bitmap-related interface
    TSectorBitmapState SetSectorAllocBits(TBatEntry aBlockSector, uint32_t aSectorNumber, uint32_t aNumBits);
    TSectorBitmapState ResetSectorAllocBits(TBatEntry aBlockSector, uint32_t aSectorNumber, uint32_t aNumBits);

    uint32_t GetSectorAllocBit(TBatEntry aBlockSector, uint32_t aSectorNumber);
    const CSectorBmpPage* GetSectorAllocBitmap(TBatEntry aBlockSector);
    //------------------------------------------------


    /** object states */
    enum TState
    {
        EInvalid,   ///< Invalid state
        EClean,     ///< Bitmaps cache is is clean and coherent with the media for all bitmaps
        EDirty,     ///< Bitmap cache is dirty, needs flushing onto the media
    };

    TState State() const            {return iState;}
    void SetState(TState aState)    {iState = aState;}


    uint32_t BmpSizeInSectors() const {return iVhd.SBmp_SizeInSectors();}           ///< @return Block bitmap size in sectors
    uint32_t BmpSizeInBytes()   const {return BmpSizeInSectors()<<SectorSzLog2();}  ///< @return Block bitmap size in bytes
    uint32_t BmpSizeInBits()    const {return BmpSizeInBytes()<<KBitsInByteLog2;}   ///< @return Block bitmap size in bits



 private:
    CSectorMapper();
    CSectorMapper(CSectorMapper&);
    CSectorMapper& operator=(const CSectorMapper&);


    uint32_t SectorSzLog2() const {return KDefSecSizeLog2;}
    uint32_t SectorSize()   const {return KDefSecSize;}


    CSectorBmpPage* DoFindCachedPage(TBatEntry aBlockSector, bool aMakeMRU = false);
    CSectorBmpPage* DoGetPopulatedPage(TBatEntry aBlockSector);
    int DoFlushPage(CSectorBmpPage* apPage);


    typedef list<CSectorBmpPage*>       TPageList;
    typedef TPageList::iterator         TPListItr;
    typedef TPageList::const_iterator   TPListConstItr;

 private:
    CVhdDynDiffBase&    iVhd;       ///< ref. to the object representing a Dynamic or Differencing VHD file
    TState              iState;     ///< object state
    TPageList           iPages;     ///< list of the cache pages
};


//--------------------------------------------------------------------
/**
    Represents VHD sector bitmap that resides in the beginning of the block in dynamic and differencing VHDs.
    This class is supposed to be used only within context of the CSectorMapper.
    Not intended for derivation.
*/
class CSectorBmpPage
{
 public:

    CSectorBmpPage(CSectorMapper& aParent, TBatEntry aBlockSector = 0);
   ~CSectorBmpPage();

    void Close(bool aForceClose = false);

    TSectorBitmapState State() const            {return iState;}
    void SetState(TSectorBitmapState aState)    {iState = aState;}

    TBatEntry BlockSector() const               {return iBlockSector;}
    void SetBlockSector(TBatEntry aNewBlockSector);

    void InvalidateCache(bool aIgnoreDirty = false);

    //----- sector allocation bitmap-related interface
    uint32_t GetAllocBmpBit(uint32_t aBitNumber) const;
    int GetAllocBitmap(CBitVector& aBitmap) const;

    inline uint32_t GetAllocBmpBit_Raw(uint32_t aBitNumber) const;
    inline const CBitVector& GetAllocBitmap_Raw() const;

    TSectorBitmapState SetAllocBmpBits(uint32_t aBitNumber, uint32_t aNumBits);
    TSectorBitmapState ResetAllocBmpBits(uint32_t aBitNumber, uint32_t aNumBits);
    //------------------------------------------------

    TSectorBitmapState ImportData(void* apBuf);
    TSectorBitmapState ExportData(void* apBuf);



 private:
    CSectorBmpPage();
    CSectorBmpPage(CSectorBmpPage&);
    CSectorBmpPage& operator=(const CSectorBmpPage&);

    uint32_t SectorSzLog2() const {return KDefSecSizeLog2;}

    TSectorBitmapState DoProcessDataBuffer(void* apData, uint32_t aNumBits) const;
    int DoCreateAllocBitmap();


 private:
    CSectorMapper&      iParent;        ///< parent objects owning this one
    TSectorBitmapState  iState;         ///< object state
    TBatEntry           iBlockSector;   ///< sector of the block this bitmaps belongs to
    CBitVector          iAllocBitmap;   ///< sector allocation bitmap
};

//--------------------------------------------------------------------
/**
    a "Raw" version of GetBit(), without object state checks. faster, but
    the caller must be sure that the iAllocBitmap is in proper state
*/
uint32_t CSectorBmpPage::GetAllocBmpBit_Raw(uint32_t aBitNumber) const
{
    return iAllocBitmap[aBitNumber];
}

/**
    a "Raw" version of GetBitmap(), without object state checks. faster, but
    the caller must be sure that the iAllocBitmap is in proper state
*/
const CBitVector& CSectorBmpPage::GetAllocBitmap_Raw() const
{
    return iAllocBitmap;
}



#endif // __BLOCK_MANAGEMENT_H__

