// ==========================================================
// Multi-Page functions
//
// Design and implementation by
// - Floris van den Berg (flvdberg@wxs.nl)
// - Laurent Rocher (rocherl@club-internet.fr)
// - Steve Johnson (steve@parisgroup.net)
// - Petr Pytelka (pyta@lightcomp.com)
// - Hervé Drolon (drolon@infonie.fr)
// - Vadim Alexandrov (vadimalexandrov@users.sourceforge.net
// - Martin Dyring-Andersen (mda@spamfighter.com)
// - Volodymyr Goncharov (volodymyr.goncharov@gmail.com)
// - Mihail Naydenov (mnaydenov@users.sourceforge.net)
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

#ifdef _MSC_VER 
#pragma warning (disable : 4786) // identifier was truncated to 'number' characters
#endif

#include "CacheFile.h"
#include "FreeImageIO.h"
#include "Plugin.h"
#include "Utilities.h"
#include "FreeImage.h"

namespace {

// ----------------------------------------------------------

enum BlockType { BLOCK_CONTINUEUS, BLOCK_REFERENCE };

// ----------------------------------------------------------

class PageBlock {

  union {
    struct {
      int  m_start;
      int  m_end;
    };
    struct {
      int  m_reference;
      int  m_size;
    };
  };

public:
  BlockType m_type;

  PageBlock(BlockType type = BLOCK_CONTINUEUS, int val1 = -1, int val2 = -1) : m_type(type)
  {
    if(m_type == BLOCK_CONTINUEUS)
    {
      m_start = val1;
      m_end = val2;
    }
    else
    {
      m_reference = val1;
      m_size = val2;
    }
  }

  bool isValid() const { return !(m_type == BLOCK_CONTINUEUS && m_start == -1 && m_end == -1); }
  /*explicit*/ operator bool() const { return isValid(); }

  int getStart() const { assert(isValid() && m_type == BLOCK_CONTINUEUS); return m_start; }
  int getEnd() const { assert(isValid() && m_type == BLOCK_CONTINUEUS); return m_end; }

  bool isSinglePage() const { assert(isValid()); return m_type == BLOCK_CONTINUEUS ? (m_start == m_end) : true; }
  int getPageCount() const { assert(isValid()); return m_type == BLOCK_CONTINUEUS ? (m_end - m_start + 1) : 1;}

  int getReference() const { assert(isValid() && m_type == BLOCK_REFERENCE); return m_reference; }
  int getSize() const { assert(isValid() && m_type == BLOCK_REFERENCE); return m_size;  }
};

// ----------------------------------------------------------

typedef std::list<PageBlock> BlockList;
typedef BlockList::iterator BlockListIterator;

// ----------------------------------------------------------

struct MULTIBITMAPHEADER {
	
	MULTIBITMAPHEADER()
		: node(NULL)
		, fif(FIF_UNKNOWN)
		, handle(NULL)
		, changed(FALSE)
		, page_count(0)
		, read_only(TRUE)
		, cache_fif(fif)
		, load_flags(0)
	{
		SetDefaultIO(&io);
	}
	
	PluginNode *node;
	FREE_IMAGE_FORMAT fif;
	FreeImageIO io;
	fi_handle handle;
	CacheFile m_cachefile;
	std::map<FIBITMAP *, int> locked_pages;
	BOOL changed;
	int page_count;
	BlockList m_blocks;
	std::string m_filename;
	BOOL read_only;
	FREE_IMAGE_FORMAT cache_fif;
	int load_flags;
};

// =====================================================================
// Helper functions
// =====================================================================

inline void
ReplaceExtension(std::string& dst_filename, const std::string& src_filename, const std::string& dst_extension) {
	size_t lastDot = src_filename.find_last_of('.');
	if (lastDot == std::string::npos) {
		dst_filename = src_filename;
		dst_filename += ".";
		dst_filename += dst_extension;
	}
	else {
		dst_filename = src_filename.substr(0, lastDot + 1);
		dst_filename += dst_extension;
	}
}

} //< ns


// =====================================================================
// Internal Multipage functions
// =====================================================================

