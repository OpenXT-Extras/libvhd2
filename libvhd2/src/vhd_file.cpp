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

    @file imlementation of the VHD file handlers
*/

#include <fcntl.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>

#include "vhd.h"
#include "block_mng.h"

ASSERT_COMPILE(!(KDefScratchBufSize& (KDefSecSize-1))); //-- max buffer size must be a multiple of sectors
ASSERT_COMPILE(sizeof(T_CHS) == sizeof (uint32_t));

//--------------------------------------------------------------------
/**
    Read VHD footer from the file and populate corresponding TVhdFooter object with the data
    @param  aFd         opened file descriptor
    @param  aFilePos    abs. position in the file to read footer from
    @param  aFooter     out: an object to fill with the data from the file

    @return KErrNone on success, error code otherwise
 */
static int DoReadVhdFooter(int aFd, off64_t aFilePos, TVhdFooter& aFooter)
{
    ASSERT(aFd > 0);
    uint8_t buf[TVhdFooter::KSize];

    ssize_t bytesRead = pread64(aFd, buf, TVhdFooter::KSize, aFilePos);
    if(bytesRead != TVhdFooter::KSize)
    {
        const int aErrCode = -errno;
        DBG_LOG("Error reading VHD file footer! pos:%lld, code:%d", aFilePos, aErrCode);
        return aErrCode;
    }

    aFooter.Internalise(buf);

    if(!aFooter.IsValid())
    {
        DBG_LOG("VHD file footer read from pos:%lld is invalid! Dump:", aFilePos);
        aFooter.Dump();
    }

    return KErrNone;
}

//--------------------------------------------------------------------
/**
    Read VHD Header from the file and populate corresponding TVhdHeader object with the data
    @param  aFd         opened file descriptor
    @param  aFilePos    abs. position in the file to read footer from
    @param  aHeader     out: an object to fill with the data from the file

    @return KErrNone on success, error code otherwise
 */
static int DoReadVhdHeader(int aFd, off64_t aFilePos, TVhdHeader& aHeader)
{
    ASSERT(aFd > 0);

    uint8_t buf[TVhdHeader::KSize];

    ssize_t bytesRead = pread64(aFd, buf, TVhdHeader::KSize, aFilePos);
    if(bytesRead != TVhdHeader::KSize)
    {
        const int aErrCode = -errno;
        DBG_LOG("Error reading VHD file header! pos:%lld, code:%d", aFilePos, aErrCode);
        return aErrCode;
    }

    aHeader.Internalise(buf);

    if(!aHeader.IsValid())
    {
        DBG_LOG("VHD file header read from pos:%lld is invalid! Dump:", aFilePos);
        aHeader.Dump();
    }

    return KErrNone;
}



//--------------------------------------------------------------------
/**
    Factory function. Creates an object of CVhdFileBase class hierarchy to handle specific VHD file type.

    @param  aFileDesc   file descriptor
    @param  aErrCode    out: error code

    @return pointer to the object on success, NULL and error code in aErrCode otherwise
*/
static CVhdFileBase* DoCreateFromFile(int aFileDesc, int& aErrCode)
{
    //-- 1. check VHD file size; it can't be less than 1 sector
    off64_t fileOffset = lseek64(aFileDesc, 0, SEEK_END);
    if (fileOffset == (off64_t)-1 || fileOffset < KDefSecSize)
    {
        aErrCode = -errno;
        DBG_LOG("Error getting file size or it is less than 512 bytes!, code:%d, fsize:%lld", aErrCode, fileOffset);
        return NULL;
    }

    //-- 2.1 read VHD footer from the last sector of the file
    TVhdFooter vhdFooter;

    aErrCode = DoReadVhdFooter(aFileDesc, (fileOffset-TVhdFooter::KSize), vhdFooter);
    if(aErrCode != KErrNone)
        return NULL;

    if(vhdFooter.IsValid() && vhdFooter.DiskType() == EVhd_Fixed)
    { //-- we are opening a Fixed VHD file
      return new CVhdFileFixed(&vhdFooter);
    }

    TVhdFooter vhdFooterCopy;   //-- footer copy, at the beginning of the file for dynamic and differencing disks
    TVhdHeader vhdHeader;       //-- vhd header, for dynamic and differencing disks

    //-- 2.2 read VHD footer copy from the 1st sector of the file (it will be invalid for fixed disks)
    aErrCode = DoReadVhdFooter(aFileDesc, 0, vhdFooterCopy);
    if(aErrCode != KErrNone)
        return NULL;

    TVhdFooter* pValidFooter = NULL; //-- pointer to a valid footer

    //-- 2.3 try reading VHD header
    {
        off64_t posHeader = KDefSecSize; //-- for the case when both footers are invalid, try to read header from its default location in the file (2nd sector)

        if(vhdFooter.IsValid())
        {//-- normal footer is valid
            pValidFooter = &vhdFooter;
            posHeader = vhdFooter.DataOffset();
        }
        else if(vhdFooterCopy.IsValid())
        {//-- normal footer is valid, but its copy looks valid
            pValidFooter = &vhdFooterCopy;
            posHeader = vhdFooterCopy.DataOffset();
        }

        aErrCode = DoReadVhdHeader(aFileDesc, posHeader, vhdHeader);
        if(aErrCode != KErrNone)
            return NULL;
    }

    //-- 3. analyse header & footer and instantiate appropriate object to deal with VHD

    if(!vhdHeader.IsValid())
    {
        DBG_LOG("VHD header is invalid!");
        aErrCode = KErrCorrupt;
        return NULL;
    }

    if(!pValidFooter)
    {
        //-- funny situation: both footers are invalid, but the header at its default location is valid.
        //-- in theory it is possible to deduct VHD type and work with it normally.
        DBG_LOG("all footers are corrupt, but the header is valid");
        Fault(ENotImplemented);
        return NULL;
    }


    switch(pValidFooter->DiskType())
    {
        case EVhd_Dynamic://-- we are opening a Dynamic VHD file
        return new CVhdFileDynamic(pValidFooter, &vhdHeader);

        case EVhd_Diff: //-- we are opening a differencing VHD file
        return new CVhdFileDiff(pValidFooter, &vhdHeader);

        default:
        DBG_LOG("Invalid VHD type:%d", pValidFooter->DiskType());
        return NULL;

    };//switch(pValidFooter->DiskType())

    Fault(EMustNotBeCalled);
    return NULL;
}

