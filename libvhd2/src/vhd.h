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
    @file libvhd2 private header file, definition of internal methods and structures
*/


#ifndef __VHD_H__
#define __VHD_H__

#include <iconv.h>
#include <limits.h>
#include <string>

#include "utils.h"
#include "../include/libvhd2.h"
//--------------------------------------------------------------------
//-- some configuration parameters

/**
    Default value for the max. size of scratch buffers in bytes. These buffers are usually used for
    copying data, filling parts of the file with some byte pattern etc.
    The bigger buffer, the less iterations may be required.
    Must be a multiple of 512 bytes, at least
*/
const uint32_t KDefScratchBufSize = 128*K1KiloByte;


/** Max. number of SectorBitmaps cached in their LRU cache  */
const uint32_t KMaxCached_SectorBitmaps = 64;


/**
    controls how blocks are created for the Dynamic VHDs.
    If "true",  sector allocation bitmap for the appended block will have all bits set to '1' to minimize hassle with bitmaps dealing
    If "false", sector allocation bitmap will have all bits set to '0'
*/
const bool KDynVhd_CreateFullyMappedBlock = true;

/**
    controls how blocks are created for the Differencing VHDs.
    If "true", all necessary data sectors will be copied from the parent in order to have a bitmap with all bits set to '1'.
    This takes longer than just adding an empty block, but can be beneficial later on because of less dealing with the bitmaps
    This flag is superseded by using VHDF_OPMODE_PURE_BLOCKS

    If "false", no data will be copied from parents, just added an empty block with bitmap filled with 0s
*/
const bool KDiffVhd_CreateFullyMappedBlock = false;


/**
    if true, then zero fill a block being appended to the differencing VHD. It may take some time.
    Zero-filling block works only when KDiffVhd_CreateFullyMappedBlock is false.
    Doesn't make much sence.
*/
const bool KDiffVhd_ZeroFillAppendedBlock = false;

/**
    if true,  the parent VHD file will be opened on demand, only when it is necessary to read data from it or get its parameters
    if false, the parent VHD file will be opened during opening the child.
*/
const bool KDiffVhd_LazyOpenParent = true;


/**
    It seems that there is a kind of compatibility issues with the legacy libvhd and code that uses it.
    For dynamic/Diff. VHDs if sectors count calculated from CHS is less than Footer::CurrentSize, then number of sectors is rounded up to the block size.
    This allows to address all sectors in all blocks, though it can be out of the CHS region.
    Not sure how correct it is, meke it configurable. @see TVhdFooter::CHS_DiskSzInSectors()

*/
const bool KRoundUp_Chs_SizeToBlock = true;


//--------------------------------------------------------------------
const uint32_t KDefSecSizeLog2 = 9;                 ///< Log2(default sector size)
const uint32_t KDefSecSize = 1 << KDefSecSizeLog2;  ///< default sector size
const uint32_t KDefSecPerBlockLog2 = 12;            ///< Log2(sectors per block) -> 2MB blocks


typedef uint32_t TBatEntry;                     ///< BAT entry type, 32 bits
const TBatEntry KBatEntry_Unused = 0xFFFFFFFF;  ///< unused BAT entry value
const TBatEntry KBatEntry_Invalid= 0x00;        ///< specifies invalid BAT entry


const char KPathDelim = '/';
const char KCurrDir[] = "./";
const char KParentDir[] = "../";


//--------------------------------------------------------------------
/**
    Simple wrapper around TVHD_ParamsStruct
*/
class TVHD_Params: public TVHD_ParamsStruct
{
 public:
    TVHD_Params();
    TVHD_Params(const TVHD_ParamsStruct& aParams);
    void Dump() const;

    void Init();

};

//--------------------------------------------------------------------
/**
    representation of VHD file footer
*/
class TVhdFooter
{
  public:

    TVhdFooter();


    void Init();
    bool Init(TVHD_Params& aParams);


    bool IsValid(std::string* apStr = NULL) const;

    void Internalise(const void* apBuf);
    void Externalise(void* apBuf, bool aFixChecksum);

    void Dump(std::string* apStr = NULL) const;

