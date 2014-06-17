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

    @file libvhd2 public header file
*/


#ifndef __LIBVHD2_H__
#define __LIBVHD2_H__

#include <stdint.h>
#include <uuid/uuid.h>

//--------------------------------------------------------------------

/**
	VHD handle type used by this library API.
	Valid handle values are > 0
	The value < 0 represents a standard error code.
*/
typedef int TVhdHandle;

//--------------------------------------------------------------------
/** max. amount of simultaneously opened VHD files, i.e. max. amount of successful VHD_Open()  calls*/
const int KMaxVhdClients = 64;


//--------------------------------------------------------------------
//  Error codes definition
//--------------------------------------------------------------------
const int KErrNone = 0;   ///< no error

//-- library-specific generic error codes (not issued by system calls)
const int KErrNotFound          = -(1000 + 1);  ///< Unable to find the specified object
const int KErrNoMemory          = -(1000 + 2);  ///< memory allocation error
const int KErrAlreadyExists     = -(1000 + 3);  ///< the object already exists
const int KErrAccessDenied      = -(1000 + 4);  ///< Access denied
const int KErrTooBig            = -(1000 + 5);  ///< the object is too big
const int KErrArgument          = -(1000 + 6);  ///< invalid arguments
const int KErrBadHandle         = -(1000 + 7);  ///< invalid handle
const int KErrDiskFull          = -(1000 + 8);  ///< storage allocation problem
const int KErrCorrupt           = -(1000 + 9);  ///< the object is corrupted
const int KErrInUse             = -(1000 + 10); ///< the object is in use
const int KErrNotSupported      = -(1000 + 11); ///< functionality not supported
const int KErrGeneral           = -(1000 + 12); ///< error, just an error..
const int KErrBadName           = -(1000 + 13); ///< generic error related to file name


//-- library-specific finer-grained error codes
const int KErr_VhdFtr           = -(1100 + 1);  ///< bad VHD footer
const int KErr_VhdHdr           = -(1100 + 2);  ///< bad VHD header
const int KErr_VhdHdrFtr        = -(1100 + 3);  ///< both header & footer are invalid

const int KErr_VhdDiff_NoParent = -(1100 + 4);  ///< can't access Diff.VHD's parent (possibly down the chain)
const int KErr_VhdDiff_ParentId = -(1100 + 5);  ///< Diff.VHD's parent UUID or timestamp mismatch  (possibly down the chain)
const int KErr_VhdDiff_Geometry = -(1100 + 6);  ///< Diff.VHD's parent geometry mismatch (possibly down the chain)



//--------------------------------------------------------------------
/**
	VHD file(s) opening modes bit flags. Specified mode is impicitly applied to all VHDs in the chain if preent.
	@see VHD_Open().
*/

const uint32_t	VHDF_OPEN_RDONLY         = 0x00000000;	///< open VHD file in RO mode
const uint32_t	VHDF_OPEN_RDWR           = 0x00000001;	///< open VHD file in RW mode
const uint32_t	VHDF_OPEN_DIRECTIO       = 0x00000002;	///< Open VHD files O_DIRECT mode, don't use file system cache for VHD data. ??? metadata???
const uint32_t	VHDF_OPEN_EXCLUSIVE_LOCK = 0x00000004;  ///< Apply exclusive lock to all files in VHD chain, not only to the Tail, opened in RW mode

/**
    Force to open Differencing VHD file with its parent missing.
    This flag is incompatible with VHDF_OPEN_RDWR.
    It can be used for retrieving information about VHD and reading data from the sectors that are marked as "present" in sector bitmaps.
    Some operations may return KErr_VhdDiff_NoParent if parent VHD is required to complete them.
*/
const uint32_t	VHDF_OPEN_IGNORE_PARENT  = 0x00000008;

/**
    This flag shlould be used to enable VHD_DiscardSectors() API.
    It is not compatible with VHDF_OPMODE_PURE_BLOCKS flag.
*/
const uint32_t	VHDF_OPEN_ENABLE_TRIM = 0x00000010;


//--------------------------------------------------------------------

/*
	VHD operational mode bit flags. Theese flags affect the way this library
	handles VHD operations. Can be set and reset at runtime.
	@see VHD_SetMode()
*/

