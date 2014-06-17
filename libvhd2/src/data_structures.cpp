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

    @file libvhd2 data structures related stuff
*/



#include <limits.h>

#include "vhd.h"

static const char KVhdFooter_Cookie[] = "conectix"; //-- default VHD footer cookie string
static const char KVhdHeader_Cookie[] = "cxsparse"; //-- VHD header identifier


//--------------------------------------------------------------------
/**
    Helper function. Copies some value from the raw buffer to a destination with optional endianness correction

    @param  apBuf   source buffer
    @param  apDest  points to the destination
    @param  aSize   number of bytes to copy
    @param  aPos    position (offset) in the source buffer, updated on return (added aSize)
    @param  aConvertEndian  if true the copied value will be converter from BigEndian order. only 2,4,8-bytes values are supported.
*/
static void BufGet(const void* apBuf, void* apDest, uint32_t aSize, uint32_t& aPos, bool aConvertEndian = false)
{
    ASSERT(apBuf && apDest && aSize);
    ASSERT(aPos < 65536); //-- just in case, not supposed to deal with big buffers

    memcpy(apDest, ((const uint8_t*)apBuf + aPos), aSize);
    aPos += aSize;

    //-- convert extracted value from BigEndian if required. only 16, 32 and 64 bit values are supported
    //-- not very nice, but simple. Performance here doesn't matter.
#if __BYTE_ORDER == __LITTLE_ENDIAN
    if(aConvertEndian)
    {
        //-- ?? __bswap_*  seem to generate compile warnings, @todo: try to use  __bswap_constant_* ???
        switch(aSize)
        {
        case sizeof(uint16_t):
        *((uint16_t*)apDest) = __bswap_16(*((uint16_t*)apDest));
        break;

        case sizeof(uint32_t):
        *((uint32_t*)apDest) = __bswap_32(*((uint32_t*)apDest));
        break;

        case sizeof(uint64_t):
        *((uint64_t*)apDest) = __bswap_64(*((uint64_t*)apDest));
        break;

        default:
            ASSERT(0);
        break;

        }
    }
#else
    (void) aConvertBE;
#endif

}


//--------------------------------------------------------------------
/**
    Helper function. Copies some data to the raw buffer with optional endianness correction

    @param  apBuf   destination buffer buffer
    @param  apSrc   points to the source
    @param  aSize   number of bytes to copy
    @param  aPos    position (offset) in the destination buffer, updated on return (added aSize)
    @param  aConvertEndian  if true the copied value will be converter from BigEndian order. only 2,4,8-bytes values are supported.
*/
static void BufPut(void* apBuf, const void* apSrc, uint32_t aSize, uint32_t& aPos, bool aConvertEndian = false)
{
    ASSERT(apBuf && apSrc && aSize);
    ASSERT(aPos < 65536); //-- just in case, not supposed to deal with big buffers

    uint8_t* pDest = ((uint8_t*)apBuf) + aPos;
    memcpy(pDest, apSrc, aSize);
    aPos += aSize;

    //-- convert extracted value from BigEndian if required. only 16, 32 and 64 bit values are supported
    //-- not very nice, but simple. Performance here doesn't matter.
#if __BYTE_ORDER == __LITTLE_ENDIAN
    if(aConvertEndian)
    {
        //-- ?? __bswap_*  seem to generate compile warnings, @todo: try to use  __bswap_constant_* ???
        switch(aSize)
        {
        case sizeof(uint16_t):
        *((uint16_t*)pDest) = __bswap_16(*((uint16_t*)pDest));
        break;

        case sizeof(uint32_t):
        *((uint32_t*)pDest) = __bswap_32(*((uint32_t*)pDest));
        break;

        case sizeof(uint64_t):
        *((uint64_t*)pDest) = __bswap_64(*((uint64_t*)pDest));
        break;

        default:
            ASSERT(0);
        break;

        }
    }
#else
    (void) aConvertBE;
#endif

}


//####################################################################
//#   TVhdFooter class implementation
//####################################################################



//--------------------------------------------------------------------
/** constructor */
TVhdFooter::TVhdFooter()
{
    Init();
}

//--------------------------------------------------------------------
/**
    Set the object to its initial state.
*/
void TVhdFooter::Init()
{
    FillZ(*this);
    iState = ENotPopulated;
}

//--------------------------------------------------------------------
/**
    @return Disk size in sectors, calculated from CHS value (iDiskGeometry) field
*/
uint32_t TVhdFooter::CHS_DiskSzInSectors() const
{
    ASSERT(ChkSumValid());

    const uint32_t sectors = DG_Cylinders()*DG_Heads()*DG_SpTrack(); //-- "real" amount of addressable sectors calculated from CHS

    if( KRoundUp_Chs_SizeToBlock &&
        ((CurrDiskSizeInBytes() >> KDefSecSizeLog2) > sectors) &&
        (DiskType() == EVhd_Dynamic || DiskType() == EVhd_Diff))
    {//-- round up sectors number to the block size if CurrDiskSizeInBytes() reports larger volume than CHS.
     //-- another approach would be to just use CurrDiskSizeInBytes(). Both solutions are dodgy.
        return  RoundUp_ToGranularity(sectors, KDefSecPerBlockLog2);
    }
    else
    {
        return sectors;
    }
}


