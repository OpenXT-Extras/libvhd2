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

    @file imlementation of some VHD block management classes that handle BAT, sector bitmaps etc.
*/

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "vhd.h"
#include "block_mng.h"

ASSERT_COMPILE(KMaxCached_SectorBitmaps > 0 && KMaxCached_SectorBitmaps < 1024);

//####################################################################
//#  CBat class implementation
//####################################################################

//--------------------------------------------------------------------
/**
    Constructor.
    @param  CVhdDynDiffBase reference to the class representing common functionality for dynamic & differencing VHDs
*/
CBat::CBat(CVhdDynDiffBase& aVhd)
     :iVhd(aVhd)
{
    const uint64_t BatOffset = iVhd.Header().BatOffset();
    iMaxEntries= iVhd.Header().MaxBatEntries();

    DBG_LOG("CBat::CBat() BatOffset:0x%llx, maxEntries:%d", BatOffset, iMaxEntries);

    ASSERT(BatOffset && ((BatOffset & (KDefSecSize-1)) == 0)); //-- check sector alignment
    ASSERT(iMaxEntries);

    iBatSector = (uint32_t)(BatOffset >> SectorSzLog2());
    iBatBuffer = NULL;
    iState = EInvalid;
}


//--------------------------------------------------------------------
/**
    Destructor. This object can be deleted only in EInvalid state i.e before using it or after
    explicit Close() call
*/
CBat::~CBat()
{
    DBG_LOG("CBat::~CBat()");

    if(State() != EInvalid)
        Fault(EInvalidState);

    ASSERT(!iBatBuffer);
}

//--------------------------------------------------------------------
/**
    Close the object, destroy cache
    @param  aForceClose     if true, ignores dirty data. not for a normal use
    @pre the cache must not contain dirty pages. Flush() them before calling this method
*/
void CBat::Close(bool aForceClose /*=false*/)
{
    DBG_LOG("CBat::Close(%d) state:%d", aForceClose, State());

    InvalidateCache(aForceClose); //-- does some checks as well

    //-- destroy BAT cache
    delete [] iBatBuffer;
    iBatBuffer = NULL;
}

//--------------------------------------------------------------------
/**
    Invalidates cache data. If the client will try to access data with cache invalid, it will result in
    re-reading data from the media to the cache.
    Does not deallocate cache RAM, just marks data as invalid.

    @param  aIgnoreDirty    if true, ignores dirty data. not for a normal use
    @pre the cache must not be dirty; @see Flush()
*/
void CBat::InvalidateCache(bool aIgnoreDirty /*=false*/)
{
    DBG_LOG("CBat::InvalidateCache(%d) state:%d", aIgnoreDirty, State());
    if(State() == EDirty && !aIgnoreDirty)
    {//-- an attempt to close / destroy cache with dirty data
        Fault(EBat_DestroyingDirty);
    }

    SetState(EInvalid);
}


//--------------------------------------------------------------------
/**
    Creates BAT cache if it doesn't exist.
*/
void CBat::CreateBatCache()
{
    DBG_LOG("CBat::CreateBatCache() maxEntries:%d", iMaxEntries);
    ASSERT(State() == EInvalid);

    if(iBatBuffer)
    {//-- strange, the cache seems to already exist
        Fault(EAlreadyExists);
    }

    //-- allocate buffer for the cache.
    iBatBuffer = new uint32_t [iMaxEntries];
    FillZ(iBatBuffer, sizeof(TBatEntry)*iMaxEntries);
}

//--------------------------------------------------------------------
/**
    Read raw BAT data from the media into the cache.
    @return KErrNone on success, negative error code otherwise
*/
int CBat::ReadBAT()
{
    DBG_LOG("CBat::ReadBAT()");

    //-- 1. create a cache if it doesn't exist
    if(!iBatBuffer)
        CreateBatCache();

    if(State() == EDirty)
    {//-- an attempt to discard cache with dirty data
        Fault(EBat_DestroyingDirty);
    }

    //-- 2. read whole BAT to the cache buffer
    const int  bytesToRead = sizeof(TBatEntry)*iMaxEntries;
    const int  bytesRead = iVhd.DoRaw_ReadData(iBatSector, bytesToRead, iBatBuffer);
    if(bytesRead != bytesToRead)
        return bytesRead; //-- error code in this case

    //-- 3. set correct state
    SetState(EClean);
    return KErrNone;
}