/**
    Opens VHD file in "pure block" mode.
    Has no effect on files opened Read-Only or Fixed VHDs.
    This flag is incompatible with VHDF_OPEN_IGNORE_PARENT and VHDF_OPEN_ENABLE_TRIM flags.

    Dynamic VHDs opened in write mode:
        When opening a file, it is checked that all present blocks' sector bitmaps have all bits set. It means that there is no need to acceess/cache bitmaps at all.
        If some bitmap has 0 bits, then corresponding sectors are checked in order to be sure that they contain all 0s. If such sectors contain garbage (which is against VHD specs, anyway),
        they are zero-filled. This may take longer to open VHD file for the _first_ time (because of sectors check), but the second opening should be fast.
        When appending a new block in this mode, it is zero-filled and all bits in its bitmap are set. There should be no impact on performance.

    Differencing VHDs opened in write mode:
        When opening a file, it is checked that all present blocks' sector bitmaps have all bits set. It means that there is no need to acceess/cache bitmaps at all
        and there is no need to traverse parent VHDs chain.
        If some bitmap has 0 bits, then corresponding sectors are copied from the parent VHDs. This may take longer to open VHD file for the _first_ time
        (because of sectors check), but the second opening should be fast.
        When appending a new block in this mode, all necessary sectors are copied from the parent VHDs all bits in the bitmap are set.
*/
const uint32_t	VHDF_OPMODE_PURE_BLOCKS = 0x00010000;


/**
    When creating fixed VHD do not zero-fill its contents.
    It will make creating very fast operation, but the VHD contents may contain garbage left in unused file system blocks.
*/
const uint32_t	VHDF_CREATE_FIXED_NO_ZERO_FILL = 0x00100000;


//--------------------------------------------------------------------
/** VHD types, see specs. Only usable and supported types are listed */
typedef enum
{
    EVhd_None    = 0,    ///< 0, invalid value
    EVhd_Fixed   = 2,    ///< 2, fixed disk
    EVhd_Dynamic = 3,    ///< 3, dynamic disk
    EVhd_Diff    = 4     ///< 4, differencing disk
} TVhdType;

//--------------------------------------------------------------------
/** VHD dis geometry in terms of CHS, see VHD specs */
typedef struct
{
    uint32_t Cylinders  : 16;
    uint32_t Heads      : 8;
    uint32_t SecPerTrack: 8;

} T_CHS;



//--------------------------------------------------------------------
/**
    Describes various VHD parameters. Used for creating VHDs and retrieving VHD info.
    While creating VHDs most of the non-required fields can be zeroes, in this case default values will be used

*/
typedef struct
{
    const char* vhdFileName;  ///< VHD fully-qualified file name

    /** a set of VHDF_* flags. VHD creating: on successful creation these flags will be used for opening VHD */
    uint32_t	vhdModeFlags;

    /** VHD type */
    TVhdType    vhdType;

    /** Identifies original creator of the disk. VHD creating: if all zeroes, then "conectix" will be used */
    char        vhdCookie[8];

	/** Log2(VHD sector size). 9 by default.  VHD creating: ignored */
	uint32_t 	secSizeLog2;

	/**
        Log2(sectors per block). VHD creating: if 0 than 12 will be used -> default 2MB block size.
        Not Applicable for Fixed VHD (ignored). Ignored for Diff. VHD, inherited from its parent.
	*/
	uint32_t	secPerBlockLog2;


    /*  VHD creating: only one of these 2 fields must contain meaningful information; Another must be 0 and won't be used.
        These parameters are ignored for diff. VHD, taken from its parent
    */
    T_CHS       vhdDiskGeometry;    ///< VHD geometry in CHS format
    uint32_t	vhdSectors;         ///< VHD volume size in sectors


    /** VHD UUID. VHD creating: if all zeroes, the VHD's UUID will be generated automatically */
    uuid_t      vhdUUID;

    char        vhdCreatorApp[4];   ///< Creator application name.        VHD creating: if [0,0,0,0] then "vpc " will be used
    uint32_t    vhdCreatorVer;      ///< Creator version (major,minor).   VHD creating: if 0, then 0x001000 will be used
    uint32_t    vhdCreatorHostOs;   ///< Creator host OS.                 VHD creating: if 0, then 0x5769326b (Wi2K) will be used

    /** VHD fully-qualified parent file name, VHD creating: used for differencing VHDs only */
    const char* vhdParentName;

} TVHD_ParamsStruct;





#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
/**
	Open VHD file and get its handle.
	@param	fileName 	name of the file to open
	@param	aModeFlags 	set of flags affecting opening the file. @see VHD_OPEN_*

	@return on success: handle value > 0
			on error:	negative error code.
*/
TVhdHandle	VHD_Open(const char *aFileName, uint32_t aModeFlags);

//--------------------------------------------------------------------
/**
	Close VHD.
    Makes the best effort to flush the data/metadata first.
    If flush wasn't successful, closes VHD forcedly, which may result in some data not written to the VHD file.

	@param 	vhdHandle VHD hadle obtained from VHD_Open()
*/
void VHD_Close(TVhdHandle aVhdHandle);

//--------------------------------------------------------------------
/**
	Read a number of sectors from VHD.
	@param 	aVhdHandle      VHD hadle obtained from VHD_Open()
	@param	aStartSector	starting sector.
	@param	aSectors		number of sectors to read 0..LONG_MAX
	@param	apBuffer		out: read data
    @param  aBufSize        buffer size in bytes

	@return	positive number of read sectors on success, negative error code otherwise.
*/
int VHD_ReadSectors(TVhdHandle aVhdHandle, uint32_t aStartSector, int aSectors, void* apBuffer, uint32_t aBufSize);


