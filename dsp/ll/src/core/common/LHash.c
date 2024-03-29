/* ============================================================================
 *  LHash.c
 * ============================================================================

 *  Author:         (C) 2002 Stefano Emiliozzi
 *  License:        See the end of this file for license information
 *  Created:        December 28, 2002
 *  Module:         LL

 *  Last changed:   $Date: 2010/04/23 16:05:53 $  
 *  Changed by:     $Author: demetres $
 *  Revision:       $Revision: 1.1 $
*/

#include "LHash.h"
#include "LArray.h"
#include "LException.h"
#include "LMemory.h"
#include "LRandSource.h"
#include <time.h>

/* MEMBER VARIABLES */

struct TCLSlot
{/* this is an element of the collision list dynarray */
	ui4		mKey;
	void* 	mItemRef; /* index of the element in the LArray */
};

struct TSlot
{/* this is a row of the hash table */
	Bool	mUsed;
	ui4		mKey;
	void* 	mItemRef;
	LArray* mCollisionList;
};

/* Initially, the table is build for 2^2=4 entries */
#define BASE_HASH_LENGTH		6

/* Esplicitly defines a base dimension */
#define BASE_ENTRIES_COUNT	   64

/* Collision keys threeshold  at 75% */
#define COLLISION_KT     0.75 

/* Low usage threeshold at 10% */
#define LOW_USAGE_T		 0.1

/* Defines constants used by the _Expand method */
#define EXPAND 1

#define SHRINK -1

/* Hash Function */
#define Hash_(a,x,l) ((a * x) >> (32 - l))

/* Definition of private methods */
void _Resize(LHash* This, i1 inFactor);
void _InsertItem (LHash* This, struct TSlot* inData, void* inItem, ui4 inKey);

/* PUBLIC METHODS */

/* ---------------------------------------------------------------------------------
*  LHash_New
*  ---------------------------------------------------------------------------------
*  Constructor */

LHash* LHash_New() 
{
	ui4 i;
	LHash theObject = {0};
	ui4 theEntriesCount = BASE_ENTRIES_COUNT;
	ui4 theSizeOf = sizeof(struct TSlot);
	ui4 theSeed = (ui4)time(NULL);
	LRandSource* theRand = NULL;

	Try
	{
		theRand					 = LRandSource_New( theSeed );
		theObject.mData          = (struct TSlot*) LMemory_Malloc(theEntriesCount*theSizeOf);
		theObject.mEntriesCount	 = theEntriesCount;
		theObject.mDataSize      = ( theEntriesCount * theSizeOf ) + sizeof(LHash);
		theObject.mItemsCount    = 0;
		theObject.mHashLength    = BASE_HASH_LENGTH;
		theObject.mRandomOdd     = ( 2 * LRandSource_GetRandUI4(theRand, 0, LType_MAX_UI4)) - 1;  
		theObject.mDebug         = FALSE;
		LRandSource_Delete(&theRand);
		theObject.mCollisionKeys = 0;

		/* Initialization loop for the table */
		for (i=0; i < theEntriesCount; i++)
		{
			theObject.mData[i].mUsed          = FALSE;
			theObject.mData[i].mCollisionList = NULL;
		}
	}
	CatchAny
	{/* if something goes wrong, cleans the memory */
		if (theRand != NULL) LRandSource_Delete(&theRand);
		if (theObject.mData != NULL) LMemory_Free( &(theObject.mData) );
		Rethrow;
	}
    return LMemory_NewObject(LHash, theObject);
}



/* ---------------------------------------------------------------------------------
*  LHash_Delete
*  ---------------------------------------------------------------------------------
*  Destructor */

