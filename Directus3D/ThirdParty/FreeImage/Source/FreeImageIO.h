// ==========================================================
// Input/Output functions
//
// Design and implementation by
// - Floris van den Berg (flvdberg@wxs.nl)
//
// This file is part of FreeImage 3
//
// COVERED CODE IS PROVIDED UNDER THIS LICENSE ON AN "AS IS" BASIS, WITHOUT WARRANTY
// OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, WITHOUT LIMITATION, WARRANTIES
// THAT THE COVERED CODE IS FREE OF DEFECTS, MERCHANTABLE, FIT FOR A PARTICULAR PURPOSE
// OR NON-INFRINGING. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE COVERED
// CODE IS WITH YOU. SHOULD ANY COVERED CODE PROVE DEFECTIVE IN ANY RESPECT, YOU (NOT
// THE INITIAL DEVELOPER OR ANY OTHER CONTRIBUTOR) ASSUME THE COST OF ANY NECESSARY
// SERVICING, REPAIR OR CORRECTION. THIS DISCLAIMER OF WARRANTY CONSTITUTES AN ESSENTIAL
// PART OF THIS LICENSE. NO USE OF ANY COVERED CODE IS AUTHORIZED HEREUNDER EXCEPT UNDER
// THIS DISCLAIMER.
//
// Use at your own risk!
// ==========================================================

#ifndef FREEIMAGEIO_H
#define FREEIMAGEIO_H

#ifndef FREEIMAGE_H
#include "FreeImage.h"
#endif

// ----------------------------------------------------------

FI_STRUCT (FIMEMORYHEADER) {
	/**
	Flag used to remember to delete the 'data' buffer.
	When the buffer is a wrapped buffer, it is read-only, no need to delete it. 
	When the buffer is a read/write buffer, it is allocated dynamically and must be deleted when no longer needed.
	*/
	BOOL delete_me;
	/**
	file_length is equal to the input buffer size when the buffer is a wrapped buffer, i.e. file_length == data_length. 
	file_length is the amount of the written bytes when the buffer is a read/write buffer.
	*/
	long file_length;
	/**
	When using read-only input buffers, data_length is equal to the input buffer size, i.e. the file_length.
	When using read/write buffers, data_length is the size of the allocated buffer, 
	whose size is greater than or equal to file_length.
	*/
	long data_length;
	/**
	start buffer address
	*/
	void *data;
	/**
	Current position into the memory stream
	*/
	long current_position;
};

void SetDefaultIO(FreeImageIO *io);

void SetMemoryIO(FreeImageIO *io);

#endif // !FREEIMAGEIO_H