//####################################################################
//#  CVhdFileBase  class implementation
//####################################################################

//--------------------------------------------------------------------

CVhdFileBase::CVhdFileBase(const TVhdFooter* apFooter)
{
    iFileDesc  = -1;
    iModeFlags = 0;
    iState = EInvalid;

    ASSERT(apFooter && apFooter->IsValid());
    iFooter = *apFooter;

    iVhdSizeSec = 0;
}

CVhdFileBase::~CVhdFileBase()
{
    DBG_LOG("CVhdFileBase::~CVhdFileBase[0x%p], state:%d", this, State());

    if(State() == EOpened)
    {//-- the Close() method should be called _before_ attempt to delete this object.
     //-- this is because Close() may try flushing data onto media, etc., which may fail, can't afford this in destructor
        Fault(EInvalidState);
    }
}


//--------------------------------------------------------------------
/**
    Invalidates cache data. If the client will try to access data with cache invalid, it will result in
    re-reading data from the media to the cache.
    Does not deallocate cache RAM, just marks data as invalid.

    @param  aIgnoreDirty    if true, ignores dirty data. not for a normal use

    @pre the cache must not be dirty; @see Flush()
*/
void CVhdFileBase::InvalidateCache(bool aIgnoreDirty)
{
    DBG_LOG("CVhdFileBase::DoInvalidateCache[0x%p](%d)", this, aIgnoreDirty);
}


//--------------------------------------------------------------------
/**
    Deallocate / release resources, close file handle.
    The caller must be sure that all dirty data are flushed. See Flush().

    @param  aForceClose     if true, ignores dirty data. not for a normal use
*/
void CVhdFileBase::Close(bool aForceClose)
{
    DBG_LOG("CVhdFileBase::Close(%d)[0x%p] State:%d", aForceClose, this, State());

    //-- make best effort to flush data/metadata
    DoFlush();
    (void)aForceClose;

    close(iFileDesc); //-- close file descriptor
    iFileDesc = -1;

    SetState(EInitialised);
}

//--------------------------------------------------------------------
/**
    Flushes VHD file data and metadata to the media.
    @return standard error code, 0 on success.
*/
int CVhdFileBase::Flush()
{
    return DoFlush();
}

int CVhdFileBase::DoFlush()
{
    if(State() != EOpened)
        return KErrGeneral;

    ASSERT(iFileDesc > 0);

    if(fsync(iFileDesc) == 0)
        return KErrNone;

    const int nRes = -errno;
    DBG_LOG("CVhdFileBase::Flush() error! code:%d  ", nRes);
    return nRes;
}

//--------------------------------------------------------------------
/**
    Implements "Open" semantics for the abstract generic VHD file.
    Doesn't change this object state, only leaf classes can do this.

    @return standard error code, KErrNone on success.
*/
int  CVhdFileBase::Open()
{
    DBG_LOG("CVhdFileBase::Open()[0x%p]", this);

    ASSERT(State() == EInitialised);

    //-- Check and store disk size in sectors. It comes from Footer CHS value (DiskGeometry field)
    iVhdSizeSec = iFooter.CHS_DiskSzInSectors();
    if(iVhdSizeSec < 2 || (((uint64_t)iVhdSizeSec) << SectorSzLog2()) >  iFooter.CurrDiskSizeInBytes())
    {
        ASSERT(0);
        return KErrCorrupt;
    }

    //-- check the file descriptor. If it is invalid, the object might have been closed before
    //-- and being reopened now. @todo implement checking that the VHD parameters are the same ?
    if(iFileDesc <= 0)
    {//-- try to re-open the file
        DBG_LOG("file descriptor is invalid, trying to re-open file...");
        int nRes = DoOpenFile(FilePath(), ModeFlags(), iFileDesc);
        if(nRes != KErrNone)
        {
            ASSERT(iFileDesc<=0);
            return nRes;
        }
    }

    return KErrNone;
}

//--------------------------------------------------------------------
/**
    Prints out some information about VHD in a human-readable form.
    @param  aStr string where the information goes.
*/
void CVhdFileBase::PrintInfo(std::string& aStr) const
{
    StrLog(&aStr, "========== VHD file info ==========");

    StrLog(&aStr, "File:'%s'", FilePath());
    StrLog(&aStr, "VHD Mode Flags: 0x%08x", ModeFlags());

    //-- dump footer
    iFooter.Dump(&aStr);

    //-- check footer and print some info if it is not valid
    if(iFooter.IsValid(&aStr))
    {
        StrLog(&aStr, "Footer data valid");
        StrLog(&aStr, "VHD size in sectors: %d", VhdSizeInSectors());
    }
}


//--------------------------------------------------------------------
/**
    Get some VHD parameters
    @param  aVhdParams out: filled in parameters structure
    @param  aParentNo       number of the parent VHD file. 0 refers to _this_ file
*/
int CVhdFileBase::GetInfo(TVHD_Params& aVhdInfo, uint32_t aParentNo) const
{
    ASSERT(aParentNo == 0);
    (void)aParentNo;

    aVhdInfo.Init();

    iFooter.GetInfo(aVhdInfo);

    aVhdInfo.vhdModeFlags = ModeFlags();
	aVhdInfo.secSizeLog2  = SectorSzLog2();
	aVhdInfo.vhdFileName  = FilePath();

    return KErrNone;
}