    inline TVhdType DiskType() const;               //-- iDiskType
    inline uint64_t DataOffset() const;             //-- iDataOffset
    inline uint64_t CurrDiskSizeInBytes() const;    //-- iCurrSize
    inline uint32_t DiskGeometry() const;           //-- iDiskGeometry

    uint32_t TimeStamp() const   {return iTimeStamp;} ///< This VHD TS
    const uuid_t& UUID() const   {return iUUID;}      ///< This VHD UUID



    uint32_t CHS_DiskSzInSectors() const;
    void GetInfo(TVHD_Params& aVhdinfo) const;

  public:
    enum {KSize = KDefSecSize}; ///< VHD footer size in bytes, see specs

  protected:
    TVhdFooter(const TVhdFooter&);


  protected:

    /** this object state*/
    enum TState
    {
        ENotPopulated,  ///< invalid, unpopulated state
        EChkSumValid,   ///< object is populated and footer checksum is valid
        EChkSumInvalid  ///< footer checksum is Invalid
    };

    enum
    {
        KFileFmtVer = 0x00010000, ///< current file format version
        KChkSumFieldOffset = 64,  ///< "checksum" field offset in the footer
    };

    /** footer reatures bit flags, see specs  */
    enum TFeatures
    {
        EFeature_None  = 0,     ///< no features enabled
        EFeature_Temp  = 0x01,  ///< temporary
        EFeature_Resvd = 0x02,  ///< reserved
    };


    bool ChkSumValid() const {return iState == EChkSumValid;} ///< @return true if footer checksum is valid

    uint32_t DoCalculateChkSum(const void* apBuf) const;


    uint32_t DG_Cylinders() const {return U32High(iDiskGeometry);}         ///< @return number of Cylinders from iDiskGeometry CHS field
    uint32_t DG_Heads()     const {return U16High(U32Low(iDiskGeometry));} ///< @return number of Heads from iDiskGeometry CHS field
    uint32_t DG_SpTrack()   const {return U16Low(U32Low(iDiskGeometry));}  ///< @return number of Sectors per track from iDiskGeometry CHS field

    uint32_t DG_MakeGeometry(uint32_t aCyl, uint32_t aHeads, uint32_t aSpTr) const;
    uint32_t DG_SectorsToCHS(uint32_t aDiskSizeInSectors) const;
    uint32_t ChsToSectors(uint32_t aChsVal) const;

  protected:

    TState      iState;         ///< this object state

    //-- these members represent VHD footer fields in the file
    char        iCookie[8];     ///< Identifies original creator of the disk
    uint32_t    iFeatures;      ///< Feature Supported bitfield
    uint32_t    iFileFormatVer; ///< version of disk file
    uint64_t    iDataOffset;    ///< Abs. offset from the file beginning to next structure
    uint32_t    iTimeStamp;     ///< Creation time. secs since 1/1/2000GMT

    char        iCreatorApp[4]; ///< Creator application name
    uint32_t    iCreatorVer;    ///< Creator version (major,minor)
    uint32_t    iCreatorHostOs; ///< Creator host OS

    uint64_t    iOrgSize;       ///< Size at creation (bytes)
    uint64_t    iCurrSize;      ///< Current size of disk (bytes)

    uint32_t    iDiskGeometry;  ///< Disk geometry
    uint32_t    iDiskType;      ///< Disk type
    uint32_t    iChecksum;      ///< footer checksum
    uuid_t      iUUID;          ///< VHD UUID

    uint8_t     iSavedState;    ///< saved state
    uint8_t     iReserved[427];


    friend  int DoGenerateVHD_Fixed(int, const TVHD_Params&, TVhdFooter&);
    friend  int DoGenerateVHD_Dynamic(int,  TVHD_Params&, TVhdFooter&aFooter);
    friend  int DoGenerateVHD_Differencing(int,  TVHD_Params&, TVhdFooter&aFooter);

};

//--------------------------------------------------------------------
/**
    Parent locator entry structure. Used in VHD header. @see TVhdHeader
*/
class TParentLocatorEntry
{
 public:

