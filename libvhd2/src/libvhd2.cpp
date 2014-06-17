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

    @file libvhd2 implementation
*/
#include <fcntl.h>
#include <unistd.h>

#include "vhd.h"



//--------------------------------------------------------------------
//-- some global objects

/** maps VHD handles to corresponding CVhdFileBase objects*/
CHandleMapper handleMapper(KMaxVhdClients);


//--------------------------------------------------------------------
/*
    Create and open VHD file.

    @param  apParams    Structure, describing VHD file parameters. Note that the real parameters of the created VHD file may be slightly different from supplied.
                        The real parameters can be retrieved with the help of VHD_Info() call after the file is created.
                        The bare minimum is (fixed and dynamic VHD):
                            vhdFileName:        Name of the file to create. The file with name this shouldn't exist.
                            vhdModeFlags:       if VHD file is created OK, it will be opened with this set of flags, see VHD_Open(), VHD_* flags.
                            vhdType:            type of the VHD

                            -- Only one of these fileds should be non-null --
                            vhdDiskGeometry:    Disk size in CHS
                            vhdSectors:         Disk size in sectors.

                        for differencing VHD:
                            vhdParentName       Name of the parent file. The file must exist.


                        all other parameters can be zero-filled and will be assigned default values.

	@return on success: handle value > 0
			on error:	negative error code.
*/

static TVhdHandle Do_VHD_Create(const TVHD_ParamsStruct* apParams)
{
    //-- make a copy of the parameters structure, it will be modified
    TVHD_Params params(*apParams);

    //-- generate the file. It will either be created OK and closed or not cretaed at all if something is wrong
    int nRes =CVhdFileBase::GenerateFile(params);
    if(nRes != KErrNone)
    {//-- failed for some reason
        ASSERT(nRes < 0);
        return nRes;
    }

    //-- open the generated file normally
    return VHD_Open(apParams->vhdFileName, apParams->vhdModeFlags);
}

//--------------------------------------------------------------------
TVhdHandle	VHD_Create(const TVHD_ParamsStruct* apParams)
{
    DBG_LOG("aFileName:%s", apParams->vhdFileName);

    if(!apParams)
    {
        ASSERT(0);
        return KErrArgument;
    }

    //-- 1. check if we hasn't exceeded number of allowed clients
    //@todo (??? shall these things be done atomically all together ???)
    if(!handleMapper.HasRoom())
    {
        DBG_LOG("Too many connections!");
        return KErrGeneral;
    }

    TVhdHandle vhdHandle = KErrGeneral;

    try
    {
        vhdHandle = Do_VHD_Create(apParams);
    }
	catch(std::exception& e)
	{
		DBG_LOG("std::exception:%s", e.what());
	}
	catch(...)
	{
        DBG_LOG("!!! non-standard exception !!!");
	}

    return vhdHandle;
}




//--------------------------------------------------------------------
/*
	Open VHD file and get its handle.
	@param	fileName 	name of the file to open
	@param	aModeFlags 	set of flags affecting opening the file. @see VHD_OPEN_*

	@return on success: handle value > 0
			on error:	negative error code.
*/

static TVhdHandle  Do_VHD_Open(const char *aFileName, uint32_t aModeFlags)
{
    int nRes;
    //-- 1. try to create an object to deal with the specified file
    //-- CAutoClosePtr will provide calling pVhd->Close() and deletion upon leaving the scope.
    CAutoClosePtr<CVhdFileBase> pVhd(CVhdFileBase::CreateFromFile(aFileName, aModeFlags, nRes));
    if(!pVhd.get())
    {
        DBG_LOG("Error creating CVhdFileBase object! code:%d", nRes);
        ASSERT(nRes < 0);
        return nRes;
    }

    //-- 2. try to open the VHD file object
    nRes = pVhd->Open();
    if(nRes != KErrNone)
    {
        DBG_LOG("Error opening CVhdFileBase object! code:%d", nRes);
        ASSERT(nRes < 0);
        return nRes;
    }

    //-- 3. associate a VHD handle with the object
    const TVhdHandle vhdHandle = handleMapper.MapHandle(pVhd.get());
    if(vhdHandle <= 0)
    {
        DBG_LOG("Error allocating a handle! code:%d", vhdHandle);
        return vhdHandle; //-- this will be the error code
    }

    //-- everything is fine now.
    (void)pVhd.release();
    return vhdHandle;
}