//--------------------------------------------------------------------
/**
    Write raw BAT data to the media from the cache.
    @return KErrNone on success, negative error code otherwise
*/
int CBat::WriteBAT()
{
    DBG_LOG("CBat::WriteBAT()");

    if(!iBatBuffer || State() != EDirty)
    {
        ASSERT(0);
        return KErrNone;
    }

    //-- write whole BAT buffer to the media
    const int  bytesToWrite = sizeof(TBatEntry)*iMaxEntries;
    const int  bytesWritten = iVhd.DoRaw_WriteData(iBatSector, bytesToWrite, iBatBuffer);
    if(bytesWritten != bytesToWrite)
        return bytesWritten; //-- error code in this case

    //-- set correct state
    SetState(EClean);
    return KErrNone;
}

//--------------------------------------------------------------------
/**
    Read BAT entry from the cache. This call may cause creating cache an reading data into it.
    The entry value will be converted to the proper endianness.

    @param  aIndex   a valid index in BAT
    @return KErrNone on success, negative error code otherwise
*/
TBatEntry CBat::ReadEntry(uint32_t aIndex)
{
    DBG_LOG("CBat::ReadEntry(%d)", aIndex);

    if(!BatIndexValid(aIndex))
    {//-- index in BAT is invalid
        ASSERT(0);
        return KBatEntry_Invalid;
    }

    if(!StateValid())
    {//-- Invalid state; cache isn't created yet or invalidated explicitly
        int nRes = ReadBAT(); //-- read BAT from the media
        if(nRes != KErrNone)
        {
            return nRes;
        }
    }

    ASSERT(StateValid());

    //-- get entry directly from the cache and change endianness if necessary
    TBatEntry entry = iBatBuffer[aIndex];
#if __BYTE_ORDER == __LITTLE_ENDIAN
    entry = __bswap_32(entry);
#endif

    return entry;
}

//--------------------------------------------------------------------
/**
    Write BAT entry to the cache. This call may cause creating cache an reading data into it.
    This call may not cause any writes to the media depending on the BAT cache type.
    The entry value will be converted to the proper endianness on the media.

    @param  aIndex   a valid index in BAT
    @return KErrNone on success, negative error code otherwise

    @post   marks whole BAT cache as dirty
*/
int CBat::WriteEntry(uint32_t aIndex, TBatEntry aEntry)
{
    if(!BatIndexValid(aIndex))
    {//-- index in BAT is invalid
        ASSERT(0);
        return KErrCorrupt;
    }

    //-- the cache might be invalidated
    if(!StateValid())
    {//-- Invalid state; cache isn't created yet or invalidated explicitly
        int nRes = ReadBAT(); //-- read BAT from the media
        if(nRes != KErrNone)
        {
            return nRes;
        }
    }

    ASSERT(StateValid());

    //-- change endianness if necessary
#if __BYTE_ORDER == __LITTLE_ENDIAN
    aEntry = __bswap_32(aEntry);
#endif

    iBatBuffer[aIndex] = aEntry;
    SetState(EDirty);

    return KErrNone;
}

//--------------------------------------------------------------------
/**
    Flushes the BAT cache.
    @return KErrNone on success, negative error code otherwise

    @post   changes cache state EDirty->EClean
*/
int CBat::Flush()
{
    DBG_LOG("CBat::Flush(), state:%d", State());

    if(State() != EDirty)
        return KErrNone;

    const int nRes = WriteBAT();
    ASSERT(State() == EClean);

    return nRes;
}


//--------------------------------------------------------------------
/** @return true if aIndex looks valid */
bool CBat::BatIndexValid(uint32_t aIndex) const
{
    ASSERT(iMaxEntries);
    return (aIndex < iMaxEntries);
}



//####################################################################
//#  CSectorMapper class implementation
//####################################################################


//--------------------------------------------------------------------
/**
    Constructor.
    @param  CVhdDynDiffBase reference to the class representing common functionality for dynamic & differencing VHDs
*/
CSectorMapper::CSectorMapper(CVhdDynDiffBase& aVhd)
              :iVhd(aVhd)
{
    DBG_LOG("CSectorMapper::CSectorMapper()");
    iState = EInvalid;
}

//--------------------------------------------------------------------
/**
    Destructor. This object can be deleted only in EInvalid state i.e before using it or after
    explicit Close() call
*/
CSectorMapper::~CSectorMapper()
{
    DBG_LOG("CSectorMapper::~CSectorMapper()");

    if(State() != EInvalid)
        Fault(EInvalidState);

    ASSERT(!iPages.size());
}