void LHash_Delete(LHash** ThisA)
{
	ui4 i;
	ui4 theEntriesCount;

	if ( ThisA == NULL)     Throw(LHash_OBJECT_NULL_POINTER);
	if ( (*ThisA) == NULL )	Throw(LHash_OBJECT_NULL_POINTER);

	theEntriesCount = (*ThisA)->mEntriesCount;
	for (i=0; i < theEntriesCount; i++)
		if ( (*ThisA)->mData[i].mCollisionList != NULL )
			LArray_Delete(&(*ThisA)->mData[i].mCollisionList);

    LMemory_Free(&(*ThisA)->mData);
    LMemory_DeleteObject(ThisA);
}


 /* ---------------------------------------------------------------------------------
 *  InsertItem
 *  ---------------------------------------------------------------------------------
 *  Inserts item inItem at entry obtained by hashing inKey */

 void LHash_InsertItem (LHash* This, void* inItem, ui4 inKey)
 {
	 struct TCLSlot theItem; 
	 ui4 theIndex;
	 f4  theUsage; 
	 ui4 theHash;
	 
	 if (This == NULL) Throw(LHash_OBJECT_NULL_POINTER);

	 theHash = Hash_(This->mRandomOdd, inKey, This->mHashLength);
	 /* if the item is already in the table, first removes it */
	 if (LHash_IsInTable(This, inKey)) 
	 	 LHash_RemoveItem(This, inKey);
	 This->mItemsCount++;
	 if (This->mData[theHash].mUsed == FALSE)
	 {/* entry is unused */
		 This->mData[theHash].mUsed    = TRUE;
         This->mData[theHash].mKey     = inKey;
		 This->mData[theHash].mItemRef = inItem;
	 }
	 else
	 {/* collision on entry 'theHash' occurs */
		 if (This->mData[theHash].mCollisionList == NULL)
		 {/* first item with a collision */
			This->mCollisionKeys++;
			This->mData[theHash].mCollisionList = LArray_New((ui4)sizeof(struct TCLSlot));
		 }
		 else
		 {/* if there was already a collision list, in order to update */
          /* correctly the This->mDataSize field, we  have to subtract */
		  /* the previous dimension of the LArray					   */
			This->mDataSize -= LArray_GetUsedMem(This->mData[theHash].mCollisionList);
		 }
		 
		 theItem.mItemRef = inItem;
		 theItem.mKey	  = inKey;
		 
		 /* then appends the new item */
		 theIndex = LArray_AppendItem(This->mData[theHash].mCollisionList, &theItem);
		 /* and finally updates DataSize value */
		 This->mDataSize += LArray_GetUsedMem(This->mData[theHash].mCollisionList);
		 
	 }

	 /* checks if the table is over used, eventually expands it */
	 theUsage = (f4)This->mCollisionKeys / (f4)This->mEntriesCount ;
	 if ( theUsage > COLLISION_KT)
	 {/* if the % of collision keys is higher than 50%, expand the table */
	 	 _Resize(This, EXPAND);
	 }
	 
 }


 /*---------------------------------------------------------------------------------
 *  IsInTable
 * ---------------------------------------------------------------------------------
 * Returns TRUE <=> item with key inKey is in the table */
 Bool LHash_IsInTable (LHash* This, ui4 inKey)
 {
	ui4 i;
	ui4 theItemsCount;
	struct TCLSlot theItem;
	ui4 theHash;
	
	if (This == NULL) Throw(LHash_OBJECT_NULL_POINTER);

	theHash = Hash_(This->mRandomOdd, inKey, This->mHashLength);
	if (This->mData[theHash].mUsed == FALSE) 
		return FALSE;
	if (This->mData[theHash].mKey == inKey)
		return TRUE;
	if (This->mData[theHash].mCollisionList == NULL)
		return FALSE;
	else
	{/* looks in the collision list */
		theItemsCount = LArray_GetItemsCount(This->mData[theHash].mCollisionList);
		for (i=0; i < theItemsCount; i++)
		{
		    theItem = *(struct TCLSlot*) LArray_ItemAt(This->mData[theHash].mCollisionList, i);
			if (theItem.mKey == inKey) return TRUE;
		}
	}
	return FALSE;
 }

 /*---------------------------------------------------------------------------------
 *  RemoveItem
 * ---------------------------------------------------------------------------------
 * Removes the item with key inKey if present */
 void LHash_RemoveItem (LHash* This, ui4 inKey)
 {
	 f4 theUsage;
	 ui4 i;
	 ui4 theItemsCount;
	 struct TCLSlot theItem;
	 ui4 theHash;

	 if (This == NULL) Throw(LHash_OBJECT_NULL_POINTER);

	 theHash = Hash_(This->mRandomOdd, inKey, This->mHashLength);
	 if (!LHash_IsInTable(This, inKey)) 
		/* the item isn't in the table, just exit */
		return;
	 This->mItemsCount--;
	 if (This->mData[theHash].mKey == inKey)
	 {/* item present at the main entry */
		 if (This->mData[theHash].mCollisionList == NULL)
		 {/* there's no collision list */
			This->mData[theHash].mUsed = FALSE;
		 }
		 else
		 {/* there's a collision list, remove first element from that */
		  /* and copy it on the main entry                            */
			theItem = *(struct TCLSlot*) LArray_ItemAt(This->mData[theHash].mCollisionList, 0);
			
		  /* to update correctly the DataSize of the table, first we subtract */
		  /* the dimension of the collision list, then we update it and add   */
		  /* the correct dimension to the field mDataSize of the table        */
			This->mDataSize -= LArray_GetUsedMem(This->mData[theHash].mCollisionList);
			LArray_RemoveItemAt(This->mData[theHash].mCollisionList, 0);
			if (LArray_GetItemsCount(This->mData[theHash].mCollisionList) == 0)
			{/* if the collision list is empty, delete the array */
				LArray_Delete( &(This->mData[theHash].mCollisionList) );
				This->mData[theHash].mCollisionList = NULL;
			 /* update the collision keys value */
				This->mCollisionKeys--;
			}
			else
				This->mDataSize += LArray_GetUsedMem(This->mData[theHash].mCollisionList);
			This->mData[theHash].mItemRef = theItem.mItemRef;
			This->mData[theHash].mKey     = theItem.mKey;
		 }
		 /* checks if the table is bigger than its default built up value */
		 /* and  it's  used for less than  10% of its entries, eventually */
		 /* shrinks it.													  */
		 theUsage = (f4)This->mItemsCount / (f4)This->mEntriesCount;
		 if ( ( theUsage < LOW_USAGE_T) && ( This->mHashLength > BASE_HASH_LENGTH) )
		 {/* only if there is an usage lower than 10% and the dimension of the */
		  /* table is bigger than its default value							   */
			 _Resize(This, SHRINK);
		 }
		 return;
	 }
	 /* the item must be in the collision list */
	 theItemsCount = LArray_GetItemsCount(This->mData[theHash].mCollisionList);
     for (i=0; i < theItemsCount; i++)
	 {
		 Try
			theItem = *(struct TCLSlot*) LArray_ItemAt(This->mData[theHash].mCollisionList, i);
		 CatchAny
			Rethrow;

		 if (theItem.mKey == inKey) 
		 {
		   /* as usual, first we subtract the LArray dimension, then we remove the item */
		   /* and finally we add again the DataSize of the LArray                       */

			This->mDataSize -= LArray_GetUsedMem(This->mData[theHash].mCollisionList);
			Try
	   		    LArray_RemoveItemAt(This->mData[theHash].mCollisionList, i);
			CatchAny
				Rethrow;

			if (LArray_GetItemsCount(This->mData[theHash].mCollisionList) == 0)
			{/* if the collision list is empty, delete the array */
				LArray_Delete( &(This->mData[theHash].mCollisionList) );
				This->mData[theHash].mCollisionList = NULL;
			 /* update the collision keys value					 */
				This->mCollisionKeys--;
			}
			else
				This->mDataSize += LArray_GetUsedMem(This->mData[theHash].mCollisionList);

			/* we did what we wanted, there's no point in running the for-loop */
			break;
		  }
	  }
	  /* check again if there's needed to shrink the table due to low usage */
	  theUsage = (f4)This->mItemsCount / (f4)This->mEntriesCount;
	  if ( ( theUsage < LOW_USAGE_T) && ( This->mHashLength > BASE_HASH_LENGTH) )
	  {/* only if there an usage lesser than 10% and the dimension of the table is */
	   /* bigger than default value												   */
		 _Resize(This, SHRINK);
	  }
	  return; 
 }

 /*---------------------------------------------------------------------------------
 *  RemoveAllItems
 * ---------------------------------------------------------------------------------
 * Removes all items in the table */
 void LHash_RemoveAllItems(LHash* This)
 {
	 ui4 i;
	 ui4 theEntriesCount;
	 ui4 theSizeOf = sizeof(struct TSlot);

	 if (This == NULL) Throw(LHash_OBJECT_NULL_POINTER);

	 /* if there's nothing to do, don't waste time */
	 if (This->mItemsCount == 0)
		 return;
	 for (i=0; i < This->mItemsCount; i++)
	 {
		 This->mData[i].mItemRef = 0;
		 This->mData[i].mUsed    = FALSE;
		 if (This->mData[i].mCollisionList != NULL)
		 {/* if there a collision list, remove it */
			 LArray_Delete( &(This->mData[i].mCollisionList) );
			 This->mData[i].mCollisionList = NULL;
		 }
	 }

	 This->mItemsCount    = 0;
	 This->mCollisionKeys = 0;
	 if ( This->mHashLength > BASE_HASH_LENGTH)
	 {/* only if the structure size is bigger than default, realloc it */
		theEntriesCount     = BASE_ENTRIES_COUNT;
		This->mHashLength   = BASE_HASH_LENGTH;
		This->mEntriesCount = theEntriesCount;
		This->mData = (struct TSlot*) LMemory_Realloc(This->mData, theEntriesCount*theSizeOf);
	 }
	 This->mDataSize = This->mEntriesCount * theSizeOf;	 
 }

 /*---------------------------------------------------------------------------------
 *  GetItemByKey
 * ---------------------------------------------------------------------------------
 * Returns (if present) a pointer to the item with key inKey, NULL otherwise */
 void* LHash_GetItemByKey (LHash* This, ui4 inKey)
 {
	struct TCLSlot* theItem;
	ui4 i;
	ui4 theItemsCount;
	ui4 theHash;

	if (This == NULL) Throw(LHash_OBJECT_NULL_POINTER);

	theHash = Hash_(This->mRandomOdd, inKey, This->mHashLength);
	/* checks if the item is present */
	if (!LHash_IsInTable(This, inKey))
		return 0;
	/* checks if the item is at the main entry */
	if (This->mData[theHash].mKey == inKey)
		return This->mData[theHash].mItemRef;
	/* scans the collision list for the item */
	theItemsCount = LArray_GetItemsCount(This->mData[theHash].mCollisionList);
	for (i=0; i < theItemsCount; i++)
	 {
		 theItem = (struct TCLSlot*) LArray_ItemAt(This->mData[theHash].mCollisionList, i);
		 if (theItem->mKey == inKey) return theItem->mItemRef;
	 }	
	 return 0;
 }
	