//--------------------------------------------------------------------
TVhdHandle  VHD_Open(const char *aFileName, uint32_t aModeFlags)
{

    DBG_LOG("aFileName:%s, aModeFlags:0x%x", aFileName, aModeFlags);

    //-- 1. check if we hasn't exceeded number of allowed clients (??? shall these things be done atomically all together ???)
    if(!handleMapper.HasRoom())
    {
        DBG_LOG("Too many connections!");
        return KErrGeneral;
    }

    {//-- 1.1 check invalid mode flags combinations

        const uint32_t incompatFlags[] =
        {//-- put here combinations of invalid flags to be checked
            VHDF_OPEN_IGNORE_PARENT | VHDF_OPEN_RDWR,
            VHDF_OPMODE_PURE_BLOCKS | VHDF_OPEN_ENABLE_TRIM,
        };

        const int arrSize = sizeof(incompatFlags) / sizeof(uint32_t);
        for(int i=0; i<arrSize; ++i)
        {
            if((aModeFlags & incompatFlags[i]) == incompatFlags[i])
            {
                DBG_LOG("Incompatible flags: 0x%x", incompatFlags[i]);
                return KErrArgument;
            }
        }
    }

    TVhdHandle vhdHandle = KErrGeneral;

    try
    {
        vhdHandle = Do_VHD_Open(aFileName, aModeFlags);
    }
	catch(std::exception& e)
	{
		DBG_LOG("std::exception:%s", e.what());
	}
	catch(...)
	{
        DBG_LOG("!!! non-standard exception !!!");
	}

    return vhdHandle;
}


//--------------------------------------------------------------------
/*
	Close VHD.
    Makes the best effort to flush the data/metadata first.
    If flush wasn't successful, closes VHD forcedly, which may result in some data not written to the VHD file.

	@param 	vhdHandle VHD hadle obtained from VHD_Open()
*/
void VHD_Close(TVhdHandle aVhdHandle)
{
    DBG_LOG("aVhdHandle:%d", aVhdHandle);

    //-- 1. find corresponding object that we are trying to close
    CVhdFileBase* pVhd = handleMapper.GetPtrByHandle(aVhdHandle);
    if(!pVhd)
    {//-- object already closed or invalid descriptor
        ASSERT(0);
        return;
    }

    try
    {
        //-- 2. unmap its handle
        int nRes = handleMapper.UnmapHandle(aVhdHandle);
        ASSERT(nRes == KErrNone);

        //-- make the best effort to flush data/metadata
        nRes = pVhd->Flush();
        if(nRes != KErrNone)
        {
            DBG_LOG("Flush() error! code:%d", nRes);
        }

        //-- 3. close and delete object. May need to use forced close if Flush has failed for some reason
        const bool bForceClose = (nRes != KErrNone);
        pVhd->Close(bForceClose);

        delete pVhd;
        pVhd = NULL;

    }
	catch(std::exception& e)
	{
		DBG_LOG("std::exception:%s", e.what());
	}
	catch(...)
	{
        DBG_LOG("!!! non-standard exception !!!");
    }

}

//--------------------------------------------------------------------
/*
	Get VHD parametes
	@param 	vhdHandle VHD hadle obtained from VHD_Open()
	@param	pVhdParams out: pointer to the structure describing parameters
	@return	0 on success,  negative value corresponding system eeror code otherwise.
*/
int VHD_Info(TVhdHandle aVhdHandle, TVHD_ParamsStruct* pVhdInfo)
{
    return VHD_ParentInfo(aVhdHandle, pVhdInfo, 0);
}