inline MULTIBITMAPHEADER *
FreeImage_GetMultiBitmapHeader(FIMULTIBITMAP *bitmap) {
	return (MULTIBITMAPHEADER *)bitmap->data;
}

static BlockListIterator DLL_CALLCONV
FreeImage_FindBlock(FIMULTIBITMAP *bitmap, int position) {
	assert(NULL != bitmap);

	MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

	// step 1: find the block that matches the given position

	int prev_count = 0;
	int count = 0;
	BlockListIterator i;

	for (i = header->m_blocks.begin(); i != header->m_blocks.end(); ++i) {
		prev_count = count;
		
		count += i->getPageCount();

		if (count > position) {
			break;
		}
	}

	// step 2: make sure we found the node. from here it gets a little complicated:
	// * if the block is single page, just return it
	// * if the block is a span of pages, split it in 3 new blocks
	//   and return the middle block, which is now a single page
	
	if ((i != header->m_blocks.end()) && (count > position)) {
		
		if (i->isSinglePage()) {
			return i;
		}
		
		const int item = i->getStart() + (position - prev_count);
		
		// left part
		
		if (item != i->getStart()) {
			header->m_blocks.insert(i, PageBlock(BLOCK_CONTINUEUS, i->getStart(), item - 1));
		}
		
		// middle part
		
		BlockListIterator block_target = header->m_blocks.insert(i, PageBlock(BLOCK_CONTINUEUS, item, item));
		
		// right part
		
		if (item != i->getEnd()) {
			header->m_blocks.insert(i, PageBlock(BLOCK_CONTINUEUS, item + 1, i->getEnd()));
		}
		
		// remove the old block that was just splitted
		
		header->m_blocks.erase(i);
		
		// return the splitted block
		
		return block_target;
	}
	
	// we should never go here ...
	assert(false);
	return header->m_blocks.end();
}

int DLL_CALLCONV
FreeImage_InternalGetPageCount(FIMULTIBITMAP *bitmap) {	
	if (bitmap) {
		if (((MULTIBITMAPHEADER *)bitmap->data)->handle) {
			MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);
			
			header->io.seek_proc(header->handle, 0, SEEK_SET);
			
			void *data = FreeImage_Open(header->node, &header->io, header->handle, TRUE);
			
			int page_count = (header->node->m_plugin->pagecount_proc != NULL) ? header->node->m_plugin->pagecount_proc(&header->io, header->handle, data) : 1;
			
			FreeImage_Close(header->node, &header->io, header->handle, data);
			
			return page_count;
		}
	}
	
	return 0;
}

// =====================================================================
// Multipage functions
// =====================================================================

FIMULTIBITMAP * DLL_CALLCONV
FreeImage_OpenMultiBitmap(FREE_IMAGE_FORMAT fif, const char *filename, BOOL create_new, BOOL read_only, BOOL keep_cache_in_memory, int flags) {

	FILE *handle = NULL;
	try {
		// sanity check on the parameters

		if (create_new) {
			read_only = FALSE;
		}

		// retrieve the plugin list to find the node belonging to this plugin

		PluginList *list = FreeImage_GetPluginList();

		if (list) {
			PluginNode *node = list->FindNodeFromFIF(fif);

			if (node) {
				if (!create_new) {
					handle = fopen(filename, "rb");
					if (handle == NULL) {
						return NULL;
					}
				}

				std::auto_ptr<FIMULTIBITMAP> bitmap (new FIMULTIBITMAP);
				std::auto_ptr<MULTIBITMAPHEADER> header (new MULTIBITMAPHEADER);
				header->m_filename = filename;
				// io is default
				header->node = node;
				header->fif = fif;
				header->handle = handle;						
				header->read_only = read_only;
				header->cache_fif = fif;
				header->load_flags = flags;

				// store the MULTIBITMAPHEADER in the surrounding FIMULTIBITMAP structure

				bitmap->data = header.get();

				// cache the page count

				header->page_count = FreeImage_InternalGetPageCount(bitmap.get());

				// allocate a continueus block to describe the bitmap

				if (!create_new) {
					header->m_blocks.push_back(PageBlock(BLOCK_CONTINUEUS, 0, header->page_count - 1));
				}

				// set up the cache

				if (!read_only) {
					std::string cache_name;
					ReplaceExtension(cache_name, filename, "ficache");
					
					if (!header->m_cachefile.open(cache_name, keep_cache_in_memory)) {
						// an error occured ...
						fclose(handle);
						return NULL;
					}
				}
				// return the multibitmap
				// std::bad_alloc won't be thrown from here on
				header.release(); // now owned by bitmap
				return bitmap.release(); // now owned by caller
			}
		}
	} catch (std::bad_alloc &) {
		/** @todo report error */
	}
	if (handle) {
		fclose(handle);
	}
	return NULL;
}