    /** some known Platform Codes */
    enum TPlatCodes
    {
        EPlatCode_NONE = 0x0,          ///< none
        EPlatCode_WI2R = 0x57693272,   ///< "Wi2r", deprecated
        EPlatCode_WI2K = 0x5769326B,   ///< "Wi2k", deprecated
        EPlatCode_W2RU = 0x57327275,   ///< "W2ru", Windows relative path (UTF-16)
        EPlatCode_W2KU = 0x57326B75,   ///< "W2ku", Windows absolute path (UTF-16)
        EPlatCode_MAC  = 0x4D616320,   ///< "Mac",  MacOS alias stored as a blob.
        EPlatCode_MACX = 0x4D616358,   ///< "MacX", File URL (UTF-8), see RFC 2396.
    };

    enum {KSize = 24}; ///< Parent locator entry size in bytes, see specs

    bool IsValid() const;

    uint32_t  PlatCode() const      {return  iCode;      }
    uint32_t  DataSpace() const     {return  iDataSpace; }
    uint32_t  DataLen() const       {return  iDataLen;   }
    uint64_t  DataOffset() const    {return  iDataOffset;}

    void  SetPlatCode(uint32_t aCode)          {iCode = aCode;}
    void  SetDataSpace(uint32_t aDataSpace)    {iDataSpace = aDataSpace; }
    void  SetDataLen(uint32_t aDataLen)        {iDataLen = aDataLen;}
    void  SetDataOffset(uint64_t aDataOffset)  {iDataOffset = aDataOffset;}


 public:
    TParentLocatorEntry(uint32_t aCode = EPlatCode_NONE);

    void Init(uint32_t aCode = EPlatCode_NONE);

    void Internalise(const void* apBuf);
    void Externalise(void* apBuf) const;

    void Dump(const char* aSzPrefix, std::string* apStr) const;


 protected:
    TParentLocatorEntry(const TParentLocatorEntry&);

 protected:
    uint32_t    iCode;          ///< Platform code @see TPlatCodes
    uint32_t    iDataSpace;     ///< Number of sectors(specs) to store parent locator. But Win implementation puts there number of _bytes_ that these sectors occupy
    uint32_t    iDataLen;       ///< Actual length of parent locator in bytes
    uint32_t    iResvd;         ///< reserved
    uint64_t    iDataOffset;    ///< absolute file offset to the parent file locator data.

};


//--------------------------------------------------------------------
/**
    representation of a dynamic of differencing VHD file header
*/
class TVhdHeader
{
  public:

    TVhdHeader();


    void Init();
    bool Init(TVHD_Params& aParams);

    bool IsValid() const;
    void Internalise(const void* apBuf);
    void Externalise(void* apBuf, bool aFixChecksum);

    void Dump(std::string* apStr = NULL) const;

    uint64_t BatOffset()     const  {return iBatOffset;}
    uint32_t MaxBatEntries() const  {return iMaxBatEntries;}
    uint32_t BlockSize()     const  {return iBlockSize;}

    uint32_t Parent_TimeStamp() const   {return iParentTimeStamp;}  ///< Parent's VHD TS from the header
    void SetParent_TimeStamp(uint32_t aTS);

    const uuid_t& Parent_UUID() const   {return iParent_UUID;}      ///< Parent's VHD UUID from the header
    void SetParent_UUID(const uuid_t& aUUID);

    void SetParent_UName(const void* apData, uint aNumBytes);

    const TParentLocatorEntry& GetParentLocatorEntry(uint32_t aIndex) const;
    void  SetParentLocatorEntry(uint32_t aIndex, TParentLocatorEntry& aEntry);


  public:
    enum
    {
        KSize = 1024,           ///< VHD header size in bytes, see specs
        KNumParentLoc = 8,      ///< number of parent locator entries, see VHD specs
        KPNameLen_bytes = 512,  ///< parent Unicode name len in _bytes_. corresponds to 256 UTF16 symbols
    };


  protected:
    TVhdHeader(const TVhdHeader&);

    bool ChkSumValid() const {return iState == EChkSumValid;} ///< @return true if header checksum is valid
    uint32_t DoCalculateChkSum(const void* apBuf) const;