//--------------------------------------------------------------------
/*
	Get parametes of the parent VHD file.
	Applicable mostly for the Differencing VHDs.
    It is possible to traverse differencing VHD chain with the help of this function. Just call it incrementing aParentNumber
    until get KErrNotFound.


	@param 	vhdHandle       VHD hadle obtained from VHD_Open()
	@param	pVhdParams      out: pointer to the filled structure describing parameters
	@param  aParentIndex    Index of the parent VHD file. 0 refers to _this_ file and it is equivalent to calling VHD_Info()
	                        1 means "first parent", 2 - "parent of the first parent" etc.

	@return	KErrNone        on success,
            KErrNotFound    if there is no parent number aParentNumber
            negative value corresponding system eeror code otherwise.
*/

static int Do_VHD_ParentInfo(TVhdHandle aVhdHandle, TVHD_ParamsStruct* pVhdInfo, uint32_t aParentIndex)
{
    //-- 1. find corresponding object that we are trying to close
    CVhdFileBase* pVhd = handleMapper.GetPtrByHandle(aVhdHandle);
    if(!pVhd)
    {//-- object already closed or invalid descriptor
        ASSERT(0);
        return KErrBadHandle;
    }

    TVHD_Params tmpParams;
    int nRes = pVhd->GetInfo(tmpParams, aParentIndex);
    if(nRes != KErrNone)
        return nRes;

    //-- re-assign the pointers to VHD file name and parent's name
    //-- to the static local buffers. This is not nice, but at least
    //-- it will help to avoid situations when TVHD_ParamsStruct::vhdFileName points inside the object that can be deleted
    //-- as the result of the next API call.
    //-- there wasn't a requirement to make libvhd2 thread-safe
    static std::string  strVhdFileName;
    static std::string  strVhdParentName;

    strVhdFileName.clear();
    strVhdParentName.clear();

    if(tmpParams.vhdFileName)
        strVhdFileName = tmpParams.vhdFileName;

    if(tmpParams.vhdParentName)
        strVhdParentName = tmpParams.vhdParentName;

    *pVhdInfo = tmpParams; //-- copy parameters structure to the client
    pVhdInfo->vhdFileName = strVhdFileName.c_str();
    pVhdInfo->vhdParentName = strVhdParentName.c_str();


    return KErrNone;
}

//--------------------------------------------------------------------
int VHD_ParentInfo(TVhdHandle aVhdHandle, TVHD_ParamsStruct* pVhdInfo, uint32_t aParentIndex)
{
    DBG_LOG("aVhdHandle:%d, aParentIndex:%d", aVhdHandle, aParentIndex);

    int nRes = KErrGeneral;

    try
    {
        nRes = Do_VHD_ParentInfo(aVhdHandle, pVhdInfo, aParentIndex);
    }
	catch(std::exception& e)
	{
		DBG_LOG("std::exception:%s", e.what());
	}
	catch(...)
	{
        DBG_LOG("!!! non-standard exception !!!");
	}

    return nRes;
}



//--------------------------------------------------------------------
/*
	Prints out some information about VHD in a human-readable form.
	@param 	vhdHandle VHD hadle obtained from VHD_Open()
	@param  apBuf     user's buffer where information will be dumped into.
	@param  aBufSize  size of the buffer


    @return	KErrNone on success
            KErrTooBig if the buffer is too small and not all text data have been copied into it
            Standard error code otherwise.
*/
int VHD_PrintInfo(TVhdHandle aVhdHandle, void* apBuf, uint32_t aBufSize)
{
    DBG_LOG("VHD_PrintInfo:%d", aVhdHandle);

    if(aBufSize < 1)
        return KErrArgument;

    //-- 1. find corresponding object that we are trying to close
    CVhdFileBase* pVhd = handleMapper.GetPtrByHandle(aVhdHandle);
    if(!pVhd)
    {//-- object already closed or invalid descriptor
        ASSERT(0);
        return KErrBadHandle;
    }


    int nRes = KErrGeneral;
    try
    {
        //-- dump information to the dynamic buffer
        std::string str;
        pVhd->PrintInfo(str);

        str+=("========== end ==========\n");

        //-- copy information to the user buffer
        const size_t len = Min(str.size(), aBufSize-1);
        memcpy(apBuf, str.c_str(), len);
        ((char*)apBuf)[len] = '\0';

        if(len < str.size())
            nRes = KErrTooBig;
        else
            nRes = KErrNone;

    }
	catch(std::exception& e)
	{
		DBG_LOG("std::exception:%s", e.what());
	}
	catch(...)
	{
        DBG_LOG("!!! non-standard exception !!!");
	}

    return nRes;
}