//--------------------------------------------------------------------
/**
    Close the object, destroy cache
    @param  aForceClose     if true, ignores dirty data. not for a normal use
    @pre the cache must not contain dirty pages. Flush() them before calling this method
*/
void CSectorMapper::Close(bool aForceClose /*=false*/)
{
    DBG_LOG("CSectorMapper::Close(%d)", aForceClose);

    InvalidateCache(aForceClose); //-- it will do all necessary checks
    SetState(EInvalid);

    //-- destroy cache page objects and the cache itself
    for(TPListConstItr itr = iPages.begin(); itr != iPages.end(); ++itr)
    {
        CSectorBmpPage* pPage = *itr;
        ASSERT((*itr)->State() == ESB_Invalid);

        pPage->Close(aForceClose);
        delete pPage;
    }
    iPages.clear();
}



//--------------------------------------------------------------------
/**
    Invalidates cache data. If the client will try to access data with cache invalid, it will result in
    re-reading data from the media to the cache.
    Does not deallocate cache RAM, just marks data as invalid.

    @param  aIgnoreDirty    if true, ignores dirty data. not for a normal use

    @pre the cache must not be dirty; @see Flush()
*/
void CSectorMapper::InvalidateCache(bool aIgnoreDirty /*=false*/)
{
    DBG_LOG("CSectorMapper::InvalidateCache(%d), state:%d", aIgnoreDirty, State());

    if(State() == EDirty && !aIgnoreDirty)
    {//-- an attempt to close / destroy cache with dirty data
        Fault(ESecMap_DestroyingDirty);
    }

    for(TPListConstItr itr = iPages.begin(); itr != iPages.end(); ++itr)
    {
        (*itr)->InvalidateCache(aIgnoreDirty);
    }
}

//--------------------------------------------------------------------
/**
    Flush dirty cache pages onto the media
    @post   changes the state: EDirty->EClean

    @return KErrNone on success, negative error code otherwise
*/
int CSectorMapper::Flush()
{
    if(State() != EDirty)
        return KErrNone;

    int nFlushRes = KErrNone;

    //-- make best effort to flush all pages
    for(TPListConstItr itr = iPages.begin(); itr != iPages.end(); ++itr)
    {
        const int nRes = DoFlushPage(*itr);
        if(nRes != KErrNone)
            nFlushRes = nRes;
    }

    if(nFlushRes == KErrNone)
        SetState(EClean);

    return nFlushRes;
}

//--------------------------------------------------------------------
/**
    Get a const reference to the populated object representing a block's sector allocation bitmap.
    The caller must ensure that aBlockSector is valid within this VHD.
    Not very (thread) safe method, because it returns a reference to internal cache structure.

    @param      aBlockSector starting sector of the block. Identifies the bitmap we need.
    @return     pointer to the CSectorBmpPage object in the valid state or NULL if something really bad happened
*/
const CSectorBmpPage* CSectorMapper::GetSectorAllocBitmap(TBatEntry aBlockSector)
{
    DBG_LOG("CSectorMapper::GetSectorAllocBitmap(aBlockSector:%d) ", aBlockSector);

    const CSectorBmpPage* pPage = DoGetPopulatedPage(aBlockSector);
    ASSERT(pPage);
    ASSERT(State() != EInvalid);

    return pPage;
}

//--------------------------------------------------------------------
/**
    Read a single bit of the block's sector allocation bitmap.

    @param  aBlockSector    aBlockSector starting sector of the block. Identifies the bitmap we need.
    @param  aSectorNumber   bit number (i.e. sector number in the block)
    @return bit value
*/
uint32_t CSectorMapper::GetSectorAllocBit(TBatEntry aBlockSector, uint32_t aSectorNumber)
{
    DBG_LOG("CSectorMapper::GetSectorAllocBit(aBlockSector:%d, aSectorNumber:%d) ", aBlockSector, aSectorNumber);
    ASSERT(aSectorNumber < BmpSizeInBits());

    CSectorBmpPage* pPage = DoGetPopulatedPage(aBlockSector);
    ASSERT(State() != EInvalid);

    return pPage->GetAllocBmpBit(aSectorNumber);
}