  protected:

    /** this object state*/
    enum TState
    {
        ENotPopulated,  ///< invalid, unpopulated state
        EChkSumValid,   ///< object is populated and header checksum is valid
        EChkSumInvalid  ///< header checksum is Invalid
    };

    enum
    {
        KHdrFmtVer = 0x00010000,///< current header format version
        KChkSumFieldOffset = 36,///< "checksum" field offset in the header
    };

    TState      iState;         ///< this object state

    //-- these members represent VHD header fields in the file
    char        iCookie[8];     ///< Header identifier. Should contain "cxsparse"
    uint64_t    iDataOffset;    ///< Abs. offset to the next VHD structure. Unused, all 0xFFs
    uint64_t    iBatOffset;     ///< Abs. offset to BAT
    uint32_t    iHdrVersion;    ///< Version of the header
    uint32_t    iMaxBatEntries; ///< max. number of entries in BAT
    uint32_t    iBlockSize;     ///< Block size in bytes. Must be power of 2.
    uint32_t    iChecksum;      ///< header checksum

    uuid_t      iParent_UUID;   ///< parent UUID for diff disks
    uint32_t    iParentTimeStamp; ///< Parent disk's modification time. secs since 1/1/2000GMT

    uint32_t    iResvd1;
    uint8_t     iParentUName[KPNameLen_bytes];      ///< Parent unicode(UTF16) name. this is raw data from the file. 512 bytes -> 256 UTF16 symbols
    TParentLocatorEntry iParentLoc[KNumParentLoc];  ///< parent locator entries
    uint8_t     iResvd2[256];

    friend  int DoGenerateVHD_Dynamic(int, TVHD_Params&, TVhdFooter&aFooter);
    friend  int DoGenerateVHD_Differencing(int, TVHD_Params&, TVhdFooter&);
};

//--------------------------------------------------------------------
/**
    A helper class for calculating checksums according to the VHD specs
*/
class TChkSum
{
 public:
    TChkSum()               {Init();}
    void Init()             {iChecksum= 0;}         ///< initialise the checksum
    uint32_t Value() const  {return ~iChecksum ;}   ///< get resulted checksum value, see specs

    void Update(const void* apBuf, uint32_t aBytes);

 private:
    uint32_t    iChecksum; ///< calculated checksum
};





//--------------------------------------------------------------------
/**
    An abstract base class for various VHDs handling classes
    Implements common functionality for all supported VHD types
*/
class CVhdFileBase
{
 public:
    virtual ~CVhdFileBase();

    //-- public virtual methods API
    virtual TVhdType VhdType() const =0;


    virtual int  Open();
    virtual void Close(bool aForceClose = false);
    virtual int Flush();
    virtual void InvalidateCache(bool aIgnoreDirty=false);

    virtual int ReadSectors(uint32_t aStartSector, int aSectors, void* apBuffer, uint32_t aBufSize) = 0;
    virtual int WriteSectors(uint32_t aStartSector, int aSectors, const void* apBuffer, uint32_t aBufSize) = 0;
    virtual int DiscardSectors(uint32_t aStartSector, int aSectors) = 0;


    virtual void PrintInfo(std::string& aStr) const;
    virtual int GetInfo(TVHD_Params& aVhdInfo, uint32_t aParentNo) const;

    virtual bool IsBlockPresent(uint32_t aLogicalBlockNumber) const = 0;
    virtual int GetBlockBitmap(uint32_t aLogicalBlockNumber, CBitVector& aSrcBitmap) const = 0;
    virtual int CoalesceDataIn(uint32_t aVhdChainLength) {Fault(EMustNotBeCalled);}
    virtual const CVhdFileBase* GetParentOpened(uint32_t aParentNo);
    virtual int ChangeParentVHD(const char *aNewParentFileName) {Fault(EMustNotBeCalled);}



    //-- non-virtual API
    inline bool ReadOnly() const;
    inline bool BlockPureMode() const;
    inline bool TrimEnabled() const;