//--------------------------------------------------------------------
/**
    Test that certain fields contain specified values (according to the specs)

    @param  apStr optional pointer to the string where debug information will be appended to. If NULL, nothing happens
    @return true if the footer's data look valid
*/
bool TVhdFooter::IsValid(std::string* apStr /*=NULL*/) const
{
    //-- 1. the object must be populated and its calculated checksum should be valid
    if(!ChkSumValid())
    {
        StrLog(apStr, "Checksum Invalid!");
        return false;
    }

    //-- 2. check some data fields for validity

    //-- "features" field
    if(! (iFeatures & EFeature_Resvd))
    {
        StrLog(apStr, "invalid: iFeatures:0x%x", iFeatures);
        return false;
    }

    //-- 'file format version'
    if(iFileFormatVer != KFileFmtVer)
    {
        StrLog(apStr,"invalid: iFileFormatVer:0x%x", iFileFormatVer);
        return false;
    }

    //-- disk type check. 6 is a magic value, it is the last type mentioned in specs.
    if(iDiskType > 6)
    {
        StrLog(apStr,"invalid: iDiskType:%d", iDiskType);
        return false;
    }

    //-- supported disk types check.
    if(iDiskType != EVhd_Fixed && iDiskType != EVhd_Dynamic && iDiskType != EVhd_Diff)
    {
        StrLog(apStr,"Unsupported DiskType:%d", iDiskType);
        return false;
    }

    //--  'Data Offset' for fixed disks
    if((DiskType() == EVhd_Fixed) && (iDataOffset != ULLONG_MAX))
    {
        StrLog(apStr,"invalid: iDataOffset:0x%llx", iDataOffset);
        return false;
    }

    //-- @todo:  find out if it is possible to check CHS values vs Disk size recorded. What's allocation granularity  ???

    return true;
}

//--------------------------------------------------------------------
/**
    calculate footer checksum using raw data from the buffer.
    Need to skip "checksum" 4-bytes field in the buffer at offset 64 .

    @param   ponts to the buffer with the footer's data at least 512 bytes long
    @return  calculated checksum
*/
uint32_t TVhdFooter::DoCalculateChkSum(const void* apBuf) const
{
    TChkSum chkSum;

    chkSum.Update(apBuf, KChkSumFieldOffset);
    chkSum.Update(((const uint8_t*)apBuf)+(KChkSumFieldOffset + sizeof(uint32_t)), KSize-(KChkSumFieldOffset + sizeof(uint32_t)));

    return chkSum.Value();
}

//--------------------------------------------------------------------
/**
    Take raw data from the buffer and populate internal structure.
    Also calculates footer checksum
    @param  apBuf   pointer to the buffer with the data. It should be at least KDefSecSize(512) bytes long
*/
void TVhdFooter::Internalise(const void* apBuf)
{

    iState = EChkSumInvalid;

    //-- 1. calculate footer checksum using raw data from the buffer.
    const uint32_t chkSum = DoCalculateChkSum(apBuf);

    //-- 2. extract raw data from the buffer and place them into member variables.
    //-- vhd file data structures' fields are big-endian, need to convert some of them to the host's order
    uint32_t pos = 0;

    BufGet(apBuf, &iCookie,         sizeof(iCookie),        pos);
    BufGet(apBuf, &iFeatures,       sizeof(iFeatures),      pos, true);
    BufGet(apBuf, &iFileFormatVer,  sizeof(iFileFormatVer), pos, true);
    BufGet(apBuf, &iDataOffset,     sizeof(iDataOffset),    pos, true);
    BufGet(apBuf, &iTimeStamp,      sizeof(iTimeStamp),     pos, true);
    BufGet(apBuf, &iCreatorApp,     sizeof(iCreatorApp),    pos);
    BufGet(apBuf, &iCreatorVer,     sizeof(iCreatorVer),    pos, true);
    BufGet(apBuf, &iCreatorHostOs,  sizeof(iCreatorHostOs), pos, true);
    BufGet(apBuf, &iOrgSize,        sizeof(iOrgSize),       pos, true);
    BufGet(apBuf, &iCurrSize,       sizeof(iCurrSize),      pos, true);
    BufGet(apBuf, &iDiskGeometry,   sizeof(iDiskGeometry),  pos, true);
    BufGet(apBuf, &iDiskType,       sizeof(iDiskType),      pos, true);
    BufGet(apBuf, &iChecksum,       sizeof(iChecksum),      pos, true);
    BufGet(apBuf, &iUUID,           sizeof(iUUID),          pos);
    BufGet(apBuf, &iSavedState,     sizeof(iSavedState),    pos);
    BufGet(apBuf, &iReserved,       sizeof(iReserved),      pos);

    ASSERT(pos == KSize);

    //-- 3. validate checksum
    iState = (chkSum == iChecksum) ? EChkSumValid : EChkSumInvalid ;
}