//--------------------------------------------------------------------
/**
    Set a range of sector's allocation bitmap bits to '1'
    This call may result in many things like hitting the media, allocating page cahes, flushing dirty pages to the media etc.

    @param  aBlockSector    sector of the block. Identifies the bitmap we need
    @param  aSectorNumber   starting bit number (i.e. sector number in the block)
    @param  aNumBits        number of bits to set
*/
TSectorBitmapState CSectorMapper::SetSectorAllocBits(TBatEntry aBlockSector, uint32_t aSectorNumber, uint32_t aNumBits)
{
    DBG_LOG("CSectorMapper::SetSectorAllocBits(aBlockSector:%d, aSectorNumber:%d, aNumBits:%d) ", aBlockSector, aSectorNumber, aNumBits);

    ASSERT(aNumBits && (aNumBits+aSectorNumber) <= BmpSizeInBits());

    CSectorBmpPage* pPage = DoGetPopulatedPage(aBlockSector);
    ASSERT(State() != EInvalid);

    const TSectorBitmapState bmpState = pPage->SetAllocBmpBits(aSectorNumber, aNumBits);

    ASSERT(bmpState != ESB_Invalid);

    if(bmpState == ESB_Dirty)
        SetState(EDirty); //-- mark whole cache as dirty

    return bmpState;
}

//--------------------------------------------------------------------
/**

    Reset a range of sector's allocation bitmap bits to '0'. Used mostly by "Discard sectors" API calls.
    This call may result in many things like hitting the media, allocating page cahes, flushing dirty pages to the media etc.

    @param  aBlockSector    sector of the block. Identifies the bitmap we need
    @param  aSectorNumber   starting bit number (i.e. sector number in the block)
    @param  aNumBits        number of bits to set
*/
TSectorBitmapState CSectorMapper::ResetSectorAllocBits(TBatEntry aBlockSector, uint32_t aSectorNumber, uint32_t aNumBits)
{
    DBG_LOG("CSectorMapper::ResetSectorAllocBits(aBlockSector:%d, aSectorNumber:%d, aNumBits:%d) ", aBlockSector, aSectorNumber, aNumBits);
    ASSERT(aNumBits && (aNumBits+aSectorNumber) <= BmpSizeInBits());
    ASSERT(iVhd.TrimEnabled()); //-- calling this method doesn't make any sense in normal operational mode, only when discarding sectors

    CSectorBmpPage* pPage = DoGetPopulatedPage(aBlockSector);
    ASSERT(State() != EInvalid);

    const TSectorBitmapState bmpState = pPage->ResetAllocBmpBits(aSectorNumber, aNumBits);

    ASSERT(bmpState != ESB_Invalid);

    if(bmpState == ESB_Dirty)
        SetState(EDirty); //-- mark whole cache as dirty

    return bmpState;
}


//--------------------------------------------------------------------
/**
    Search cache for a page with a given block number (key)
    Can make found page MRU if required.

    @param  aBlockSector    sector of the block this bitmap belongs to. The caller is responsible for ensuring it is correct.
    @param  aMakeMRU        if true and if the page is found, makes it MRU by placint to the top of the list
    @return pointer to it from the cache
*/
CSectorBmpPage* CSectorMapper::DoFindCachedPage(TBatEntry aBlockSector, bool aMakeMRU /*=false*/)
{
    ASSERT(iVhd.BatEntryValid(aBlockSector));

    for(TPListItr itr = iPages.begin(); itr != iPages.end(); ++itr)
    {
        CSectorBmpPage* pPage = (*itr);
        ASSERT(pPage);

        if(pPage->BlockSector() ==  aBlockSector)
        {
            if(aMakeMRU && itr != iPages.begin())
            {
                iPages.erase(itr);
                iPages.push_front(pPage);
            }

            return pPage;
        }
    }

    return NULL;
}