//--------------------------------------------------------------------
/** @return VHD file type. @see TVhdType */
TVhdType CVhdFileBase::VhdType() const
{
    return EVhd_None;
}



//--------------------------------------------------------------------
/**
    Open a physial file, apply necessary locks and retur the file descriptor on success

    @param  aFileName   name of the file to open
    @param  aModeFlags  set of flags affecting opening the file. @see VHDF_OPEN_*
    @param  aFd         out: file descriptor

    @return standard error code, KErrNone on success.
*/
int CVhdFileBase::DoOpenFile(const char *aFileName, uint32_t aModeFlags, int& aFd)
{
    DBG_LOG("aFileName:%s, aModeFlags:0x%x", aFileName, aModeFlags);

    int nRes;

    //-- 1. try opening the specified VHD file.
    int openFlags = O_LARGEFILE | O_RDONLY;
    short int lockType;  //-- lock type for the VHD file we are trying to open

    if(aModeFlags & VHDF_OPEN_RDWR)
    {//-- open for read and write
        openFlags |= O_RDWR;
        lockType = F_WRLCK; //-- write lock
    }
    else
    {//-- open RO
        aModeFlags &= ~VHDF_OPMODE_PURE_BLOCKS; //-- this flag doesn't make any sense in RO mode
        lockType = F_RDLCK; //-- read lock
    }

    if(aModeFlags & VHDF_OPEN_DIRECTIO)
            openFlags |= O_DIRECT;  //-- don't use FS caching

    if(aModeFlags & VHDF_OPEN_EXCLUSIVE_LOCK)
            lockType = F_WRLCK; //-- explicit request to apply write or "exclusive" lock

    const mode_t openMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

    aFd = open(aFileName, openFlags, openMode);

    if(aFd < 0)
    {
        nRes = -errno;
        DBG_LOG("Error opening the file! code:%d", nRes);
        return nRes;
    }

    //-- 2. try to acquire file lock, Read lock if open file in RO mode and Write lock for O_RDWR
    flock64 fl;

    fl.l_type   = lockType;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0; //-- lock whole file from start to the end
    fl.l_pid    = getpid();

    if(fcntl(aFd, F_SETLK, &fl) == -1)
    {
        nRes = -errno;
        DBG_LOG("Error locking the the file! lock:%d, code:%d", lockType, nRes);
        close(aFd);
        return nRes;
    }

    return KErrNone;
}

//--------------------------------------------------------------------
/**
    Factory function.
    Tries to opend a given file, checks pemissions, locks a file. Then creates an object of CVhdFileBase class hierarchy
    to handle specific VHD file type.

    @param  aFileName   name of the file to open
    @param  aModeFlags  set of flags affecting opening the file. @see VHDF_OPEN_*
    @param  aErrCode    out: error code

    @return pointer to the object on success, NULL and error code in aErrCode otherwise
*/
CVhdFileBase* CVhdFileBase::CreateFromFile(const char *aFileName, uint32_t aModeFlags, int& aErrCode)
{

    DBG_LOG("aFileName:%s, aModeFlags:0x%x", aFileName, aModeFlags);

    int fd;

    aErrCode = DoOpenFile(aFileName, aModeFlags, fd);
    if(aErrCode != KErrNone)
        return NULL;

    //-- try to create an object of type that corresponds to the type of VHD file
    CVhdFileBase* pSelf = DoCreateFromFile(fd, aErrCode);

    if(pSelf)
    {//-- the object created OK
        pSelf->iFileDesc  = fd;
        pSelf->iModeFlags = aModeFlags;
        pSelf->SetState(EInitialised);


        char realPath[PATH_MAX];
        const char* p = realpath(aFileName, realPath);
        ASSERT(p)
        pSelf->iFilePath.assign(p);

        aErrCode = KErrNone;
        return pSelf;
    }
    else
    {//-- failed for some reason
        close(fd);
        return NULL;
    }

}

//--------------------------------------------------------------------
/**
    A helper method. Checks ReadSectors() / WriteSectors() parameters and adjsuts number of sectors to be accessed

    @param  aStartSector    start sector in VHD file
    @param  aSectors        number of sectors to read/write; must be >0
    @param  aBufSize        size of the in/out buffer in bytes

    @return on success: number of sectors that can be read/written
            on error:   negative error code
*/
int CVhdFileBase::DoCheckRW_Args(uint32_t aStartSector, int aSectors, uint32_t aBufSize) const
{
    if(State() != EOpened)
    {
        ASSERT(0);
        return KErrBadHandle;
    }

    if(aSectors <= 0 || aBufSize < SectorSize())
    {
        ASSERT(0);
        return KErrArgument;
    }

    //-- check if we are trying to read/write outside the disk
    if(aStartSector >= VhdSizeInSectors())
        return KErrTooBig;

    uint32_t sectors = aSectors;

    ASSERT(aSectors > 0);
    const uint64_t lastSector = (uint64_t)aStartSector + sectors;

    if(lastSector > VhdSizeInSectors())
        sectors -= (uint32_t)(lastSector - VhdSizeInSectors());

    ASSERT(sectors <= (uint32_t)aSectors);

    //-- check if we have buffer large enough
    const uint32_t bufSectors = aBufSize >> SectorSzLog2();
    sectors = Min(sectors, bufSectors);

    ASSERT(sectors && !U32High(sectors));

    return (int)sectors;
}