ui4 LHash_GetUsedMem(LHash* This)
{ 
	if (This == NULL) Throw(LHash_OBJECT_NULL_POINTER);
	return This->mDataSize; 
}

ui4 LHash_GetItemsCount(LHash* This)
{	
	if (This == NULL) Throw(LHash_OBJECT_NULL_POINTER);
	return This->mItemsCount; 
}

ui4 LHash_GetCollisionKeysCount(LHash* This)
{ 
	if (This == NULL) Throw(LHash_OBJECT_NULL_POINTER);
	return This->mCollisionKeys; 
}


/*---------------------------------------------------------------------------------
 *  Dump
 * ---------------------------------------------------------------------------------
 * Gives a representation of the table */

void LHash_Dump(LHash* This)
{
	ui4 i;
	ui4 j;
	ui4 theItemsCount;
	struct TCLSlot theItem;

	if (!This->mDebug) return;
	if (This == NULL) Throw(LHash_OBJECT_NULL_POINTER);

	LDebug_Print("\n\n***************DUMP***************\n");
	LDebug_Print("[EntriesCount =%u]\n", This->mEntriesCount);
	LDebug_Print("[DataSize     =%u]\n", This->mDataSize);
	LDebug_Print("[ItemsCount   =%u]\n", This->mItemsCount);
	LDebug_Print("[HashLength   =%u]\n", This->mHashLength);
	LDebug_Print("[RandomOdd    =%u]\n", This->mRandomOdd);
	LDebug_Print("[CollisionKeys=%u]\n", This->mCollisionKeys);
	LDebug_Print("\n\n***************MAIN STRUCTURE***************\n\n");
	for(i=0; i < This->mEntriesCount; i++)
	{
		if (This->mData[i].mUsed == TRUE)
		{
			LDebug_Print("Index[%u] - Key[%u] - ItemRef[%u]",i , This->mData[i].mKey, This->mData[i].mItemRef);
			if (This->mData[i].mCollisionList != NULL)
			{
				LDebug_Print(">");
				theItemsCount = LArray_GetItemsCount(This->mData[i].mCollisionList);
				for (j=0; j < theItemsCount; j++)
				{
					theItem = *(struct TCLSlot*)LArray_ItemAt(This->mData[i].mCollisionList, j);
					LDebug_Print("Key[%u] - ItemRef[%u] * ", theItem.mKey, theItem.mItemRef);
				}
			}
			LDebug_Print("\n");
		}
		else
			LDebug_Print("Index[%u]    ***EMPTY***\n", i);
	}
}