FIMULTIBITMAP * DLL_CALLCONV
FreeImage_OpenMultiBitmapFromHandle(FREE_IMAGE_FORMAT fif, FreeImageIO *io, fi_handle handle, int flags) {
	try {
		BOOL read_only = FALSE;	// modifications (if any) will be stored into the memory cache

		if (io && handle) {
		
			// retrieve the plugin list to find the node belonging to this plugin
			PluginList *list = FreeImage_GetPluginList();
		
			if (list) {
				PluginNode *node = list->FindNodeFromFIF(fif);
			
				if (node) {
					std::auto_ptr<FIMULTIBITMAP> bitmap (new FIMULTIBITMAP);
					std::auto_ptr<MULTIBITMAPHEADER> header (new MULTIBITMAPHEADER);
					header->io = *io;
					header->node = node;
					header->fif = fif;
					header->handle = handle;						
					header->read_only = read_only;	
					header->cache_fif = fif;
					header->load_flags = flags;
							
					// store the MULTIBITMAPHEADER in the surrounding FIMULTIBITMAP structure

					bitmap->data = header.get();

					// cache the page count

					header->page_count = FreeImage_InternalGetPageCount(bitmap.get());

					// allocate a continueus block to describe the bitmap
					
					header->m_blocks.push_back(PageBlock(BLOCK_CONTINUEUS, 0, header->page_count - 1));
					
					// no need to open cache - it is in-memory by default

					header.release();
					return bitmap.release();
				}
			}
		}
	} catch (std::bad_alloc &) {
		/** @todo report error */
	}
	return NULL;
}

BOOL DLL_CALLCONV
FreeImage_SaveMultiBitmapToHandle(FREE_IMAGE_FORMAT fif, FIMULTIBITMAP *bitmap, FreeImageIO *io, fi_handle handle, int flags) {
	if(!bitmap || !bitmap->data || !io || !handle) {
		return FALSE;
	}

	BOOL success = TRUE;

	// retrieve the plugin list to find the node belonging to this plugin
	PluginList *list = FreeImage_GetPluginList();
	
	if (list) {
		PluginNode *node = list->FindNodeFromFIF(fif);

		if(node) {
			MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);
			
			// dst data
			void *data = FreeImage_Open(node, io, handle, FALSE);
			// src data
			void *data_read = NULL;
			
			if(header->handle) {
				// open src
				header->io.seek_proc(header->handle, 0, SEEK_SET);
				data_read = FreeImage_Open(header->node, &header->io, header->handle, TRUE);
			}
			
			// write all the pages to the file using handle and io
			
			int count = 0;
			
			for (BlockListIterator i = header->m_blocks.begin(); i != header->m_blocks.end(); i++) {
				if (success) {
					switch(i->m_type) {
						case BLOCK_CONTINUEUS:
						{
							for (int j = i->getStart(); j <= i->getEnd(); j++) {
								
								// load the original source data
								FIBITMAP *dib = header->node->m_plugin->load_proc(&header->io, header->handle, j, header->load_flags, data_read);
								
								// save the data
								success = node->m_plugin->save_proc(io, dib, handle, count, flags, data);
								count++;
								
								FreeImage_Unload(dib);
							}
							
							break;
						}
						
						case BLOCK_REFERENCE:
						{
							// read the compressed data
							
							BYTE *compressed_data = (BYTE*)malloc(i->getSize() * sizeof(BYTE));
							
							header->m_cachefile.readFile((BYTE *)compressed_data, i->getReference(), i->getSize());
							
							// uncompress the data
							
							FIMEMORY *hmem = FreeImage_OpenMemory(compressed_data, i->getSize());
							FIBITMAP *dib = FreeImage_LoadFromMemory(header->cache_fif, hmem, 0);
							FreeImage_CloseMemory(hmem);
							
							// get rid of the buffer
							free(compressed_data);
							
							// save the data
							
							success = node->m_plugin->save_proc(io, dib, handle, count, flags, data);
							count++;
							
							// unload the dib

							FreeImage_Unload(dib);

							break;
						}
					}
				} else {
					break;
				}
			}
			
			// close the files
			
			FreeImage_Close(header->node, &header->io, header->handle, data_read);

			FreeImage_Close(node, io, handle, data); 
			
			return success;
		}
	}

	return FALSE;
}