//--------------------------------------------------------------------
/*
	Flush data and metadata.
	@param 	aVhdHandle      VHD hadle obtained from VHD_Open()
	@return 0 on success, negative error code otherwise.
*/
int VHD_Flush(TVhdHandle aVhdHandle)
{
    DBG_LOG("VHD_Flush:%d", aVhdHandle);

    //-- 1. find corresponding object that we are trying to close
    CVhdFileBase* pVhd = handleMapper.GetPtrByHandle(aVhdHandle);
    if(!pVhd)
    {//-- object already closed or invalid descriptor
        ASSERT(0);
        return KErrBadHandle;
    }

    int nRes = KErrGeneral;
    try
    {
        nRes = pVhd->Flush();
    }
	catch(std::exception& e)
	{
		DBG_LOG("std::exception:%s", e.what());
	}
	catch(...)
	{
        DBG_LOG("!!! non-standard exception !!!");
	}

    return nRes;
}

//--------------------------------------------------------------------
/*
    Invalidates cached data and metadata. Thus, on the next access internal caches will be re-populated.
    @pre All metadata should be clean before an attempt to invalidate caches. @see Flush(). An attempt to invalidate caches will cause panic.

	@param 	aVhdHandle      VHD hadle obtained from VHD_Open()
	@return 0 on success, negative error code otherwise.
*/
int VHD_InvalidateCaches(TVhdHandle aVhdHandle)
{
    DBG_LOG("VHD_InvalidateCaches:%d", aVhdHandle);

    //-- 1. find corresponding object that we are trying to close
    CVhdFileBase* pVhd = handleMapper.GetPtrByHandle(aVhdHandle);
    if(!pVhd)
    {//-- object already closed or invalid descriptor
        ASSERT(0);
        return KErrBadHandle;
    }


    int nRes = KErrGeneral;
    try
    {
        pVhd->InvalidateCache();
        nRes = KErrNone;
    }
	catch(std::exception& e)
	{
		DBG_LOG("std::exception:%s", e.what());
	}
	catch(...)
	{
        DBG_LOG("!!! non-standard exception !!!");
	}

    return nRes;
}



//--------------------------------------------------------------------
/*
	Read a number of sectors from VHD.
	@param 	aVhdHandle      VHD hadle obtained from VHD_Open()
	@param	aStartSector	starting sector.
	@param	aSectors		number of sectors to read 0..LONG_MAX
	@param	apBuffer		out: read data
    @param  aBufSize        buffer size in bytes

	@return	positive number of read sectors on success, negative error code otherwise.
*/
int VHD_ReadSectors(TVhdHandle aVhdHandle, uint32_t aStartSector, int aSectors, void* apBuffer, uint32_t aBufSize)
{
    DBG_LOG("aVhdHandle:%d, aStartSector:%d, aSectors:%d", aVhdHandle, aStartSector, aSectors);

    //-- 1. find corresponding object that we are trying to close
    CVhdFileBase* pVhd = handleMapper.GetPtrByHandle(aVhdHandle);
    if(!pVhd)
    {//-- object already closed or invalid descriptor
        ASSERT(0);
        return KErrBadHandle;
    }

    int nRes = KErrGeneral;
    try
    {
        nRes = pVhd->ReadSectors(aStartSector, aSectors, apBuffer, aBufSize);
    }
	catch(std::exception& e)
	{
		DBG_LOG("std::exception:%s", e.what());
	}
	catch(...)
	{
        DBG_LOG("!!! non-standard exception !!!");
	}

    return nRes;
}