//--------------------------------------------------------------------
/**
    Brings the populated page object from the cache if it is cached.
    Otherwise it either creates a new page or evicts the LRU page from the cache. Then populates the page with data from the media.
    Also makes the page MRU by putting it to the top of the list.

    @param  aBlockSector    sector of the block this bitmap belongs to. The caller is responsible for ensuring it is correct.
    @return pointer to page object. Guaranteed to be valid
*/
CSectorBmpPage* CSectorMapper::DoGetPopulatedPage(TBatEntry aBlockSector)
{

    //-- search the cache first for the given block number (key)
    CSectorBmpPage* pPage = DoFindCachedPage(aBlockSector, true);
    if(pPage && pPage->State() != ESB_Invalid)
    {//-- found the page in the cache and made it MRU
        return pPage;
    }

    if(!pPage)
    {//-- need to allocate a new cache page or evict one from the cache for us
        if(iPages.size() < KMaxCached_SectorBitmaps)
        {//-- allocate a brand new cache page
            pPage = new CSectorBmpPage(*this, aBlockSector);
        }
        else
        {
            pPage = iPages.back();              //-- get last page from the list

            //-- flush it, may contain dirty data
            //-- if flushing page failed, it means that something VERY serious happened.
            //-- don't try being too smart now, @todo make better error handling
            if(DoFlushPage(pPage) != KErrNone)
                return NULL;

            iPages.pop_back();                  //-- evict last page from the list
            pPage->InvalidateCache();           //-- invalidate cached data
            pPage->SetBlockSector(aBlockSector);//-- assign a new block sector (key)
        }

        //-- add the page to the top of the list, making it MRU
        iPages.push_front(pPage);
    }

    ASSERT(pPage && pPage->State() == ESB_Invalid); //-- page cache state must be invalid
    ASSERT(pPage == iPages.front());                //-- the page must be made MRU already
    ASSERT(pPage->BlockSector() == aBlockSector);   //-- key must be set correctly

    {//-- read the page data from the media and place it to the corresponding object.
     //-- try not to leak memory if some error happens
        CAutoClosePtr<CSectorBmpPage> pTmp(pPage);
        pPage = NULL;

        CDynBuffer buf(BmpSizeInBytes());

        const int nRes = iVhd.DoRaw_ReadData(aBlockSector, BmpSizeInBytes(), buf.Ptr());
        if(nRes != (int)BmpSizeInBytes())
        {
            ASSERT(0);
            return NULL;
        }

        const TSectorBitmapState state = pTmp->ImportData(buf.Ptr());
        if(state == ESB_Invalid)
        {//-- something really bad happened
            ASSERT(0);
            return NULL;
        }

        ASSERT(state != ESB_Dirty);

        if(State() == EInvalid)
            SetState(EClean);

        pPage = pTmp.release();
    }

    return pPage;
}

//--------------------------------------------------------------------
/**
    Flushes page's dirty data  onto the media.

    @param  apPage pointer to the page object to flush.
    @return KErrNone on success, negative error code otherwise

    @post  changes page state ESB_Dirty->ESB_Clean, other states are not changed
*/
int CSectorMapper::DoFlushPage(CSectorBmpPage* apPage)
{
    DBG_LOG("CSectorMapper::DoFlushPage() pageBlkSector:%d, PState:%d", apPage->BlockSector(), apPage->State());

    if(apPage->State() != ESB_Dirty)
        return KErrNone;

    ASSERT(iVhd.BatEntryValid(apPage->BlockSector()));

    CDynBuffer buf(BmpSizeInBytes());

    //-- 1. get page data in correct endianness
    const TSectorBitmapState bufState = apPage->ExportData(buf.Ptr());


    //-- 2. write data to the media
    const int nRes = iVhd.DoRaw_WriteData(apPage->BlockSector(), BmpSizeInBytes(), buf.Ptr());
    if(nRes != (int)BmpSizeInBytes())
    {
        DBG_LOG("Flushing page error! code:%d", nRes);
        return nRes;
    }

    //-- 3. mark page as "clean", "fully mapped" or "fully unmapped"

    if(iVhd.TrimEnabled())
    {//-- TRIM or "discarding sectors" can reset bits in the alloc. bitmap
        ASSERT(bufState == ESB_Clean || bufState == ESB_FullyMapped || bufState == ESB_FullyUnmapped);
    }
    else
    {//-- Normally we can only set bits, other states are impossible
        ASSERT(bufState == ESB_Clean || bufState == ESB_FullyMapped);
    }

    apPage->SetState(bufState);

    return KErrNone;
}


//####################################################################
//#  CSectorBmpPage class implementation
//####################################################################


//--------------------------------------------------------------------
/**
    Constructor. Creates empty invalid page without bitmap allocated.

    @param  aParent         reference to the parent container
    @param  aBlockSector    sector of the block this bitmap belongs to
*/
CSectorBmpPage::CSectorBmpPage(CSectorMapper& aParent, TBatEntry aBlockSector/*=0*/)
               :iParent(aParent), iBlockSector(aBlockSector)
{
    DBG_LOG("CSectorBmpPage::CSectorBmpPage[0x%p] Sect:%d", this, aBlockSector);
    iState = ESB_Invalid;
}