//--------------------------------------------------------------------
/**
    Serialise this object's internal structure to a buffer that represents VHD footer in the file.

    @param  apBuf           pointer to the buffer where the data be seralized. It should be at least KDefSecSize(512) bytes long
    @param  aFixChecksum    if true, then VHD footer checksum will be calculated and placed to the correct field in the buffer.
                            also, will change this object state to EChkSumValid
                            if false then the caller must guarantee that the object is in the EChkSumValid already.

*/
void TVhdFooter::Externalise(void* apBuf, bool aFixChecksum)
{
    //-- 1. place member variables to the raw data buffer
    //-- vhd file data structures' fields are big-endian, need to convert some of them
    uint32_t pos = 0;

    BufPut(apBuf, &iCookie,         sizeof(iCookie),        pos);
    BufPut(apBuf, &iFeatures,       sizeof(iFeatures),      pos, true);
    BufPut(apBuf, &iFileFormatVer,  sizeof(iFileFormatVer), pos, true);
    BufPut(apBuf, &iDataOffset,     sizeof(iDataOffset),    pos, true);
    BufPut(apBuf, &iTimeStamp,      sizeof(iTimeStamp),     pos, true);
    BufPut(apBuf, &iCreatorApp,     sizeof(iCreatorApp),    pos);
    BufPut(apBuf, &iCreatorVer,     sizeof(iCreatorVer),    pos, true);
    BufPut(apBuf, &iCreatorHostOs,  sizeof(iCreatorHostOs), pos, true);
    BufPut(apBuf, &iOrgSize,        sizeof(iOrgSize),       pos, true);
    BufPut(apBuf, &iCurrSize,       sizeof(iCurrSize),      pos, true);
    BufPut(apBuf, &iDiskGeometry,   sizeof(iDiskGeometry),  pos, true);
    BufPut(apBuf, &iDiskType,       sizeof(iDiskType),      pos, true);
    BufPut(apBuf, &iChecksum,       sizeof(iChecksum),      pos, true);
    BufPut(apBuf, &iUUID,           sizeof(iUUID),          pos);
    BufPut(apBuf, &iSavedState,     sizeof(iSavedState),    pos);
    BufPut(apBuf, &iReserved,       sizeof(iReserved),      pos);

    ASSERT(pos == KSize);

    if(!aFixChecksum)
    {//-- caller shouldn't try to externalise incorrect footer
        ASSERT(ChkSumValid());
    }
    else
    {//-- fix the checksum both in buffer and in the object
        iChecksum = DoCalculateChkSum(apBuf);

        pos = KChkSumFieldOffset;
        BufPut(apBuf, &iChecksum, sizeof(iChecksum),pos, true);
        iState = EChkSumValid;
    }

}


//--------------------------------------------------------------------
/**
    Dumps footer's contents to a log and/or string buffer
    @param  apStr optional pointer to the string where debug information will be appended to. If NULL, nothing happens
*/
void TVhdFooter::Dump(std::string* apStr /*=NULL*/) const
{
    char buf[128];

    StrLog(apStr, "");
    StrLog(apStr, "--- VHD Footer dump. Checksum: %s ---", ChkSumValid()? "valid" : "INVALID !!!");

    FillZ(buf); memcpy(buf, iCookie, sizeof(iCookie));
    StrLog(apStr, "iCookie: '%s'", buf);

    StrLog(apStr, "iFeatures: 0x%x", iFeatures);
    StrLog(apStr, "iFileFormatVer: 0x%x", iFileFormatVer);
    StrLog(apStr, "iDataOffset: 0x%llx", iDataOffset);

    VHD_Time_To_String(iTimeStamp, buf, sizeof(buf));
    StrLog(apStr, "iTimestamp: 0x%x [%s]", iTimeStamp, buf);

    FillZ(buf); memcpy(buf, iCreatorApp, sizeof(iCreatorApp));
    StrLog(apStr,"iCreatorApp: '%s'", buf);

    StrLog(apStr, "iCreatorVer: 0x%x", iCreatorVer);
    StrLog(apStr, "iCreatorHostOs: 0x%x", iCreatorHostOs);

    StrLog(apStr, "iOrgSize:  0x%llx, sectors:%d", iOrgSize,  iOrgSize  >> KDefSecSizeLog2);
    StrLog(apStr, "iCurrSize: 0x%llx, sectors:%d", iCurrSize, iCurrSize >> KDefSecSizeLog2);

    StrLog(apStr,"iDiskGeometry: 0x%x, CHS=%d:%d:%d, sectors:%d", iDiskGeometry, DG_Cylinders(), DG_Heads(), DG_SpTrack(), (DG_Cylinders()*DG_Heads()*DG_SpTrack()) );

    StrLog(apStr, "iDiskType: %d", iDiskType);

    uuid_unparse_upper(iUUID, buf);
    StrLog(apStr, "iUUID: {%s}", buf);

    StrLog(apStr, "iSavedState: 0x%x", iSavedState);
    StrLog(apStr, "--- end of VHD Footer dump ---");
}