//--------------------------------------------------------------------
/*
	Write a number of sectors to VHD.
	@param 	vhdHandle 	    VHD hadle obtained from VHD_Open()
	@param	aStartSector	starting sector.
	@param	aSectors		number of sectors to write 0..LONG_MAX
	@param	apBuffer		in: data to write
    @param  aBufSize        buffer size in bytes

	@return	positive number of written sectors on success, negative  error code otherwise.
*/
int VHD_WriteSectors(TVhdHandle aVhdHandle, uint32_t aStartSector, int aSectors, const void* apBuffer, uint32_t aBufSize)
{
    DBG_LOG("aVhdHandle:%d, aStartSector:%d, aSectors:%d", aVhdHandle, aStartSector, aSectors);

    //-- 1. find corresponding object that we are trying to close
    CVhdFileBase* pVhd = handleMapper.GetPtrByHandle(aVhdHandle);
    if(!pVhd)
    {//-- object already closed or invalid descriptor
        ASSERT(0);
        return KErrBadHandle;
    }


    int nRes = KErrGeneral;
    try
    {
        nRes = pVhd->WriteSectors(aStartSector, aSectors, apBuffer, aBufSize);
    }
	catch(std::exception& e)
	{
		DBG_LOG("std::exception:%s", e.what());
	}
	catch(...)
	{
        DBG_LOG("!!! non-standard exception !!!");
	}

    return nRes;
}


//--------------------------------------------------------------------
/*
	TRIM or "Discard sectors" API.
    Calling this API marks an extent of sectors as "discarded" or not containing useful information (opposite to VHD_WriteSectors() API).
    This feature can be used for efficient VHD compacting, because VHD blocks that contain all "discarded" sectors can be taken out by compacting tool.
	In order to use this API VHD file must be opened with VHDF_OPEN_ENABLE_TRIM flag.

	@param 	vhdHandle 	    VHD hadle obtained from VHD_Open()
	@param	aStartSector	starting sector.
	@param	aSectors		number of sectors to discard 0..LONG_MAX

	@return	0 on success, negative error code otherwise.
*/
int VHD_DiscardSectors(TVhdHandle aVhdHandle, uint32_t aStartSector, int aSectors)
{
    DBG_LOG("aVhdHandle:%d, aStartSector:%d, aSectors:%d", aVhdHandle, aStartSector, aSectors);

    //-- 1. find corresponding object that we are trying to close
    CVhdFileBase* pVhd = handleMapper.GetPtrByHandle(aVhdHandle);
    if(!pVhd)
    {//-- object already closed or invalid descriptor
        ASSERT(0);
        return KErrBadHandle;
    }

    if(! pVhd->TrimEnabled())
        return KErrNotSupported;

    int nRes = KErrGeneral;
    try
    {
        nRes = pVhd->DiscardSectors(aStartSector, aSectors);
    }
	catch(std::exception& e)
	{
		DBG_LOG("std::exception:%s", e.what());
	}
	catch(...)
	{
        DBG_LOG("!!! non-standard exception !!!");
	}

    return nRes;

}