    const TVhdFooter& Footer() const {return iFooter;}
    inline uint32_t VhdSizeInSectors() const;


    const char* FilePath() const {return iFilePath.c_str();}///< @return real fully-qualified VHD file path
    const char* FileName() const;                           ///< @return File name only, without a path


    //-- factory methods
    static CVhdFileBase* CreateFromFile(const char *aFileName, uint32_t aModeFlags, int& aErrCode);
    static int GenerateFile(TVHD_Params& aParams);



    //-- low-level disk access internal interface, not a public API.
    int DoRaw_ReadData (uint32_t aStartSector, int aBytes, void* apBuffer) const;
    int DoRaw_WriteData(uint32_t aStartSector, int aBytes, const void* apBuffer) const;
    int DoRaw_FillMedia(uint32_t aStartSector, uint32_t aSectors, uint8_t aFill) const;
    int DoRaw_CheckMediaFill(uint32_t aStartSector, uint32_t aSectors, uint8_t aFill) const;



 protected:
    CVhdFileBase(const TVhdFooter* apFooter);
    CVhdFileBase(CVhdFileBase&);
    CVhdFileBase& operator=(const CVhdFileBase&);



    /** this object states */
    enum TState
    {
        EInvalid = 0,   ///< invalid initial state. The object can't be opened from this state
        EInitialised,   ///< the object is initialised and can be opened.
        EOpened,        ///< the VHD file is opened OK and ready for data access
    };

    TState State() const            {return iState;}
    void SetState(TState aState)    {iState = aState;}

    //--

    uint32_t ModeFlags() const {return iModeFlags;}
    uint32_t SectorSzLog2() const {return KDefSecSizeLog2;}
    uint32_t SectorSize()   const {return KDefSecSize;}

    //--
    int DoCheckRW_Args(uint32_t aStartSector, int aSectors, uint32_t aBufSize) const;
    int GetFileSize(uint64_t& aFileSize) const;




 private:
    int DoFlush();
    static int DoOpenFile(const char *aFileName, uint32_t aModeFlags, int& aFd);

 private:


    int         iFileDesc;  ///< file descriptor
    std::string iFilePath;  ///< file real path
    TState      iState;     ///< this object state
    uint32_t    iModeFlags; ///< open/operational mode bit flags
    uint32_t    iVhdSizeSec;///< VHD size in sectors, based on CHS value from VHD Footer. Not a real file size!
    TVhdFooter  iFooter;    ///< VHD Footer
};


class CBat;
class CSectorMapper;
//--------------------------------------------------------------------
/**
    A base class implementing a common functionality for Dynamic and Differencing VHD files
    Not intended for instantiation.
*/
class CVhdDynDiffBase : public CVhdFileBase
{
 public:
    const TVhdHeader& Header() const  {return iHeader;}
    TVhdHeader& Header()              {return iHeader;}

    uint32_t SBmp_SizeInSectors() const;

    inline bool BatEntryValid(TBatEntry aEntry) const;

    inline uint32_t SectorsPerBlockLog2() const;
    inline uint32_t SectorsPerBlock() const;

    virtual int ReadSectors(uint32_t aStartSector, int aSectors, void* apBuffer, uint32_t aBufSize);
    virtual int WriteSectors(uint32_t aStartSector, int aSectors, const void* apBuffer, uint32_t aBufSize);

 protected:
    CVhdDynDiffBase(const TVhdFooter* apFooter, const TVhdHeader* apHeader);
   ~CVhdDynDiffBase();

    virtual int  Open();
    virtual void Close(bool aForceClose = false);
    virtual int Flush();
    virtual void InvalidateCache(bool aIgnoreDirty = false);

    virtual void PrintInfo(std::string& aStr) const;
    virtual int GetInfo(TVHD_Params& aVhdInfo, uint32_t aParentNo) const;

    virtual bool IsBlockPresent(uint32_t aLogicalBlockNumber) const;


 protected:

    inline uint32_t SectorToBlockNumber(uint32_t aSectorNumber) const;
    inline uint32_t SectorInBlock(uint32_t aSectorNumber) const;
    bool BlockNumberValid(uint32_t aLogicalBlockNumber) const;