//--------------------------------------------------------------------
/**
    Put some parameters to the TVHD_Params  structure
    @param  aVhdParams out: filled in parameters structure
*/
void TVhdFooter::GetInfo(TVHD_Params& aVhdInfo) const
{
    ASSERT_COMPILE(sizeof(iCreatorApp) == sizeof(aVhdInfo.vhdCreatorApp));
    ASSERT_COMPILE(sizeof(iCookie) == sizeof(aVhdInfo.vhdCookie));

    ASSERT(ChkSumValid());

    aVhdInfo.vhdType    = DiskType();
    aVhdInfo.vhdSectors = CHS_DiskSzInSectors();

	aVhdInfo.vhdDiskGeometry.Cylinders   = DG_Cylinders();
	aVhdInfo.vhdDiskGeometry.Heads       = DG_Heads();
	aVhdInfo.vhdDiskGeometry.SecPerTrack = DG_SpTrack();

    uuid_copy(aVhdInfo.vhdUUID, iUUID);

    memcpy(&aVhdInfo.vhdCreatorApp, &iCreatorApp, sizeof(iCreatorApp));

    aVhdInfo.vhdCreatorVer    = iCreatorVer;
    aVhdInfo.vhdCreatorHostOs = iCreatorHostOs;

    memcpy(&aVhdInfo.vhdCookie, &iCookie, sizeof(iCookie));
}

//--------------------------------------------------------------------
/**
    Makes 32-bit word representing VHD CHS value. See specs.
*/
uint32_t TVhdFooter::DG_MakeGeometry(uint32_t aCyl, uint32_t aHeads, uint32_t aSpTr) const
{
    ASSERT(aCyl <= 0xFFFF);
    ASSERT(aHeads <= 0xFF);
    ASSERT(aSpTr <= 0xFF);

    const uint32_t val = (aCyl << 16) | (aHeads << 8) | aSpTr;
    return val;
}

//--------------------------------------------------------------------
/**
    @return Number of sectors corresponding to the VHD CHS value
    @param  aChsVal 32-bit word representing VHD CHS value. See specs.
*/
uint32_t TVhdFooter::ChsToSectors(uint32_t aChsVal) const
{
    const uint32_t sectors =  U32High(aChsVal) * U16High(U32Low(iDiskGeometry)) * U16Low(U32Low(iDiskGeometry));
    return sectors;
}

//--------------------------------------------------------------------
/**
    Calculates CHS value from the total number of sectors. Borrowed from the MS specs.
    @param      aDiskSizeInSectors number of sectors
    @return     calculated CHS value. See specs.
*/
uint32_t TVhdFooter::DG_SectorsToCHS(uint32_t aDiskSizeInSectors) const
{
    ASSERT(aDiskSizeInSectors);

    //-- copied from MS VHD specs
    uint32_t secs = aDiskSizeInSectors;
    uint32_t cylinders, heads, spt, cth;

	if (secs > 65535 * 16 * 255)
		secs = 65535 * 16 * 255;

	if (secs >= 65535 * 16 * 63)
	{
		spt   = 255;
		cth   = secs / spt;
		heads = 16;
	}
	else
	{
		spt   = 17;
		cth   = secs / spt;
		heads = (cth + 1023) / 1024;

		if (heads < 4)
			heads = 4;

		if (cth >= (heads * 1024) || heads > 16)
		{
			spt   = 31;
			cth   = secs / spt;
			heads = 16;
		}

		if (cth >= heads * 1024)
		{
			spt   = 63;
			cth   = secs / spt;
			heads = 16;
		}
	}

	cylinders = cth / heads;

    return DG_MakeGeometry(cylinders, heads, spt);
}