//--------------------------------------------------------------------
/*
    Coalesce a chain of VHD files. Data from a subchain of VHD files will be coalesced into selected one, then VHD links fixed and
    VHDs no longer necessary will be deleted.
    VHD chain indexing: "Tail" VHD, currently opened and with the corresponding handle "aVhdHandle" has index 0, its parent has index 1, etc. until the "Head" VHD,
    whis is the first in the chain and not of the "differencing" type.

    Depending on aChainIdxResult parameter can work in 2 different modes.
        Mode 1: Coalescing VHD sub-chain _into_ the RW-opened "Tail". In this case "aChainLength" specifies number of parents to be coalesced in. Should be 1 or more.
                In this case aChainIdxResult must be == 0, which means "Tail"
                This more is not fault-tolerant; Changing VHD's parent implies modifications of several parts of VHD metadata and this can't be performed atomically.
                Thus, there is a risk that on write failure during changing VHD parent (fixing teh chain after coalescing data) that we can end up with completely broken chain.

        Mode 2: Coalescing VHD sub-chain by using intermediate temporary VHD that becomes one of the Tail's parents. This mode doesn't modify _any_ VHD in the chain and whole chain
                can be opened RO.
                In this case aChainIdxResult must be > 0, which means "some parent of the Tail".
                aChainLength specifies how many VHDs will be coalesced into the chain[aChainIdxResult]. Must be > 1.
                All data from the sub-chain coalesced into the temporary VHD file and then this file is atomically renamed to the chain[aChainIdxResult].
                Assuming that the file system renames/replaces files atomically, this way of coalescing will either succeed or the chain of VHDs will remain intact.


    @param 	vhdHandle 	    VHD hadle obtained from VHD_Open()
    @param  aChainLength    Length of the sub-chain being coalesced. See above.
                            value 0 has a special meaning: "coalesce whole chain, except the head"

    @param  aChainIdxResult Index of the resulting VHD. Also specifies the way coalescing is done. See above.
*/

static int Do_VHD_CoalesceChain(TVhdHandle aVhdHandle, uint32_t aChainLength, uint32_t aChainIdxResult)
{
    //-- find object corresponding to the handle
    CVhdFileBase* pVhd = handleMapper.GetPtrByHandle(aVhdHandle);
    if(!pVhd)
        return KErrBadHandle;


    int nRes;
    TVHD_Params vhdParams;

    //-- 1. check Tail VHD type
    nRes = pVhd->GetInfo(vhdParams, 0);
    if(nRes != KErrNone)
        return nRes;

    if(vhdParams.vhdType != EVhd_Diff)
        return KErrNotSupported;


    //-- 2. check a special case: "coalesce whole chain excluding the Head"
    if(aChainLength == 0)
    {//-- will have to walk whole chain in ordet to find out number of VHDs to coalesce
        for(;;)
        {
            nRes = pVhd->GetInfo(vhdParams, aChainLength+1);
            if(nRes != KErrNone)
            {
                DBG_LOG("Error getting %d parent info! code:%d", aChainLength, nRes);
                return nRes;
            }

            if(vhdParams.vhdType != EVhd_Diff)
                break;

            ++aChainLength;
        }
    }

    if(aChainIdxResult == 0)
    {//-- chain coalescing into the "Tail" VHD, currently opened file with aVhdHandle
     //-- "aChainLength" here means "how many parents will be merged into the opened Tail"
        if(aChainLength < 1)
            return KErrNone; //-- nothing to do

        nRes = CoalesceChain_IntoTail(pVhd, aChainLength);

    }
    else
    {//-- coalescing a subchain using a temporary file that becomes VHD in a chain[aChainIdxResult].
    //-- "aChainLength" here means "how many VHDs will be merged together and become one: chain[aChainIdxResult]"
        if(aChainLength < 2)
            return KErrNone; //-- nothing to do; merging VHD into itself doesn't make sense

        nRes = CoalesceChain_Safely(pVhd, aChainLength, aChainIdxResult);
    }

    return nRes;
}

//--------------------------------------------------------------------
int VHD_CoalesceChain(TVhdHandle aVhdHandle, uint32_t aChainLength, uint32_t aChainIdxResult)
{

    DBG_LOG("aVhdHandle:%d, aChainLength:%d, aChainIdxResult:%d", aVhdHandle, aChainLength, aChainIdxResult);

    int nRes = KErrGeneral;
    try
    {
        nRes = Do_VHD_CoalesceChain(aVhdHandle, aChainLength, aChainIdxResult);
    }
	catch(std::exception& e)
	{
		DBG_LOG("std::exception:%s", e.what());
	}
	catch(...)
	{
        DBG_LOG("!!! non-standard exception !!!");
	}


    return nRes;
}