BOOL DLL_CALLCONV
FreeImage_CloseMultiBitmap(FIMULTIBITMAP *bitmap, int flags) {
	if (bitmap) {
		BOOL success = TRUE;
		
		if (bitmap->data) {
			MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);			
			
			// saves changes only of images loaded directly from a file
			if (header->changed && !header->m_filename.empty()) {
				try {
					// open a temp file

					std::string spool_name;

					ReplaceExtension(spool_name, header->m_filename, "fispool");

					// open the spool file and the source file
        
					FILE *f = fopen(spool_name.c_str(), "w+b");
				
					// saves changes
					if (f == NULL) {
						FreeImage_OutputMessageProc(header->fif, "Failed to open %s, %s", spool_name.c_str(), strerror(errno));
						success = FALSE;
					} else {
						success = FreeImage_SaveMultiBitmapToHandle(header->fif, bitmap, &header->io, (fi_handle)f, flags);

						// close the files

						if (fclose(f) != 0) {
							success = FALSE;
							FreeImage_OutputMessageProc(header->fif, "Failed to close %s, %s", spool_name.c_str(), strerror(errno));
						}
					}
					if (header->handle) {
						fclose((FILE *)header->handle);
					}
				
					// applies changes to the destination file

					if (success) {
						remove(header->m_filename.c_str());
						success = (rename(spool_name.c_str(), header->m_filename.c_str()) == 0) ? TRUE:FALSE;
						if(!success) {
							FreeImage_OutputMessageProc(header->fif, "Failed to rename %s to %s", spool_name.c_str(), header->m_filename.c_str());
						}
					} else {
						remove(spool_name.c_str());
					}
				} catch (std::bad_alloc &) {
					success = FALSE;
				}

			} else {
				if (header->handle && !header->m_filename.empty()) {
					fclose((FILE *)header->handle);
				}
			}

			// delete the last open bitmaps

			while (!header->locked_pages.empty()) {
				FreeImage_Unload(header->locked_pages.begin()->first);

				header->locked_pages.erase(header->locked_pages.begin()->first);
			}

			// delete the FIMULTIBITMAPHEADER

			delete header;
		}

		delete bitmap;

		return success;
	}

	return FALSE;
}

int DLL_CALLCONV
FreeImage_GetPageCount(FIMULTIBITMAP *bitmap) {
	if (bitmap) {
		MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

		if (header->page_count == -1) {
			header->page_count = 0;

			for (BlockListIterator i = header->m_blocks.begin(); i != header->m_blocks.end(); ++i) {
				header->page_count += i->getPageCount();
			}
		}

		return header->page_count;
	}

	return 0;
}

static PageBlock
FreeImage_SavePageToBlock(MULTIBITMAPHEADER *header, FIBITMAP *data) {
	PageBlock res;
	
	if (header->read_only || !header->locked_pages.empty()) {
		return res;
	}

	DWORD compressed_size = 0;
	BYTE *compressed_data = NULL;

	// compress the bitmap data

	// open a memory handle
	FIMEMORY *hmem = FreeImage_OpenMemory();
	if(hmem==NULL) {
		return res;
	}
	// save the file to memory
	if(!FreeImage_SaveToMemory(header->cache_fif, data, hmem, 0)) {
		FreeImage_CloseMemory(hmem);
		return res;
	}
	// get the buffer from the memory stream
	if(!FreeImage_AcquireMemory(hmem, &compressed_data, &compressed_size)) {
		FreeImage_CloseMemory(hmem);
		return res;
	}
	
	// write the compressed data to the cache
	int ref = header->m_cachefile.writeFile(compressed_data, compressed_size);
	// get rid of the compressed data
	FreeImage_CloseMemory(hmem);
	
	res = PageBlock(BLOCK_REFERENCE, ref, compressed_size);
	
	return res;
}