//--------------------------------------------------------------------
/**
    Populate VHD footer from the values in aParams. Also can change some values in aParams.
    It doesn't populate whole object, using only relevant fields from aParams.

    @post   leaves this object in ENotPopulated state. The caller must fill the rest before externalizing to the buffer.

    @param  aParams generic VHD parameters, some of them can be used to populate the footer
    @return true if everything is OK; false if some parameters are very badly formed
*/
bool TVhdFooter::Init(TVHD_Params& aParams)
{


    Init();

    //=== 1. VHD Type
    if(aParams.vhdType != EVhd_Fixed && aParams.vhdType != EVhd_Dynamic && aParams.vhdType != EVhd_Diff)
    {
        DBG_LOG("Unsupported DiskType:%d", aParams.vhdType);
        return false;
    }

    iDiskType = aParams.vhdType;


    //=== 2. VHD Geometry / size in sectors
    if(iDiskType == EVhd_Diff)
    {//-- diff. VHD will inherit geometry from the parent. The client is responsible for this
        iDiskGeometry = 0;

        aParams.vhdSectors = 0;
        aParams.vhdDiskGeometry.Cylinders   = 0;
        aParams.vhdDiskGeometry.Heads       = 0;
        aParams.vhdDiskGeometry.SecPerTrack = 0;
    }
    else
    {//-- process parameters specified by user
        uint32_t chs = DG_MakeGeometry(aParams.vhdDiskGeometry.Cylinders, aParams.vhdDiskGeometry.Heads, aParams.vhdDiskGeometry.SecPerTrack);

        if(!(!chs ^ !aParams.vhdSectors))
        {//-- either both are 0 or both are non 0, a conflict.
            DBG_LOG("problem with disk geometry parameters! chs:0x%x, sectors:%d", chs, aParams.vhdSectors);
            return false;
        }

        if(!chs)
        {//-- need to generate CHS from disk size in secotrs
            chs = DG_SectorsToCHS(aParams.vhdSectors);
        }

        iDiskGeometry = chs;
        aParams.vhdSectors = ChsToSectors(iDiskGeometry);
        if(!aParams.vhdSectors)
        {//-- 0 heads, for example
            DBG_LOG("problem with disk geometry! chs:0x%x", chs);
            return false;
        }

        aParams.vhdDiskGeometry.Cylinders   = DG_Cylinders();
        aParams.vhdDiskGeometry.Heads       = DG_Heads();
        aParams.vhdDiskGeometry.SecPerTrack = DG_SpTrack();
    }

    //=== 3. VHD UUID
    if(uuid_is_null(aParams.vhdUUID))
    {//-- generate random value
        uuid_generate(aParams.vhdUUID);
    }
    uuid_copy(iUUID, aParams.vhdUUID);


    //=== 4. vhdCreatorApp, vhdCreatorVer, vhdCreatorHostOs
    if(CheckFill(&aParams.vhdCreatorApp, sizeof(aParams.vhdCreatorApp),0))
    {
        memcpy(aParams.vhdCreatorApp, "vpc ", sizeof(aParams.vhdCreatorApp));
    }
    memcpy(&iCreatorApp, &aParams.vhdCreatorApp, sizeof(aParams.vhdCreatorApp));

    if(!aParams.vhdCreatorVer)
    {
        aParams.vhdCreatorVer = 0x00010000;
    }
    iCreatorVer = aParams.vhdCreatorVer;

    if(!aParams.vhdCreatorHostOs)
    {
        aParams.vhdCreatorHostOs = 0x5769326b; //-- (Wi2K)
    }
    iCreatorHostOs = aParams.vhdCreatorHostOs;

    //=== 5. some other internal fields
    iFeatures = EFeature_Resvd;
    iFileFormatVer = KFileFmtVer;

    //=== 6. cookie
    if(CheckFill(aParams.vhdCookie, sizeof(aParams.vhdCookie),0))
    {
        memcpy(aParams.vhdCookie, KVhdFooter_Cookie, sizeof(aParams.vhdCookie));
    }
    memcpy(iCookie, aParams.vhdCookie, sizeof(aParams.vhdCookie));

    //=== 7. timestamp
    iTimeStamp = VHD_Time();


    return true;
}


//####################################################################
//#   TParentLocatorEntry class implementation
//####################################################################


//--------------------------------------------------------------------
/** constructor */
TParentLocatorEntry::TParentLocatorEntry(uint32_t aCode /*=EPlatCode_NONE*/)
{
    Init(aCode);
}

//--------------------------------------------------------------------
/**
    Set the object to its initial state.
*/
void TParentLocatorEntry::Init(uint32_t aCode /*=EPlatCode_NONE*/)
{
    FillZ(*this);
    iCode = aCode;
}


//--------------------------------------------------------------------
/**
    @return true if the parent locator entry looks valid
*/
bool TParentLocatorEntry::IsValid() const
{
    //-- check the platform code
    switch(iCode)
    {
    case EPlatCode_NONE:
    case EPlatCode_WI2R:
    case EPlatCode_WI2K:
    case EPlatCode_W2RU:
    case EPlatCode_W2KU:
    case EPlatCode_MAC:
    case EPlatCode_MACX:
    return true;

    default:
    return false; //-- unsupported code
    };
}

//--------------------------------------------------------------------
/**
    Take raw data from the buffer and populate internal structure.
    @param  apBuf   pointer to the buffer with the data. It should be at least 24 bytes long
*/
void TParentLocatorEntry::Internalise(const void* apBuf)
{
    //-- extract raw data from the buffer and place them into member variables.
    //-- some fields are big-endian, need to convert them to the host's byte order
    uint32_t pos = 0;

    BufGet(apBuf, &iCode,       sizeof(iCode),      pos, true);
    BufGet(apBuf, &iDataSpace,  sizeof(iDataSpace), pos, true);
    BufGet(apBuf, &iDataLen,    sizeof(iDataLen),   pos, true);
    BufGet(apBuf, &iResvd,      sizeof(iResvd),     pos, true);
    BufGet(apBuf, &iDataOffset, sizeof(iDataOffset),pos, true);

    ASSERT(pos == KSize); //-- this structure is 24 bytes long
}