void LHash_SetDebug(LHash* This, Bool inDebug)
{ This->mDebug = inDebug;}

/* PRIVATE METHODS */	 

/*----------------------------------------------------------------------------------
 *  _Resize
 * ---------------------------------------------------------------------------------
 * Expand or Shrink by a 2 factor the entries of the table, and correctly reinserts 
 * all elements */
void _Resize(LHash* This, i1 inFactor)
{
	ui4 i;
	ui4 j;
	ui4 theItemsCount;
	ui4 theNewSize;
	ui4 theNewEntriesCount;
	ui4 theOldEntriesCount;
	f4  theMD;/* multiply or divide factor */
	i1  theAS;/* add or subtract factor    */
	struct TCLSlot theItem;
	struct TSlot*  theTempData = NULL;
	/* ui4 theSeed = 777; TODO use a random seed */

	if (This == NULL) Throw(LHash_OBJECT_NULL_POINTER);

	if (inFactor == EXPAND)
	{ theMD = 2  ; theAS =  1; }
	else
	{ theMD = 0.5; theAS = -1; }

    theNewEntriesCount = (ui4)(theMD * This->mEntriesCount);
	theNewSize         = theNewEntriesCount * sizeof(struct TSlot);
	theOldEntriesCount = This->mEntriesCount;

	Try
	{/* checks if there is enough memory for expanding the structure */ 
		theTempData = (struct TSlot*)LMemory_Malloc(theNewSize);
	}
	CatchAny
	{/* if we cannot have the requested amount of new memory, leave the structure */
	 /* unchanged																  */
		if ( theTempData != NULL) LMemory_Free( &theTempData );
		return;
	}
	for (i=0; i < theNewEntriesCount; i++)
	{/* initializes the new table */
		theTempData[i].mUsed = FALSE;
		theTempData[i].mCollisionList = NULL;
	}
	/* updates private fields to new values */
	This->mCollisionKeys = 0;
	This->mItemsCount    = 0;
	This->mHashLength    = This->mHashLength + theAS;
	This->mEntriesCount  = theNewEntriesCount;
	This->mDataSize      = theNewSize + sizeof(LHash);
	/* this loop copies all the couples [key, itemRef] in the temp structure */
	for (i=0; i < theOldEntriesCount; i++)
	{
		if ( This->mData[i].mUsed == TRUE)
		{
			_InsertItem(This, theTempData, This->mData[i].mItemRef, This->mData[i].mKey);
			if ( This->mData[i].mCollisionList != NULL)
			{/* if there's also a collision list, copy each item in the temp structure */
				theItemsCount = LArray_GetItemsCount( This->mData[i].mCollisionList);
				for (j=0; j < theItemsCount; j++)
				{
					theItem = *(struct TCLSlot*)LArray_ItemAt( This->mData[i].mCollisionList, j);
					_InsertItem(This, theTempData, theItem.mItemRef, theItem.mKey);
				}
				/* also dealloc the LArray */
				LArray_Delete( &(This->mData[i].mCollisionList) );
			}
		}
	}
	/* dealloc old mData field */
	LMemory_Free( &This->mData );
	/* realloc it */
	This->mData = theTempData;
}