//--------------------------------------------------------------------
/**
    Raw read a number of bytes from the VHD file.

	@param	aStartSector	starting sector.
	@param	aBytes		    number of bytes to read 0..LONG_MAX
	@param	apBuffer		out: read data

    @return	positive number of read bytes on success, negative value corresponding system error code otherwise.
*/
int CVhdFileBase::DoRaw_ReadData(uint32_t aStartSector, int aBytes, void* apBuffer) const
{

    DBG_LOG("CVhdFileBase::DoRaw_ReadData[0x%p](FileSector:%d, aBytes:%d) ",this, aStartSector, aBytes);

    ASSERT(State() == EOpened);
    ASSERT(iFileDesc > 0);
    ASSERT(aBytes > 0);

    const __off64_t filePos = ((uint64_t)aStartSector) << SectorSzLog2();

    const ssize_t bytesRead = pread64(iFileDesc, apBuffer, aBytes, filePos);

    if(bytesRead != aBytes)
    {
        const int nRes = -errno;
        DBG_LOG("CVhdFileBase::DoRaw_ReadData() error! val:%d, code:%d  ", bytesRead, nRes);
        return nRes;
    }

    return bytesRead;
}

//--------------------------------------------------------------------
/**
	Write a number of bytes to the VHD file.
	@param	aStartSector	starting sector.
	@param	aBytes		    number of bytes to write 0..LONG_MAX
	@param	apBuffer		in: data to write

	@return	positive number of written bytes on success, negative value corresponding system error code otherwise.
*/
int CVhdFileBase::DoRaw_WriteData(uint32_t aStartSector, int aBytes, const void* apBuffer) const
{
    DBG_LOG("CVhdFileBase::DoRaw_WriteData[0x%p](FileSector:%d, aBytes:%d) ", this, aStartSector, aBytes);
    ASSERT(State() == EOpened);
    ASSERT(iFileDesc > 0);
    ASSERT(aBytes > 0);

    const __off64_t filePos     = ((uint64_t)aStartSector) << SectorSzLog2();
    const ssize_t bytesWritten = pwrite64(iFileDesc, apBuffer, aBytes, filePos);

    if(bytesWritten != aBytes)
    {
        const int nRes = -errno;
        DBG_LOG("CVhdFileBase::DoRaw_WriteData() error! val:%d, code:%d  ", bytesWritten, nRes);
        return nRes;
    }

    return bytesWritten;
}


//--------------------------------------------------------------------
/**
    Check if some region of the file (multiple of sector size) is filled with the given byte

	@param	aStartSector	starting sector.
	@param	aSectors        number of sector to write
	@param	aFill   		filling byte

	@return	KErrNone        if the given region is filled with the given byte pattern
            KErrNotFound    if the given region is not filled with the given byte pattern
            negative error code on some other error
*/
int CVhdFileBase::DoRaw_CheckMediaFill(uint32_t aStartSector, uint32_t aSectors, uint8_t aFill) const
{
    DBG_LOG("CVhdFileBase::DoRaw_CheckMediaFill[0x%p](aStartSector:%d, aSectors:%d, aFill:0x%x)", this, aStartSector, aSectors, aFill);

    if(!aSectors)
    {
        ASSERT(0);
        return KErrNone;
    }

    const uint32_t KMaxBufSize = KDefScratchBufSize;    //-- max buffer size for media fill checking
    ASSERT_COMPILE(!(KMaxBufSize& (KDefSecSize-1)));    //-- max buffer size must be a multiple of sectors

    uint32_t remBytes =  aSectors << SectorSzLog2();
    const uint32_t KBufSize = Min(remBytes, KMaxBufSize);

    CDynBuffer buf(KBufSize);

    while(remBytes)
    {
        const uint32_t bytesToRead = Min(KBufSize, remBytes);

        int nRes = DoRaw_ReadData(aStartSector, bytesToRead, buf.Ptr());
        if(nRes <0)
            return nRes;//-- this is the error code

        ASSERT(nRes == (int)bytesToRead);

        if(!CheckFill(buf.Ptr(), bytesToRead, aFill))
            return KErrNotFound; //-- other data that required

        aStartSector += (bytesToRead >> SectorSzLog2());
        remBytes -= bytesToRead;

    }

    return KErrNone;
}

//--------------------------------------------------------------------
/**
    Fill media with a given data. Does not perform any geometry checks.

	@param	aStartSector	starting sector.
	@param	aSectors        number of sector to write
	@param	aFill   		filling byte

	@return	KErrNone on success, negative error code otherwise.
*/
int CVhdFileBase::DoRaw_FillMedia(uint32_t aStartSector, uint32_t aSectors, uint8_t aFill) const
{
    DBG_LOG("CVhdFileBase::DoRaw_FillMedia[0x%p](aStartSector:%d, aSectors:%d, aFill:0x%x)", this, aStartSector, aSectors, aFill);

    if(!aSectors)
        return KErrNone;

    const uint32_t KMaxBufSize = KDefScratchBufSize;    //-- max buffer size for media filling
    ASSERT_COMPILE(!(KMaxBufSize& (KDefSecSize-1)));    //-- max buffer size must be a multiple of sectors

    uint32_t remBytes =  aSectors << SectorSzLog2();
    const uint32_t KBufSize = Min(remBytes, KMaxBufSize);

    CDynBuffer buf(KBufSize);
    buf.Fill(aFill);

    while(remBytes)
    {
        const uint32_t bytesToWrite = Min(KBufSize, remBytes);

        int nRes = DoRaw_WriteData(aStartSector, bytesToWrite, buf.Ptr());

        if(nRes <0)
            return nRes;//-- this is the error code

        ASSERT(nRes == (int)bytesToWrite);

        aStartSector += (bytesToWrite >> SectorSzLog2());
        remBytes -= bytesToWrite;

    }

    return KErrNone;
}