void DLL_CALLCONV
FreeImage_AppendPage(FIMULTIBITMAP *bitmap, FIBITMAP *data) {
	if (!bitmap || !data) {
		return;
	}
	
	MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);
	
	if(const PageBlock block = FreeImage_SavePageToBlock(header, data)) {
		// add the block
		header->m_blocks.push_back(block);
		header->changed = TRUE;
		header->page_count = -1;
	}
}

void DLL_CALLCONV
FreeImage_InsertPage(FIMULTIBITMAP *bitmap, int page, FIBITMAP *data) {
	if (!bitmap || !data) {
		return;
	}
	if (page >= FreeImage_GetPageCount(bitmap)) {
		return;
	}
	
	MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);
	
	if(const PageBlock block = FreeImage_SavePageToBlock(header, data)) {
		// add a block
		if (page > 0) {
			BlockListIterator block_source = FreeImage_FindBlock(bitmap, page);
			header->m_blocks.insert(block_source, block);
		} else {
			header->m_blocks.push_front(block);
		}
		
		header->changed = TRUE;
		header->page_count = -1;
	}
}

void DLL_CALLCONV
FreeImage_DeletePage(FIMULTIBITMAP *bitmap, int page) {
	if (bitmap) {
		MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

		if ((!header->read_only) && (header->locked_pages.empty())) {
			if (FreeImage_GetPageCount(bitmap) > 1) {
				BlockListIterator i = FreeImage_FindBlock(bitmap, page);
				
				if (i != header->m_blocks.end()) {
					switch(i->m_type) {
						case BLOCK_CONTINUEUS :
							header->m_blocks.erase(i);
							break;
							
						case BLOCK_REFERENCE :
							header->m_cachefile.deleteFile(i->getReference());
							header->m_blocks.erase(i);
							break;
					}
					
					header->changed = TRUE;
					header->page_count = -1;
				}
			}
		}
	}
}

FIBITMAP * DLL_CALLCONV
FreeImage_LockPage(FIMULTIBITMAP *bitmap, int page) {
	if (bitmap) {
		MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

		// only lock if the page wasn't locked before...

		for (std::map<FIBITMAP *, int>::iterator i = header->locked_pages.begin(); i != header->locked_pages.end(); ++i) {
			if (i->second == page) {
				return NULL;
			}
		}

		// open the bitmap
		
		header->io.seek_proc(header->handle, 0, SEEK_SET);
		
		void *data = FreeImage_Open(header->node, &header->io, header->handle, TRUE);
		
		// load the bitmap data
		
		if (data != NULL) {
			FIBITMAP *dib = (header->node->m_plugin->load_proc != NULL) ? header->node->m_plugin->load_proc(&header->io, header->handle, page, header->load_flags, data) : NULL;

			// close the file
			
			FreeImage_Close(header->node, &header->io, header->handle, data);

			// if there was still another bitmap open, get rid of it

			if (dib) {
				header->locked_pages[dib] = page;

				return dib;
			}	

			return NULL;
		}
	}

	return NULL;
}

void DLL_CALLCONV
FreeImage_UnlockPage(FIMULTIBITMAP *bitmap, FIBITMAP *page, BOOL changed) {
	if ((bitmap) && (page)) {
		MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

		// find out if the page we try to unlock is actually locked...

		if (header->locked_pages.find(page) != header->locked_pages.end()) {
			// store the bitmap compressed in the cache for later writing
			
			if (changed && !header->read_only) {
				header->changed = TRUE;

				// cut loose the block from the rest

				BlockListIterator i = FreeImage_FindBlock(bitmap, header->locked_pages[page]);

				// compress the data

				DWORD compressed_size = 0;
				BYTE *compressed_data = NULL;

				// open a memory handle
				FIMEMORY *hmem = FreeImage_OpenMemory();
				// save the page to memory
				FreeImage_SaveToMemory(header->cache_fif, page, hmem, 0);
				// get the buffer from the memory stream
				FreeImage_AcquireMemory(hmem, &compressed_data, &compressed_size);

				// write the data to the cache
				
				if (i->m_type == BLOCK_REFERENCE) {
					header->m_cachefile.deleteFile(i->getReference());
				}
				
				int iPage = header->m_cachefile.writeFile(compressed_data, compressed_size);
				
				*i = PageBlock(BLOCK_REFERENCE, iPage, compressed_size);
				
				// get rid of the compressed data

				FreeImage_CloseMemory(hmem);
			}

			// reset the locked page so that another page can be locked

			FreeImage_Unload(page);

			header->locked_pages.erase(page);
		}
	}
}