void _InsertItem (LHash* This, struct TSlot* inData, void* inItem, ui4 inKey)
 {
	 struct TCLSlot theItem; 
	 ui4 theIndex;
	 ui4 theHash;
	 
	 theHash = Hash_(This->mRandomOdd, inKey, This->mHashLength);
	 This->mItemsCount++;
	 if (inData[theHash].mUsed == FALSE)
	 {/* entry is unused */
		 inData[theHash].mUsed    = TRUE;
         inData[theHash].mKey     = inKey;
		 inData[theHash].mItemRef = inItem;
	 }
	 else
	 {/* collision on entry 'theHash' occurs */
		 if (inData[theHash].mCollisionList == NULL)
		 {/* first item with a collision */
			This->mCollisionKeys++;
			inData[theHash].mCollisionList = LArray_New((ui4)sizeof(struct TCLSlot));
		 }
		 else
		 {/* if there was already a collision list, in order to update */
          /* correctly the This->mDataSize field, we  have to subtract */
		  /* the previous dimension of the LArray					   */
			This->mDataSize -= LArray_GetUsedMem(inData[theHash].mCollisionList);
		 }
		 
		 theItem.mItemRef = inItem;
		 theItem.mKey	  = inKey;
		 
		 /* then appends the new item */
		 theIndex = LArray_AppendItem(inData[theHash].mCollisionList, &theItem);
		 /* and finally updates DataSize value */
		 This->mDataSize += LArray_GetUsedMem(inData[theHash].mCollisionList);
	 }
 }

/* Copyright (C) 2002 Stefano Emiliozzi

 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.

 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