//--------------------------------------------------------------------
/**
    Serialise this object's internal structure to a buffer that represents VHD header in the file.
    @param  apBuf  pointer to the buffer where the data be seralized. It should be at least 24 bytes long
*/
void TParentLocatorEntry::Externalise(void* apBuf)  const
{
    uint32_t pos = 0;

    BufPut(apBuf, &iCode,       sizeof(iCode),      pos, true);
    BufPut(apBuf, &iDataSpace,  sizeof(iDataSpace), pos, true);
    BufPut(apBuf, &iDataLen,    sizeof(iDataLen),   pos, true);
    BufPut(apBuf, &iResvd,      sizeof(iResvd),     pos, true);
    BufPut(apBuf, &iDataOffset, sizeof(iDataOffset),pos, true);

    ASSERT(pos == KSize); //-- this structure is 24 bytes long
}


//--------------------------------------------------------------------
/**
    Debug-only method. Dumps structure contents to a log
    @param  apStr optional pointer to the string where debug information will be appended to. If NULL, nothing happens
*/
void TParentLocatorEntry::Dump(const char* aSzPrefix, std::string* apStr) const
{
    StrLog(apStr,"%s --- VHD parent locator entry dump ---", aSzPrefix ? aSzPrefix : "");

    if(iCode == EPlatCode_NONE)
    {//-- this entry looks unused, save space in the log
        StrLog(apStr,"   PlatCode:none 0x%x, 0x%x, 0x%x, 0x%x, 0x%llx", iCode, iDataSpace, iDataLen, iResvd, iDataOffset);
    }
    else
    {
        StrLog(apStr,"   iCode: 0x%x", iCode);
        StrLog(apStr,"   iDataSpace: %d", iDataSpace);
        StrLog(apStr,"   iDataLen: %d", iDataLen);
        StrLog(apStr,"   iResvd: 0x%x", iResvd);
        StrLog(apStr,"   iDataOffset: 0x%llx", iDataOffset);
    }
}



//####################################################################
//#  TVhdHeader class implementation
//####################################################################

//--------------------------------------------------------------------
/** constructor */
TVhdHeader::TVhdHeader()
{
    Init();
}

//--------------------------------------------------------------------
/**
    Set the object to its initial state.
*/
void TVhdHeader::Init()
{
    FillZ(*this);
    iState = ENotPopulated;
}

//--------------------------------------------------------------------
/**
    Populate VHD HEADER from the values in aParams. Also can change some values in aParams.
    It doesn't populate whole object, using only relevant fields from aParams.

    @post   leaves this object in ENotPopulated state. The caller must fill the rest before externalizing to the buffer.

    @param  aParams generic VHD parameters, some of them can be used to populate the footer
    @return true if everything is OK; false if some parameters are very badly formed
*/
bool TVhdHeader::Init(TVHD_Params& aParams)
{
    ASSERT(aParams.vhdType == EVhd_Dynamic || aParams.vhdType == EVhd_Diff);
    Init();

    //=== 1. cookie
    memcpy(&iCookie, KVhdHeader_Cookie, sizeof(iCookie));

    //=== 2. Data Offset
    iDataOffset = ULLONG_MAX;

    //=== 3. Header Version
    iHdrVersion = KHdrFmtVer;

    //== 4. Block Size (bytes)
    const uint32_t blockSzLog2 = (aParams.secPerBlockLog2 + aParams.secSizeLog2);
    iBlockSize = 1 << blockSzLog2;

    //== 5. max BAT entries
    ASSERT(aParams.vhdSectors);
    iMaxBatEntries = 1 + ((aParams.vhdSectors-1) >> aParams.secPerBlockLog2);

    //-- for differencing VHD the caller must fill the rest of the required parameters basing on parent VHD

    return true;
}

//--------------------------------------------------------------------
/**
    Test that certain fields contain specified values (according to the specs)
    @return true if the header's data look valid
*/
bool TVhdHeader::IsValid() const
{
    //-- 1. the object must be populated and its calculated checksum should be valid
    if(!ChkSumValid())
        return false;

    //-- 2. check some data fields for validity

    if(strncmp(iCookie, KVhdHeader_Cookie, 8))
    {//-- "cookie" field must contain "cxsparse"
        DBG_LOG("invalid: iCookie");
        return false;
    }

    //-- 'Data Offset' field
    if(iDataOffset != ULLONG_MAX)
    {
        DBG_LOG("invalid: iDataOffset:0x%llx", iDataOffset);
        return false;
    }

    //-- 'header version'
    if(iHdrVersion != KHdrFmtVer)
    {
        DBG_LOG("invalid: iHdrVersion:0x%x", iHdrVersion);
        return false;
    }

    //-- 'Block size'; 2MB is a default value, but can be different in theory
    if(!iBlockSize || !IsPowerOf2(iBlockSize) || iBlockSize < KDefSecSize)
    {
        DBG_LOG("invalid: iBlockSize:0x%x", iBlockSize);
        return false;
    }

    //-- validate parent locator entries (known codes)
    for(uint i=0; i<KNumParentLoc; ++i)
    {
        if(!GetParentLocatorEntry(i).IsValid())
        {
        DBG_LOG("Parent Loc. entry [%d] is invalid!", i);
        GetParentLocatorEntry(i).Dump(NULL, NULL);
        return false;
        }
    }


    return true;
}