BOOL DLL_CALLCONV
FreeImage_MovePage(FIMULTIBITMAP *bitmap, int target, int source) {
	if (bitmap) {
		MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

		if ((!header->read_only) && (header->locked_pages.empty())) {
			if ((target != source) && ((target >= 0) && (target < FreeImage_GetPageCount(bitmap))) && ((source >= 0) && (source < FreeImage_GetPageCount(bitmap)))) {
				BlockListIterator block_source = FreeImage_FindBlock(bitmap, target);
				BlockListIterator block_target = FreeImage_FindBlock(bitmap, source);

				header->m_blocks.insert(block_target, *block_source);			
				header->m_blocks.erase(block_source);
				
				header->changed = TRUE;
				
				return TRUE;
			}
		}
	}

	return FALSE;
}

BOOL DLL_CALLCONV
FreeImage_GetLockedPageNumbers(FIMULTIBITMAP *bitmap, int *pages, int *count) {
	if ((bitmap) && (count)) {
		MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

		if ((pages == NULL) || (*count == 0)) {
			*count = (int)header->locked_pages.size();
		} else {
			int c = 0;

			for (std::map<FIBITMAP *, int>::iterator i = header->locked_pages.begin(); i != header->locked_pages.end(); ++i) {
				pages[c] = i->second;

				c++;

				if (c == *count) {
					break;
				}
			}
		}

		return TRUE;
	}

	return FALSE;
}

// =====================================================================
// Memory IO Multipage functions
// =====================================================================

FIMULTIBITMAP * DLL_CALLCONV
FreeImage_LoadMultiBitmapFromMemory(FREE_IMAGE_FORMAT fif, FIMEMORY *stream, int flags) {
	BOOL read_only = FALSE;	// modifications (if any) will be stored into the memory cache

	// retrieve the plugin list to find the node belonging to this plugin

	PluginList *list = FreeImage_GetPluginList();

	if (list) {
		PluginNode *node = list->FindNodeFromFIF(fif);

		if (node) {

				FIMULTIBITMAP *bitmap = new(std::nothrow) FIMULTIBITMAP;

				if (bitmap) {
					MULTIBITMAPHEADER *header = new(std::nothrow) MULTIBITMAPHEADER;

					if (header) {
						header->node = node;
						header->fif = fif;
						SetMemoryIO(&header->io);
						header->handle = (fi_handle)stream;						
						header->read_only = read_only;
						header->cache_fif = fif;
						header->load_flags = flags;

						// store the MULTIBITMAPHEADER in the surrounding FIMULTIBITMAP structure

						bitmap->data = header;

						// cache the page count

						header->page_count = FreeImage_InternalGetPageCount(bitmap);

						// allocate a continueus block to describe the bitmap
						
						header->m_blocks.push_back(PageBlock(BLOCK_CONTINUEUS, 0, header->page_count - 1));
						
						// no need to open cache - it is in-memory by default

						return bitmap;
					}
					
					delete bitmap;
				}

		}
	}

	return NULL;
}

BOOL DLL_CALLCONV
FreeImage_SaveMultiBitmapToMemory(FREE_IMAGE_FORMAT fif, FIMULTIBITMAP *bitmap, FIMEMORY *stream, int flags) {
	if (stream && stream->data) {
		FreeImageIO io;
		SetMemoryIO(&io);

		return FreeImage_SaveMultiBitmapToHandle(fif, bitmap, &io, (fi_handle)stream, flags);
	}

	return FALSE;
}