//--------------------------------------------------------------------
/**
    Destructor. This object can be deleted only in EInvalid state i.e before using it or after
    explicit Close() call
*/
CSectorBmpPage::~CSectorBmpPage()
{
    DBG_LOG("CSectorBmpPage::~CSectorBmpPage[0x%p]", this );

    if(State() != ESB_Invalid)
        Fault(EInvalidState);

    ASSERT(!iAllocBitmap.Size());
}

//--------------------------------------------------------------------
/**
    Close the object, deallocate data
    @param  aForceClose     if true, ignores dirty data. not for a normal use
    @pre the page must not contain dirty datain normal circumstances
*/
void CSectorBmpPage::Close(bool aForceClose/*=false*/)
{
    DBG_LOG("CSectorBmpPage::Close(%d)", aForceClose);

    InvalidateCache(aForceClose);//-- will do some checks

    iAllocBitmap.Close();
    iBlockSector = KBatEntry_Invalid;
    SetState(ESB_Invalid);
}

//--------------------------------------------------------------------
/**
    Deliberately marks page data as invalid this may cause this page to be re-read from the media on the next access.
    iBlockSector value is not changed.

    @param  aIgnoreDirty    if true, ignores dirty data. not for a normal use
    @pre the page must not contain dirty data in normal circumstances
*/
void CSectorBmpPage::InvalidateCache(bool aIgnoreDirty/*=false*/)
{
    DBG_LOG("CSectorBmpPage::InvalidateCache[0x%p](%d) state:%d, iBlockSector=0x%x", this, aIgnoreDirty, State(), iBlockSector);
    if(State() == ESB_Dirty && !aIgnoreDirty )
    {
        Fault(ESecPage_DestroyingDirty);
    }

    SetState(ESB_Invalid);
}

//--------------------------------------------------------------------
/**
    Sets a page's new block sector.

    @pre The page must be in ESB_Invalid state to avoid possible confusion
    @param aNewBlockSector new value
*/
void CSectorBmpPage::SetBlockSector(TBatEntry aNewBlockSector)
{
    ASSERT(State() == ESB_Invalid);
    iBlockSector = aNewBlockSector;
}


//--------------------------------------------------------------------
/**
    Read a single bit of the block's allocation bitmap.

    @param  aBitNumber bit number to read
    @return bit status
*/
uint32_t CSectorBmpPage::GetAllocBmpBit(uint32_t aBitNumber) const
{
    switch(State())
    {
    case ESB_Clean:
    case ESB_Dirty:
    return iAllocBitmap[aBitNumber];

    case ESB_FullyMapped:
    return 1; //-- Bitmap contains all '1's, contents isn't cached

    case ESB_FullyUnmapped:
    return 0; //-- Bitmap contains all '0's, contents isn't cached

    default:
    ASSERT(0);
    break;
    };

    Fault(EMustNotBeCalled);
}


//--------------------------------------------------------------------
/**
    Get block's sector allocation bitmap copy
    @param  out: aSrcBitmap bit vector representing the block's sector allocation bitmap
    @return standard error code
*/
int CSectorBmpPage::GetAllocBitmap(CBitVector& aBitmap) const
{
    switch(State())
    {
    case ESB_FullyMapped:   //-- block bitmap contains all '1'
        aBitmap.Fill(1);
    return KErrNone;

    case ESB_FullyUnmapped: //-- block bitmap contains all '0'
        aBitmap.Fill(0);
    return KErrNone;

    case ESB_Clean: //-- mixture of '1' and '0'
        aBitmap = iAllocBitmap;
    return KErrNone;

    default:
        ASSERT(0);
    return KErrCorrupt;
    };

    Fault(EMustNotBeCalled);
}