//--------------------------------------------------------------------
/**
    calculate header checksum using raw data from the buffer.
    Need to skip "checksum" 4-bytes field in the buffer at offset 36.

    @param   ponts to the buffer with the footer's data at least 512 bytes long
    @return  calculated checksum
*/
uint32_t TVhdHeader::DoCalculateChkSum(const void* apBuf) const
{
    TChkSum chkSum;

    chkSum.Update(apBuf, KChkSumFieldOffset);
    chkSum.Update(((const uint8_t*)apBuf)+(KChkSumFieldOffset + sizeof(uint32_t)), KSize-(KChkSumFieldOffset + sizeof(uint32_t)));

    return chkSum.Value();
}


//--------------------------------------------------------------------
/**
    Serialise this object's internal structure to a buffer that represents VHD header in the file.

    @param  apBuf           pointer to the buffer where the data be seralized. It should be at least 1024 bytes long
    @param  aFixChecksum    if true, then VHD header checksum will be calculated and placed to the correct field in the buffer.
                            also, will change this object state to EChkSumValid
                            if false then the caller must guarantee that the object is in the EChkSumValid already.

*/
void TVhdHeader::Externalise(void* apBuf, bool aFixChecksum)
{
    //-- 1. place member variables to the raw data buffer
    //-- vhd file data structures' fields are big-endian, need to convert some of them
    uint32_t pos = 0;

    BufPut(apBuf, &iCookie,             sizeof(iCookie),            pos);
    BufPut(apBuf, &iDataOffset,         sizeof(iDataOffset),        pos, true);
    BufPut(apBuf, &iBatOffset,          sizeof(iBatOffset),         pos, true);
    BufPut(apBuf, &iHdrVersion,         sizeof(iHdrVersion),        pos, true);
    BufPut(apBuf, &iMaxBatEntries,      sizeof(iMaxBatEntries),     pos, true);
    BufPut(apBuf, &iBlockSize,          sizeof(iBlockSize),         pos, true);
    BufPut(apBuf, &iChecksum,           sizeof(iChecksum),          pos, true);
    BufPut(apBuf, &iParent_UUID,        sizeof(iParent_UUID),       pos);
    BufPut(apBuf, &iParentTimeStamp,    sizeof(iParentTimeStamp),   pos, true);
    BufPut(apBuf, &iResvd1,             sizeof(iResvd1),            pos, true);
    BufPut(apBuf, &iParentUName,        sizeof(iParentUName),       pos);


    //-- import parent locator entries
    for(int i=0; i<KNumParentLoc; ++i)
    {
        uint8_t* pEntry = ((uint8_t*)apBuf) + pos;
        iParentLoc[i].Externalise(pEntry);
        pos += TParentLocatorEntry::KSize;
    }

    BufPut(apBuf, &iResvd2, sizeof(iResvd2), pos);

    ASSERT(pos == KSize);

    if(!aFixChecksum)
    {//-- caller shouldn't try to externalise incorrect footer
        ASSERT(ChkSumValid());
    }
    else
    {//-- fix the checksum both in buffer and in the object
        iChecksum = DoCalculateChkSum(apBuf);

        pos = KChkSumFieldOffset;
        BufPut(apBuf, &iChecksum, sizeof(iChecksum), pos, true);
        iState = EChkSumValid;
    }
}