//--------------------------------------------------------------------
/**
    Get file size.
    @param  aFileSize out: file size in bytes
    @return standard error code, KErrNone on success
*/
int CVhdFileBase::GetFileSize(uint64_t& aFileSize) const
{
    ASSERT(State() == EOpened);
    ASSERT(iFileDesc > 0);

    int nRes = KErrNone;

    aFileSize = lseek64(iFileDesc, 0, SEEK_END);
    if ((off64_t)aFileSize == (off64_t)-1)
    {
        nRes = -errno;
        DBG_LOG("Error getting file size! code:%d, fsize:%lld", nRes, aFileSize);
    }

    return nRes;
}

//--------------------------------------------------------------------
/**
    @return File name only
*/
const char* CVhdFileBase::FileName() const
{
    ASSERT(!iFilePath.empty());

    size_t pos = iFilePath.rfind(KPathDelim);
    ASSERT(pos != std::string::npos);

    return iFilePath.c_str() + (pos+1);
}

//--------------------------------------------------------------------
/**
    Get a pointer to the object representing a parent VHD file.
    If the parent VHD isn't opened, makes the best effort to open it.

    @param  aParentNo parent VHD number counted from the "Tail" towards "Head"
            0 means "this" VHD, 1 - first parent, 2 parent of the 1st parent etc.

    @return pointer to the parent VHD, NULL on error
*/
const CVhdFileBase* CVhdFileBase::GetParentOpened(uint32_t aParentNo)
{
    if(aParentNo == 0)
        return this;

    //-- this VHD type can't have parents
    Fault(EMustNotBeCalled);
    return NULL;
}



//####################################################################
//#  CVhdDynDiffBase class implementation
//####################################################################

//--------------------------------------------------------------------
/**
    constructor.
    @param  apFooter a valid VHD footer
    @param  apHeader a valid VHD header
*/
CVhdDynDiffBase::CVhdDynDiffBase(const TVhdFooter* apFooter, const TVhdHeader* apHeader)
                :CVhdFileBase(apFooter), ipBAT(NULL), ipSectorMapper(NULL)
{
    //-- 1. process footer
    ASSERT(Footer().IsValid());
    ASSERT(Footer().DiskType() == EVhd_Diff  || Footer().DiskType() == EVhd_Dynamic);

    //-- 2. process header
    ASSERT(apHeader && apHeader->IsValid());
    iHeader = *apHeader;

    //-- calculate "Log2(sectors per block)"
    ASSERT(IsPowerOf2(apHeader->BlockSize()));
    iSectPerBlockLog2 = Log2(apHeader->BlockSize());
    ASSERT(iSectPerBlockLog2 > SectorSzLog2());
    iSectPerBlockLog2 -= SectorSzLog2();

}

//--------------------------------------------------------------------

CVhdDynDiffBase::~CVhdDynDiffBase()
{
    //-- Close() method should have been called before an attempt to delete this object
    ASSERT(!ipBAT && !ipSectorMapper);
}



//--------------------------------------------------------------------
/**
    Implements "Open" semantics for the common functionality between dynamic and diff. VHD files
    Doesn't change this object state, only leaf classes can do this.

    @return standard error code, 0 on success.
*/
int  CVhdDynDiffBase::Open()
{
    if(State() != EInitialised)
    {
        ASSERT(0);
        return KErrAlreadyExists;
    }

    if(!Footer().IsValid() || (Footer().DiskType() != EVhd_Dynamic && Footer().DiskType() != EVhd_Diff))
    {
        ASSERT(0);
        return KErrCorrupt;
    }

    //-- open the base object, doesn't change the object state
    int nRes = CVhdFileBase::Open();
    if(nRes != KErrNone)
    {
        DBG_LOG("CVhdDynDiffBase::Open() error! code:%d", nRes);
        return nRes;
    }

    ASSERT(!ipBAT);
    ASSERT(!ipSectorMapper);

    CAutoClosePtr<CBat> pBat(new CBat(*this));
    CAutoClosePtr<CSectorMapper> pSecMapper(new CSectorMapper(*this));

    ipBAT = pBat.release();
    ipSectorMapper = pSecMapper.release();

    return KErrNone;
}

//--------------------------------------------------------------------
/**
    Deallocate / release resources, close file handle.
    The caller must be sure that all dirty data are flushed. See Flush().

    @param  aForceClose     if true, ignores dirty data. not for a normal use
*/
void CVhdDynDiffBase::Close(bool aForceClose /*=false*/)
{
    DBG_LOG("CVhdDynDiffBase::Close(%d)[0x%p], State:%d",aForceClose, this, State());

    //-- make best effort to flush data/metadata
    Flush();

    //-- deallocate BAT
    if(ipBAT)
    {
        ipBAT->Close(aForceClose);
        delete ipBAT;
        ipBAT = NULL;
    }

    //-- deallocate sector mapper
    if(ipSectorMapper)
    {
        ipSectorMapper->Close(aForceClose);
        delete ipSectorMapper;
        ipSectorMapper = NULL;
    }

    CVhdFileBase::Close(aForceClose);
}

//--------------------------------------------------------------------
/**
    Flushes VHD file data and metadata to the media.
    @return standard error code, 0 on success.
*/
int CVhdDynDiffBase::Flush()
{
    if(State() != EOpened)
        return KErrGeneral;

    //-- @todo better error handling here
    int nRes1 = ipBAT->Flush();
    int nRes2 = ipSectorMapper->Flush();
    int nRes3 = CVhdFileBase::Flush();

    if(nRes1 == KErrNone && nRes2 == KErrNone && nRes3 == KErrNone)
        return KErrNone;

    DBG_LOG("CVhdDynDiffBase::Flush[0x%p], Errors! %d, %d, %d", this, nRes1, nRes2, nRes3);

    return KErrGeneral;
}