    int AppendBlock(TBatEntry& aBlockSector, bool aSecBmpFill, bool aZeroFillData);

    /** an internal helper structure describing some parameters for reading/writing sector extents from blocks*/
    struct TBlkOpParams
    {
        TBlkOpParams() {FillZ(*this);}

        uint32_t iCurrBlock;    ///< current block number we are dealing with
        uint8_t* ipData;        ///< pointer to the external buffer
        uint32_t iCurrSectorL;  ///< current logical sector of the VHD
        uint32_t iNumSectors;   ///< number of sectors to process in the single block
        uint32_t iFlushMetadata;///< true if we need to flush metadata (applicable to write ops only)
    };

    virtual int DoReadSectorsFromBlock(TBlkOpParams &aParams) = 0;
    virtual int DoWriteSectorsToBlock(TBlkOpParams &aParams) = 0;

 protected:

    CBat*           ipBAT;          ///< an object to work with the Block Allocation Table (BAT)
    CSectorMapper*  ipSectorMapper; ///< pointer to the object that handles the sector allocation bitmaps for dynamic & diff. VHDs. Can be NULL for RO Dynamic VHD

 private:
    uint32_t    iSectPerBlockLog2;  ///< Log2(sectors per block)
    TVhdHeader  iHeader;            ///< VHD header.
};

//--------------------------------------------------------------------
/**
    Represents fixed VHD file
*/
class CVhdFileFixed : public CVhdFileBase
{
 public:
    CVhdFileFixed(const TVhdFooter* apFooter);

    virtual TVhdType VhdType() const {return  EVhd_Fixed;}
    virtual int  Open();
    virtual int GetInfo(TVHD_Params& aVhdInfo, uint32_t aParentNo) const;


    virtual int ReadSectors(uint32_t aStartSector, int aSectors, void* apBuffer, uint32_t aBufSize);
    virtual int WriteSectors(uint32_t aStartSector, int aSectors, const void* apBuffer, uint32_t aBufSize);
    virtual int DiscardSectors(uint32_t aStartSector, int aSectors);


    virtual bool IsBlockPresent(uint32_t aLogicalBlockNumber) const;
    virtual int GetBlockBitmap(uint32_t aLogicalBlockNumber, CBitVector& aSrcBitmap) const;

    virtual int CoalesceDataIn(uint32_t aVhdChainLength) {Fault(EMustNotBeCalled);}


 protected:
    CVhdFileFixed();
    CVhdFileFixed(const CVhdFileFixed&);
    CVhdFileFixed& operator=(const CVhdFileFixed&);
};

//--------------------------------------------------------------------
/**
    Represents dynamic VHD file
*/
class CVhdFileDynamic: public CVhdDynDiffBase
{
 public:

    CVhdFileDynamic(const TVhdFooter* apFooter, const TVhdHeader* apHeader);

    virtual TVhdType VhdType() const {return  EVhd_Dynamic;}
    virtual int  Open();
    virtual int GetInfo(TVHD_Params& aVhdInfo, uint32_t aParentNo) const;
    virtual int DiscardSectors(uint32_t aStartSector, int aSectors);
    virtual int GetBlockBitmap(uint32_t aLogicalBlockNumber, CBitVector& aSrcBitmap) const;


 protected:
    CVhdFileDynamic();
    CVhdFileDynamic(const CVhdFileDynamic&);
    CVhdFileDynamic& operator=(const CVhdFileDynamic&);


 private:
    int ProcessPureBlocksMode();

    virtual int DoReadSectorsFromBlock(TBlkOpParams &aParams);
    virtual int DoWriteSectorsToBlock(TBlkOpParams &aParams);

};


//--------------------------------------------------------------------
/**
    Represents differencing VHD file
*/
class CVhdFileDiff : public CVhdDynDiffBase
{
 public:

    CVhdFileDiff(const TVhdFooter* apFooter, const TVhdHeader* apHeader);
   ~CVhdFileDiff();

    virtual TVhdType VhdType() const {return  EVhd_Diff;}