//--------------------------------------------------------------------
/**
	Write a number of sectors to VHD.
	@param 	vhdHandle 	    VHD hadle obtained from VHD_Open()
	@param	aStartSector	starting sector.
	@param	aSectors		number of sectors to write 0..LONG_MAX
	@param	apBuffer		in: data to write
    @param  aBufSize        buffer size in bytes

	@return	positive number of written sectors on success, negative  error code otherwise.
*/
int VHD_WriteSectors(TVhdHandle aVhdHandle, uint32_t aStartSector, int aSectors, const void* apBuffer, uint32_t aBufSize);



//--------------------------------------------------------------------
/**
	TRIM or "Discard sectors" API.
    Calling this API marks an extent of sectors as "discarded" or not containing useful information (opposite to VHD_WriteSectors() API).
    This feature can be used for efficient VHD compacting, because VHD blocks that contain all "discarded" sectors can be taken out by compacting tool.
	In order to use this API VHD file must be opened with VHDF_OPEN_ENABLE_TRIM flag.

	@param 	vhdHandle 	    VHD hadle obtained from VHD_Open()
	@param	aStartSector	starting sector.
	@param	aSectors		number of sectors to discard 0..LONG_MAX

	@return	0 on success, negative error code otherwise.
*/
int VHD_DiscardSectors(TVhdHandle aVhdHandle, uint32_t aStartSector, int aSectors);





//--------------------------------------------------------------------
/**
	Flush data and metadata.
	@param 	aVhdHandle      VHD hadle obtained from VHD_Open()
	@return 0 on success, negative error code otherwise.
*/
int VHD_Flush(TVhdHandle aVhdHandle);


//--------------------------------------------------------------------
/**
    Invalidates cached data and metadata. Thus, on the next access internal caches will be re-populated.
    @pre All metadata should be clean before an attempt to invalidate caches. @see Flush(). An attempt to invalidate caches will cause panic.

	@param 	aVhdHandle      VHD hadle obtained from VHD_Open()
	@return 0 on success, negative error code otherwise.
*/
int VHD_InvalidateCaches(TVhdHandle aVhdHandle);


//--------------------------------------------------------------------
/**
	Prints out some information about VHD in a human-readable form.
	@param 	vhdHandle VHD hadle obtained from VHD_Open()
	@param  apBuf     user's buffer where information will be dumped into.
	@param  aBufSize  size of the buffer


    @return	KErrNone on success
            KErrTooBig if the buffer is too small and not all text data have been copied into it
            Standard error code otherwise.
*/
int VHD_PrintInfo(TVhdHandle aVhdHandle, void* apBuf, uint32_t aBufSize);


//--------------------------------------------------------------------
/**
	Get VHD parametes
	@param 	vhdHandle       VHD hadle obtained from VHD_Open()
	@param	pVhdInfo        out: filled structure describing VHD parameters
	@return	0 on success,   negative value corresponding system error code otherwise.

    Note: pointers "vhdFileName", "vhdParentName" in the pVhdInfo structure point to the valid strings only
    until the next API call. Thus, it is API user's responsibility to copy corresponting strings to its internal buffer if needed.
*/
int VHD_Info(TVhdHandle aVhdHandle, TVHD_ParamsStruct* pVhdInfo);


//--------------------------------------------------------------------
/**
	Get parametes of the parent VHD file.
	Applicable mostly for the Differencing VHDs.
    It is possible to traverse differencing VHD chain with the help of this function. Just call it incrementing aParentNumber
    until get KErrNotFound.


	@param 	vhdHandle       VHD hadle obtained from VHD_Open()
	@param	pVhdInfo        out: filled structure describing VHD parameters
	@param  aParentIndex    Index of the parent VHD file. 0 refers to _this_ file and it is equivalent to calling VHD_Info()
	                        1 means "first parent", 2 - "parent of the first parent" etc.

	@return	KErrNone        on success,
            KErrNotFound    if there is no parent number aParentNumber
            negative value corresponding system error code otherwise.

    Note: pointers "vhdFileName", "vhdParentName" in the pVhdInfo structure point to the valid strings only
    until the next API call. Thus, it is API user's responsibility to copy corresponting strings to its internal buffer if needed.
*/
int VHD_ParentInfo(TVhdHandle aVhdHandle, TVHD_ParamsStruct* pVhdInfo, uint32_t aParentIndex);



//--------------------------------------------------------------------
/**
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
TVhdHandle	VHD_Create(const TVHD_ParamsStruct* apParams);


//--------------------------------------------------------------------
/**
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

	@return	KErrNone        on success, negative value corresponding system error code otherwise.


*/
int VHD_CoalesceChain(TVhdHandle aVhdHandle, uint32_t aChainIdxStart, uint32_t aChainIdxResult);





//--------------------------------------------------------------------

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif





#endif /* __LIBVHD2_H__ */