//--------------------------------------------------------------------
/**
    Sets a group of bits in the allocation bitmap. Should be faster than multiple calls to ::SetBit().
    Depending on bitmap state it can work differently, e.g. if bitmap has already all bits set (ESB_FullyMapped), then do nothing
    If the bitmap has all bits reset initially (ESB_FullyUnmapped), it might require allocating a bitmap object.

    @param  aBitNumber  starting bit number
    @param  aNumBits    number of bits to set.
    @return new bitmap cache page status
*/
TSectorBitmapState CSectorBmpPage::SetAllocBmpBits(uint32_t aBitNumber, uint32_t aNumBits)
{
    ASSERT(aNumBits >0);

    switch(State())
    {
    case ESB_Invalid: //-- Invalid state, bitmap isn't initialised
        ASSERT(0);
    break;

    case ESB_Clean:
        if(iAllocBitmap.IsFilledWith(aBitNumber, aBitNumber+aNumBits-1, 1))
            break; //-- nothing to do

        iAllocBitmap.Fill(aBitNumber, aBitNumber+aNumBits-1, 1);
        SetState(ESB_Dirty);
    break;

    case ESB_Dirty:
        iAllocBitmap.Fill(aBitNumber, aBitNumber+aNumBits-1, 1);
    break;

    case ESB_FullyMapped:
        //-- Bitmap contains all '1's, contents isn't cached, nothing to do
    break;

    case ESB_FullyUnmapped:
    {   //-- Bitmap contains all '0's, contents isn't cached. need to create the cache
        //-- and set the corresponding bits
        if(DoCreateAllocBitmap() != KErrNone)
        {
            ASSERT(0);
            return ESB_Invalid;
        }

        iAllocBitmap.Fill(aBitNumber, aBitNumber+aNumBits-1, 1);
        SetState(ESB_Dirty);
    }
    break;


    default:
        ASSERT(0);
    break;

    };

    return State();
}


//--------------------------------------------------------------------
/**
    Reset a group of bits in the allocation bitmap. This is for support TRIM operations mostly
    Depending on bitmap state it can work differently, e.g. if bitmap has already all bits reset (ESB_FullyUnmapped), then do nothing
    If the bitmap has all bits set initially (ESB_FullyMapped), it might require allocating a bitmap object.

    @param  aBitNumber  starting bit number
    @param  aNumBits    number of bits to reset.
    @return new bitmap cache page status
*/
TSectorBitmapState CSectorBmpPage::ResetAllocBmpBits(uint32_t aBitNumber, uint32_t aNumBits)
{
    ASSERT(aNumBits >0);

    switch(State())
    {
    case ESB_Invalid: //-- Invalid state, bitmap isn't initialised
        ASSERT(0);
    break;

    case ESB_Clean:
        if(iAllocBitmap.IsFilledWith(aBitNumber, aBitNumber+aNumBits-1, 0))
            break; //-- nothing to do

        iAllocBitmap.Fill(aBitNumber, aBitNumber+aNumBits-1, 0);
        SetState(ESB_Dirty);
    break;

    case ESB_Dirty:
        iAllocBitmap.Fill(aBitNumber, aBitNumber+aNumBits-1, 0);
    break;

    case ESB_FullyMapped:
        {
        //-- Bitmap contains all '1's, contents isn't cached. need to create the cache
        //-- and reset the corresponding bits
        if(DoCreateAllocBitmap() != KErrNone)
        {
            ASSERT(0);
            return ESB_Invalid;
        }

        iAllocBitmap.Fill(aBitNumber, aBitNumber+aNumBits-1, 0);
        SetState(ESB_Dirty);
        }
    break;

    case ESB_FullyUnmapped:
        //-- Bitmap contains all '0's, contents isn't cached. Nothing to do
    break;

    default:
        ASSERT(0);
    break;

    };

    return State();
}



//--------------------------------------------------------------------
/**
    Imports data from the external buffer into the internal bitmap representation.
    The bits of bitmap bytes from the disk may require swapping, because the data are big-endian.

    @pre    Page data mustn't be dirty

    @post   Changes the state of the page to one of these:
            ESB_FullyUnmapped   the bitmap is filled with '0's, no buffer allocated
            ESB_FullyMapped     the bitmap is filled with '1's, no buffer allocated
            ESB_Clean           mixture of 1's and 0's, buffer is allocated


    @param  apData      pointer to data buffer. !! It is non-const pointer, this function may modify the data in the buffer to mae them correct endianness !!
    @return new bitmap cache page status
*/
TSectorBitmapState CSectorBmpPage::ImportData(void* apData)
{
    ASSERT(State() != ESB_Dirty);

    //-- analyse data and fix endainness if required
    const TSectorBitmapState bufState = DoProcessDataBuffer(apData, iParent.BmpSizeInBits());

    switch(bufState)
    {
        case ESB_FullyUnmapped: //-- the bitmap is filled with '0's
        case ESB_FullyMapped:   //-- the bitmap is filled with '1's
            iAllocBitmap.Close();    //-- don't need to keep the data buffer
        break;

        case ESB_Clean:
            //-- mixture of 1's and 0's, need to put data into the cache
            if(DoCreateAllocBitmap() != KErrNone)
            {
                ASSERT(0);
                return ESB_Invalid;
            }

            ASSERT(iAllocBitmap.Size()  == iParent.BmpSizeInBits());
            //-- import converted data
            iAllocBitmap.ImportData(0, iAllocBitmap.Size(), apData);
        break;

        default:
            ASSERT(0);
        break;
    };

    SetState(bufState);
    return State();
}