//--------------------------------------------------------------------
/**
    Invalidates cache data. If the client will try to access data with cache invalid, it will result in
    re-reading data from the media to the cache.
    Does not deallocate cache RAM, just marks data as invalid.

    @param  aIgnoreDirty    if true, ignores dirty data. not for a normal use

    @pre the cache must not be dirty; @see Flush()
*/
void CVhdDynDiffBase::InvalidateCache(bool aIgnoreDirty /*=false*/)
{
    DBG_LOG("CVhdDynDiffBase::InvalidateCache[0x%p](%d)", this, aIgnoreDirty);

    ipBAT->InvalidateCache(aIgnoreDirty);
    ipSectorMapper->InvalidateCache(aIgnoreDirty);

    CVhdFileBase::InvalidateCache(aIgnoreDirty);
}


//--------------------------------------------------------------------
/**
    Prints out some information about VHD in a human-readable form.
    @param  aStr string where the information goes.
*/
void CVhdDynDiffBase::PrintInfo(std::string& aStr) const
{
    CVhdFileBase::PrintInfo(aStr);
    iHeader.Dump(&aStr);
}

//--------------------------------------------------------------------
/**
    Get some VHD parameters
    @param  aVhdParams out: filled in parameters structure
    @param  aParentNo       number of the parent VHD file. 0 refers to _this_ file
*/
int CVhdDynDiffBase::GetInfo(TVHD_Params& aVhdInfo, uint32_t aParentNo) const
{
    ASSERT(aParentNo == 0);
    (void)aParentNo;

    //-- get parent parameters first
    CVhdFileBase::GetInfo(aVhdInfo, aParentNo);

    //-- get dynamic/differencing specific VHD parameters
    aVhdInfo.secPerBlockLog2  = SectorsPerBlockLog2();

    return KErrNone;
}



//--------------------------------------------------------------------
/**
    Calculate Sector Bitmap size in VHD sectors.
    @return Size of the block bitmap in (512-bytes) sectors
*/
uint32_t CVhdDynDiffBase::SBmp_SizeInSectors() const
{
    uint32_t nBytes = SectorsPerBlock() >> 3;  //-- 1 bit corresponds to 1 sector
    nBytes = RoundUp_ToGranularity(nBytes, SectorSzLog2());

    return (nBytes >> SectorSzLog2());
}


//--------------------------------------------------------------------
/**
    @param  aLogicalBlockNumber logical block number
    @return true if the logical block number appears to be valid in terms of number of sectors this VHD consist of.
*/
bool CVhdDynDiffBase::BlockNumberValid(uint32_t aLogicalBlockNumber) const
{
    return (aLogicalBlockNumber < Header().MaxBatEntries());
}

//--------------------------------------------------------------------
/**
    Check if VHD block is physically present in the file (has a valid BAT entry)

    @param      aLogicalBlockNumber logical block number
    @return     true if the block is physically present in the file
*/
bool CVhdDynDiffBase::IsBlockPresent(uint32_t aLogicalBlockNumber) const
{
    if(!BlockNumberValid(aLogicalBlockNumber))
        Fault(EBat_InvalidBlockNumber);

    const TBatEntry batEntry = ipBAT->ReadEntry(aLogicalBlockNumber);

    if(batEntry == KBatEntry_Unused)
        return false;

    ASSERT(BatEntryValid(batEntry));
    return true;
}