//--------------------------------------------------------------------
/**
    Take raw data from the buffer and populate internal structure.
    Also calculates VHD header checksum
    @param  apBuf   pointer to the buffer with the data. It should be at least 1024 bytes long
*/
void TVhdHeader::Internalise(const void* apBuf)
{

    iState = EChkSumInvalid;

    //-- 1. calculate checksum using raw data from the buffer.
    const uint32_t chkSum = DoCalculateChkSum(apBuf);

    //-- 2. extract raw data from the buffer and place them into member variables.
    //-- vhd file data structures' fields are big-endian, need to convert some of them to the host's order
    uint32_t pos = 0;

    BufGet(apBuf, &iCookie,             sizeof(iCookie),            pos);
    BufGet(apBuf, &iDataOffset,         sizeof(iDataOffset),        pos, true);
    BufGet(apBuf, &iBatOffset,          sizeof(iBatOffset),         pos, true);
    BufGet(apBuf, &iHdrVersion,         sizeof(iHdrVersion),        pos, true);
    BufGet(apBuf, &iMaxBatEntries,      sizeof(iMaxBatEntries),     pos, true);
    BufGet(apBuf, &iBlockSize,          sizeof(iBlockSize),         pos, true);
    BufGet(apBuf, &iChecksum,           sizeof(iChecksum),          pos, true);
    BufGet(apBuf, &iParent_UUID,        sizeof(iParent_UUID),       pos);
    BufGet(apBuf, &iParentTimeStamp,    sizeof(iParentTimeStamp),   pos, true);
    BufGet(apBuf, &iResvd1,             sizeof(iResvd1),            pos, true);
    BufGet(apBuf, &iParentUName,        sizeof(iParentUName),       pos);

    //-- import parent locator entries
    for(int i=0; i<KNumParentLoc; ++i)
    {
        const uint8_t* pEntry = ((const uint8_t*)apBuf) + pos;
        iParentLoc[i].Internalise(pEntry);
        pos += TParentLocatorEntry::KSize;
    }

    BufGet(apBuf, &iResvd2, sizeof(iResvd2), pos);

    ASSERT(pos == KSize);

    //-- 3. validate checksum
    iState = (chkSum == iChecksum) ? EChkSumValid : EChkSumInvalid ;
}



//--------------------------------------------------------------------
/**
    Debug-only method. Dumps VHD header contents to a log
    @param  apStr optional pointer to the string where debug information will be appended to. If NULL, nothing happens
*/
void TVhdHeader::Dump(std::string* apStr /*=NULL*/) const
{
    char buf[512];

    StrLog(apStr,"");
    StrLog(apStr,"--- VHD Header dump. Checksum: %s ---", ChkSumValid() ? "valid" : "INVALID !!!");

    FillZ(buf); memcpy(buf, iCookie, sizeof(iCookie));
    StrLog(apStr,"iCookie: '%s'", buf);

    StrLog(apStr,"iDataOffset: 0x%llx", iDataOffset);
    StrLog(apStr,"iBatOffset: 0x%llx", iBatOffset);
    StrLog(apStr,"iHdrVersion: 0x%x", iHdrVersion);

    StrLog(apStr,"iMaxBatEntries: %d", iMaxBatEntries);
    StrLog(apStr,"iBlockSize: 0x%x", iBlockSize);
    StrLog(apStr,"iChecksum: 0x%x", iChecksum);

    uuid_unparse_upper(iParent_UUID, buf);
    StrLog(apStr, "iParent_iUUID: {%s}", buf);

    VHD_Time_To_String(iParentTimeStamp, buf, sizeof(buf));

    StrLog(apStr,"iParentTimeStamp: 0x%x [%s]", iParentTimeStamp, buf);
    StrLog(apStr,"iResvd1: 0x%x", iResvd1);

    if( !UNICODE_to_ASCII(iParentUName, 250, buf, sizeof(buf), EUTF_16BE) )
    {//-- the unicode name length here is limited by 250 UNICODE characters, but it is not very important here.
        StrLog(apStr,"Parent VHD name: '%s'", buf);
    }

    for(int i=0; i<KNumParentLoc; ++i)
    {
        sprintf(buf, "[%d]", i);
        iParentLoc[i].Dump(buf, apStr);
    }
    StrLog(apStr,"--- end of VHD Header dump ---");
}


//--------------------------------------------------------------------
const TParentLocatorEntry& TVhdHeader::GetParentLocatorEntry(uint32_t aIndex) const
{
    if(aIndex >= KNumParentLoc)
        Fault(EIndexOutOfRange);

    return iParentLoc[aIndex];
}

void  TVhdHeader::SetParentLocatorEntry(uint32_t aIndex, TParentLocatorEntry& aEntry)
{
    if(aIndex >= KNumParentLoc)
        Fault(EIndexOutOfRange);

    iParentLoc[aIndex] = aEntry;
}


void TVhdHeader::SetParent_TimeStamp(uint32_t aTS)
{
    iParentTimeStamp = aTS;
}

void TVhdHeader::SetParent_UUID(const uuid_t& aUUID)
{
    uuid_copy(iParent_UUID, aUUID);
}

void TVhdHeader::SetParent_UName(const void* apData, uint aNumBytes)
{
    ASSERT(aNumBytes && aNumBytes <= KPNameLen_bytes);
    FillZ(iParentUName);
    memcpy(iParentUName, apData, Min((uint)KPNameLen_bytes, aNumBytes));
}




//####################################################################
//#   TChkSum class implementation
//####################################################################

/**
    Update the checksum with the bytes from the buffer.

    @param  apBuf   pointer to the buffer with data
    @param  aBytes  number of bytes to process
*/
void TChkSum::Update(const void* apBuf, uint32_t aBytes)
{
    ASSERT(aBytes);

    for(uint32_t i=0; i<aBytes; ++i)
        iChecksum += ((const uint8_t*)apBuf)[i];

}