//--------------------------------------------------------------------
/**
    Exports data from the internal bitmap representation to the external buffer.

    @param  apBuf       pointer the buffer where data are exported. Must be large enough for the bitmap data.
    @return state of the bitmap data, @see DoProcessDataBuffer() result.
*/
TSectorBitmapState CSectorBmpPage::ExportData(void* apBuf)
{
    ASSERT(State() == ESB_Clean || ESB_Dirty);
    ASSERT(iAllocBitmap.Size() && iAllocBitmap.Size() == iParent.BmpSizeInBits());

    //-- 1. export data from the bitmap to buffer
    iAllocBitmap.ExportData(0, iAllocBitmap.Size(), apBuf);

    //-- 2. analyse data and fix endainness if required
    const TSectorBitmapState bufState = DoProcessDataBuffer(apBuf, iAllocBitmap.Size());

    return bufState; //-- not the page state!
}

//--------------------------------------------------------------------
/**
    Swap bits in every 4 bytes in a 32-bit word. Doesn't swap bytes themselves.
    I.e. the bit numbering will become: [24]..[31][16]..[23][8]..[15][0]..[7]
    @param  aVal    input 32-bit value
    @return every byte in the word with its bits swapped
*/
static inline uint32_t DoSwapBitsInBytes(uint32_t aVal)
{
    aVal = ((aVal & 0xAAAAAAAA) >> 1) | ((aVal & 0x55555555) << 1); //-- swap every second bit
    aVal = ((aVal & 0xCCCCCCCC) >> 2) | ((aVal & 0x33333333) << 2); //-- swap 2 bits pairs
    aVal = ((aVal & 0xF0F0F0F0) >> 4) | ((aVal & 0x0F0F0F0F) << 4); //-- swap every second nibble

    return aVal;
}

//--------------------------------------------------------------------
/**
    Processes a given data buffer to make it usable by bitmap cache.
    If this system is little endian, swaps bits in every byte of the buffer (data on the disk is BE)

    @param  apData      pointer to the buffer with data, data can be modified there
    @param  aNumBits    number of bits; must be a multiple of 32

    @return ESB_FullyUnmapped   the bitmap is filled with '0's, data in buffer are not modified
            ESB_FullyMapped     the bitmap is filled with '1's, data in buffer are not modified
            ESB_Clean           the bitmap contains 1s and 0s, data in buffer are likely to be modified (converted to LE)
*/
TSectorBitmapState CSectorBmpPage::DoProcessDataBuffer(void* apData, uint32_t aNumBits) const
{
    ASSERT(!(aNumBits & 0x1f));  //-- need to have whole words.

    const uint32_t  numWords =  aNumBits >> 5;
    uint32_t*       pwData = (uint32_t*)apData;

    uint32_t cnt00 = 0;
    uint32_t cntFF = 0;

    for(uint32_t i=0; i<numWords; ++i)
    {
        switch(pwData[i])
        {
        case 0x00:
            ++cnt00;
        break;

        case 0xFFFFFFFF:
            ++cntFF;
        break;

        default: //-- convert bytes in the word to Big-endian bit order
#if __BYTE_ORDER == __LITTLE_ENDIAN
            pwData[i] = DoSwapBitsInBytes(pwData[i]);
#endif
        break;

        };
    }//for(...)

    if(cnt00 == numWords)
        return ESB_FullyUnmapped;//-- the bitmap is filled with '0's

    if(cntFF == numWords)
        return ESB_FullyMapped; //-- the bitmap is filled with '1's

    return ESB_Clean; //-- just to indicate mixture of 1s and 0s
}


//--------------------------------------------------------------------
/**
    Create and initialise iAllocBitmap object
    @return Standard Error code, KErrNone on success
*/
int CSectorBmpPage::DoCreateAllocBitmap()
{
    if(iAllocBitmap.Size())
    {//-- already allcoated
        ASSERT(iAllocBitmap.Size()  == iParent.BmpSizeInBits());
        return KErrNone;
    }

    const int nRes = iAllocBitmap.Create(iParent.BmpSizeInBits());
    return nRes;
}