//--------------------------------------------------------------------
/**
    Append a new block with its allocation bitmap to the end of the VHD file. The footer is moved to the very last sector of the file.

    @param  aBlockSector    out: sector number of the appended block. This value should go to BAT
    @param  aSecBmpFill     if true, all bits in the block bitmap will be set to '1', otherwise to '0'
    @param  aZeroFillData   if true, the block will be explicitly filled with 0s

    @return standard error code, KErrNone on success
*/
int CVhdDynDiffBase::AppendBlock(TBatEntry& aBlockSector, bool aSecBmpFill, bool aZeroFillData)
{
    DBG_LOG("CVhdDynDiffBase::AppendBlock[0x%p], SecBmpFill:%d, DataZFill:%d", this, aSecBmpFill, aZeroFillData );

    int nRes;

    uint64_t filePos;

    //-- 1. check VHD file size; it must be multiple of sector size
    nRes = GetFileSize(filePos);
    if(nRes != KErrNone)
        return nRes;

    if(filePos & (SectorSize()-1))
    {//-- file size is supposed to be a multiple of sectors
        DBG_LOG(" Wrong file size! %llx", filePos);
        return KErrCorrupt;
    }

    //-- 2. read footer from the last sector of the file
    uint32_t currSector = U64Low((filePos >> SectorSzLog2()));
    ASSERT(currSector > 0);
    --currSector;

    CDynBuffer buf(SectorSize());

    nRes = DoRaw_ReadData(currSector, SectorSize(), buf.Ptr());
    if(nRes <0)
        return nRes;//-- this is the error code

    ASSERT(nRes == (int)SectorSize());

    //-- 2.1
    //-- in theory, we need to check VHD footer fields OriginalSize & CurrentSize;
    //-- they might need to be updated. Many VHDs are created with these fields already containing max. VHD size.
    //-- updating these fields implies recalculating footer checksums etc. Leave it for later if we need to implement it.


    //-- 3. write footer at the end of a new block, expand the file
    {
        const uint32_t newFooterSec = currSector + SBmp_SizeInSectors() + SectorsPerBlock();

        nRes = DoRaw_WriteData(newFooterSec, SectorSize(), buf.Ptr());
        if(nRes <0)
            return nRes;//-- this is the error code

        ASSERT(nRes == (int)SectorSize());
    }

    //-- 4. Create  sector allocation bitmap with required filling
    {
        const uint32_t secBmpSizeInBytes = SBmp_SizeInSectors()<<SectorSzLog2();

        buf.Resize(secBmpSizeInBytes);//-- expand buffer if necessary
        buf.FillZ();                  //-- zero-fill buffer

        if(aSecBmpFill)
        {//-- mark all sectors in bitmap as "mapped", setting appropriate bits to '1'
            const uint32_t secBmpFillBytes = 1 << (SectorsPerBlockLog2() - KBitsInByteLog2);
            ASSERT(secBmpFillBytes <= secBmpSizeInBytes);

            buf.Fill(0, secBmpFillBytes, 0xFF);
        }

        nRes = DoRaw_WriteData(currSector, secBmpSizeInBytes, buf.Ptr());
        if(nRes <0)
            return nRes;//-- this is the error code

        ASSERT(nRes == (int)secBmpSizeInBytes);

        aBlockSector = currSector;
        currSector += SBmp_SizeInSectors();
    }

    //-- 5. zero-fill block body if requested
    if(aZeroFillData)
    {
        nRes = DoRaw_FillMedia(currSector, SectorsPerBlock(), 0);
        if(nRes != KErrNone)
            return nRes;//-- this is the error code
    }

    return KErrNone;
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

int CVhdDynDiffBase::ReadSectors(uint32_t aStartSector, int aSectors, void* apBuffer, uint32_t aBufSize)
{
    DBG_LOG("#--- CVhdDynDiffBase::ReadSectors[0x%p] startSec:%d, num:%d",this, aStartSector, aSectors);

    //-- check arguments and adjust number of sectors to read if necessary
    int nRes = DoCheckRW_Args(aStartSector, aSectors, aBufSize);
    if(nRes <= 0 )
        return nRes; //-- something is wrong with the arguments

    uint32_t remSectors = (uint32_t)nRes; //-- total amount of sectors to read, possibly adjusted

    const uint32_t startBlock = SectorToBlockNumber(aStartSector);
    uint32_t cntBlocks = SectorToBlockNumber(aStartSector + remSectors -1) - startBlock + 1; //-- number of blocks the range of sectors spans

    ASSERT(cntBlocks && cntBlocks <= (VhdSizeInSectors()>>SectorsPerBlockLog2()));

    TBlkOpParams blkParams;

    blkParams.iCurrBlock    = startBlock;
    blkParams.iCurrSectorL  = aStartSector;
    blkParams.ipData        = (uint8_t*)apBuffer;

    do
    {
        --cntBlocks;

        //-- amount of sectors we can read from the _current_ block
        const uint32_t KSectorsToRead = (cntBlocks) ? SectorsPerBlock()-SectorInBlock(blkParams.iCurrSectorL)   //-- sectors span more than one block, access those ones that are in the current block only.
                                                    : remSectors;                                               //-- all sectors fit in one block
        blkParams.iNumSectors = KSectorsToRead;

        nRes = DoReadSectorsFromBlock(blkParams);
        if(nRes <0)
        {
            DBG_LOG("#--- CVhdDynDiffBase::DoReadSectorsFromBlock[0x%p] error!, code:%d", this, nRes);
            return nRes;
        }


        remSectors -= KSectorsToRead;
        ++blkParams.iCurrBlock;

    } while(cntBlocks);


    ASSERT(!remSectors);
    return (blkParams.iCurrSectorL - aStartSector);
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

int CVhdDynDiffBase::WriteSectors(uint32_t aStartSector, int aSectors, const void* apBuffer, uint32_t aBufSize)
{
    DBG_LOG("#--- CVhdDynDiffBase::WriteSectors[0x%p] startSec:%d, num:%d",this, aStartSector, aSectors);

    if(ReadOnly())
        return -EBADF;

    //-- check arguments and adjust number of sectors to write if necessary
    int nRes = DoCheckRW_Args(aStartSector, aSectors, aBufSize);
    if(nRes <= 0 )
        return nRes; //-- something is wrong with the arguments

    uint32_t remSectors = (uint32_t)nRes; //-- total amount of sectors to read, possibly adjusted

    const uint32_t startBlock = SectorToBlockNumber(aStartSector);
    uint32_t cntBlocks = SectorToBlockNumber(aStartSector + remSectors -1) - startBlock + 1; //-- number of blocks the range of sectors spans

    ASSERT(cntBlocks && cntBlocks <= (VhdSizeInSectors()>>SectorsPerBlockLog2()));

    TBlkOpParams blkParams;
    blkParams.iCurrBlock    = startBlock;
    blkParams.iCurrSectorL  = aStartSector;
    blkParams.ipData        = (uint8_t*)apBuffer;

    do
    {
        --cntBlocks;

        //-- amount of sectors we can write to the _current_ block
        const uint32_t KSectorsToWrite = (cntBlocks) ? SectorsPerBlock()-SectorInBlock(blkParams.iCurrSectorL)   //-- sectors span more than one block, access those ones that are in the current block only.
                                                     : remSectors;                                               //-- all sectors fit in one block
        blkParams.iNumSectors = KSectorsToWrite;

        nRes = DoWriteSectorsToBlock(blkParams);
        if(nRes <0)
        {
            DBG_LOG("#--- CVhdDynDiffBase::DoWriteSectorsToBlock[0x%p] error!, code:%d", this, nRes);
            return nRes;
        }


        remSectors -= KSectorsToWrite;
        ++blkParams.iCurrBlock;

    } while(cntBlocks);

    ASSERT(!remSectors);

    if(blkParams.iFlushMetadata)
        nRes = Flush();

    if(nRes < 0)
        return nRes;

    return (blkParams.iCurrSectorL - aStartSector);
}






//####################################################################
//#  CHandleMapper class implementation
//####################################################################

//--------------------------------------------------------------------
/**
    Constructor.
    @param  aMaxClients max. number of clients that can be handled, or amount of handles issued. 0..256
*/
CHandleMapper::CHandleMapper(uint32_t aMaxClients)
                 :iMaxClients(aMaxClients), iNumClients(0)
{
    DBG_LOG("===>>>>>> ::CHandleMapper(%d)", aMaxClients);

    //-- 1. check max. amount of clients, hardcoded at present. May need to be changed later
    if(aMaxClients > 256 || aMaxClients < 1)
    {
        Fault(EHContainer_NumClients);
    }

    //-- 2. create and initialise an array of pointers to the CVhdFileBase class objects.
    iPtrArray = new CVhdFileBase* [iMaxClients];
    FillZ(iPtrArray, iMaxClients*sizeof(CVhdFileBase*));
}

//--------------------------------------------------------------------
/** destructor */
CHandleMapper::~CHandleMapper()
{
    DBG_LOG("<<<<<<=== ::~CHandleMapper()");
    //-- check if we are trying to destroy container that still has pointers to the objects
    if(iNumClients)
    {
        DBG_LOG("destroying CHandleMapper that still has clients!");
        Fault(EHContainer_DestroyingDirty);
    }

#ifdef _DEBUG
    //-- additional check in DEBUG mode that we are not going to deallocate an array with pointers in use
    for(uint32_t i=0; i<MaxClients(); ++i)
    {
        if(iPtrArray[i])
        {
            DBG_LOG("destroying CHandleMapper that still has client [%d]!", i);
            Fault(EHContainer_DestroyingDirty);
        }

    }
#endif //_DEBUG


    delete[] iPtrArray;
}

//--------------------------------------------------------------------
/**
    Allocate a handle by a pointer to the CVhdFileBase object. Actually, associates a unique handle with a given pointer.
    @param  apObj pointer to the CVhdFileBase object, must not be NULL
    @return on success: a positive integer (handle) that can be used by a client instead of the raw pointer. @see GetPtrByHandle()
            on error - negative error code.
*/
TVhdHandle CHandleMapper::MapHandle(CVhdFileBase* apObj)
{
    if(!apObj)
    {
        ASSERT(0);
        return KErrBadHandle;
    }

    if(!HasRoom())
        return KErrNotFound; //-- there is no room in the pointer container; max. number of clients exceeded

    uint32_t i;

#ifdef _DEBUG
    //-- additional check in DEBUG mode that we haven't got a pointer to the same object in the array already
    for(i=0; i<MaxClients(); ++i)
    {
        if(iPtrArray[i] == apObj)
            Fault(EAlreadyExists);
    }
#endif //_DEBUG


    //-- find a free slot in the array; simple linear search for now
    for(i=0; i<MaxClients(); ++i)
    {
        if(!iPtrArray[i])
            break;
    }

    ASSERT(i<MaxClients());

    iPtrArray[i] = apObj;

    ASSERT(iNumClients < MaxClients());
    ++iNumClients;

    return i+1;
}

//--------------------------------------------------------------------
/**
    "Unbind" a VHD handle from the corresponding pointer to the CVhdFileBase object and free slot in the container.

    @param  aVhdHandle VHD handle to unbind from the pointer; Shall be a valid handle.
    @return KErrNone on success, negative error code otherwise
*/
int CHandleMapper::UnmapHandle(TVhdHandle aVhdHandle)
{
    --aVhdHandle;
    if(aVhdHandle < 0 || aVhdHandle >= (int)MaxClients())
    {
        Fault(EIndexOutOfRange);
    }

    if(!iPtrArray[aVhdHandle])
    {//-- the handle seems to be already unmapped
        ASSERT(0);
        return KErrNotFound;
    }

    iPtrArray[aVhdHandle] = NULL;

    ASSERT(iNumClients > 0);
    --iNumClients;

    return KErrNone;
}


//####################################################################
//#  TVHD_Params class implementation
//####################################################################

//--------------------------------------------------------------------

TVHD_Params::TVHD_Params()
{
    Init();
}

TVHD_Params::TVHD_Params(const TVHD_ParamsStruct& aParams)
{
    memcpy(this, &aParams, sizeof(*this));
}

void TVHD_Params::Init()
{
    ASSERT_COMPILE(sizeof(TVHD_Params) == sizeof(TVHD_ParamsStruct));
    FillZ(*this);
}


//--------------------------------------------------------------------
/**
    Debug only method. Dump contents of this structure to log
*/
void TVHD_Params::Dump() const
{
#ifdef _DEBUG
    char buf[128];

    DBG_LOG("---- TVHD_Params:");

    DBG_LOG("vhdFileName: '%s'", vhdFileName);
    DBG_LOG("vhdModeFlags: 0x%x", vhdModeFlags);
    DBG_LOG("vhdType: %d", vhdType);
    DBG_LOG("secSizeLog2: %d", secSizeLog2);
    DBG_LOG("secPerBlockLog2: %d", secPerBlockLog2);
    DBG_LOG("vhdDiskGeometry: C:%d, H:%d, S:%d", vhdDiskGeometry.Cylinders, vhdDiskGeometry.Heads, vhdDiskGeometry.SecPerTrack);
    DBG_LOG("vhdSectors: %d", vhdSectors);

    uuid_unparse_upper(vhdUUID, buf);
    DBG_LOG("iUUID: {%s}", buf);

    FillZ(buf); memcpy(buf, vhdCreatorApp, sizeof(vhdCreatorApp));
    DBG_LOG("vhdCreatorApp: '%s'", buf);

    DBG_LOG("vhdCreatorVer: 0x%x", vhdCreatorVer);
    DBG_LOG("vhdCreatorHostOs: 0x%x", vhdCreatorHostOs);
    DBG_LOG("vhdParentName: '%s'", vhdParentName);

#endif
}





