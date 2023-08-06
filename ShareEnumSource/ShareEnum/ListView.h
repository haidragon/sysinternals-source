//////////////////////////////////////////////////////////////
// Copyright (C) 2002-2003 Bryce Cogswell 
// www.sysinternals.com
// cogswell@winternals.com
//
// You may modify and use this code for personal use only.
// Redistribution in any form is expressly prohibited
// without permission by the author.
//////////////////////////////////////////////////////////////
#pragma once

enum 
{
	LISTVIEW_IMG_FILE,			// default icon
	LISTVIEW_IMG_ASCENDING,
	LISTVIEW_IMG_DESCENDING,
};

enum DATATYPE 
{
	DATATYPE_TEXT,
	DATATYPE_NUMBER,
	DATATYPE_HEX,
	DATATYPE_DATE,
};

enum SORTDIR 
{
	SORTDIR_UNDEF,
	SORTDIR_UP,
	SORTDIR_DOWN,
};

struct LISTVIEW_COLUMN 
{
	const TCHAR		*	Title;
	int					Width;
	DATATYPE			Type;
	SORTDIR				SortDir;
};

struct LISTVIEW_SORT 
{
	HWND				hList;
	LISTVIEW_COLUMN	*	ColumnList;
	int					Column;
	bool				UsingEx;
};

void InitListViewColumns( HWND hListView, const LISTVIEW_COLUMN * Cols, int numColumns, long style = LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP );
void SortListView(HWND hListview, int iSubItem, LISTVIEW_COLUMN * ColumnList, bool Reverse = true );
LPARAM ListView_GetParam( HWND hListView, UINT iItem );
void PopupListviewColumnsMenu( HWND hListview, LISTVIEW_COLUMN * ColumnList );
int GetListViewSortColumn( LISTVIEW_COLUMN * ColumnList );
void SaveListViewColumns( HWND hListView );