    virtual int  Open();
    virtual void Close(bool aForceClose = false);
    virtual void PrintInfo(std::string& aStr) const;
    virtual int Flush();
    virtual void InvalidateCache(bool aIgnoreDirty = false);
    virtual int GetInfo(TVHD_Params& aVhdInfo, uint32_t aParentNo) const;
    virtual int DiscardSectors(uint32_t aStartSector, int aSectors);
    virtual int GetBlockBitmap(uint32_t aLogicalBlockNumber, CBitVector& aSrcBitmap) const;

    virtual int CoalesceDataIn(uint32_t aVhdChainLength);
    virtual const CVhdFileBase* GetParentOpened(uint32_t aParentNo);
    virtual int ChangeParentVHD(const char *aNewParentFileName);

 protected:
    CVhdFileDiff();
    CVhdFileDiff(const CVhdFileDiff&);
    CVhdFileDiff& operator=(const CVhdFileDiff&);

 private:

    int DoFindParentFile(std::string& aParentRealName) const;
    int DoReadParentLocator(uint aIndex, std::string& aLocator, bool aHackPathToUnix) const;
    inline int DoReadSectorsFromParent(uint32_t aStartSector, int aSectors, void* apBuffer, uint32_t aBufSize);
    int DoCopySectorsFromParent(uint32_t aStartSectorParentL, uint32_t aStartSectorChildP, uint32_t aSectors);
    int ProcessPureBlocksMode();

    int  OpenParentVHD(const char* apParentFileName = NULL) const;
    void CloseParentVHD() const;

    int DoCoalesceBlock(uint32_t aLogicalBlockNumber, uint32_t aVhdChainLength);

    bool DoValidateParentGeometry(const CVhdFileBase* apParentVhd) const;

    virtual int DoReadSectorsFromBlock(TBlkOpParams &aParams);
    virtual int DoWriteSectorsToBlock(TBlkOpParams &aParams);


 private:
    mutable CVhdFileBase* iParent; ///< parent VHD, NULL if none

};


//--------------------------------------------------------------------
/**
    An ad-hoc class to handle pointers to CVhdFileBase and associate them with the handles (TVhdHandle).
    Simply associates a unique integer number of TVhdHandle type with the pointer to the CVhdFileBase object. Doesn't do any object allocation/deallocation.

    Theoretically should be a singleton and a static object.
    Not intended for derivation.
    (?) It might be necessary to implement some type of locking in this class to deal with multiple ctients accessing it from different threads.
    (?) @todo think about making it better, possibly implementing "pointer ownership" semantics ?
    (?) @todo think about making it less strict about invalid handles - does it have to explode everything ?
*/
class CHandleMapper
{
 public:
    CHandleMapper(uint32_t aMaxClients);
   ~CHandleMapper();

    uint32_t MaxClients() const {return iMaxClients;}                  ///< @return Max. number of clients supported
    uint32_t NumClients() const {return iNumClients;}                  ///< @return current number of clients
    bool  HasRoom()    const {return NumClients() < MaxClients();}  ///< @return true, if another client can register (allocate a VHD handle)


    TVhdHandle MapHandle(CVhdFileBase* apObj);
    int UnmapHandle(TVhdHandle aVhdHandle);

    inline CVhdFileBase* GetPtrByHandle(TVhdHandle aVhdHandle) const;

 private:
    const uint32_t  iMaxClients;    ///< max. number of clients (or allocated handles)
    uint32_t        iNumClients;    ///< current number of clients (occupied slots in the iPtrArray);
    CVhdFileBase**  iPtrArray;      ///< ptr. to the array of objects(size==iMaxClients); The TVhdHandle is actually index in this array +1
};


int GenerateParentLocator(const char* aThisFileName, const char* aParentFileName, TParentLocatorEntry& aLocatorEntry, CDynBuffer& aLocatorData);



int CoalesceChain_IntoTail(CVhdFileBase* apVhdTail, uint32_t aChainLength);
int CoalesceChain_Safely(CVhdFileBase* apVhdTail, uint32_t aChainLength, uint32_t aChainIdxResult);

#include "vhd.inl"


#endif /* __VHD_H__ */

