/*
 * ComboBoxEx control
 *
 * Copyright 1998, 1999 Eric Kohl
 * Copyright 2000, 2001, 2002 Guy Albertelli <galberte@neo.lrun.com>
 * Copyright 2002 Dimitrie O. Paun
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * TODO   <-------------
 *  1. The following extended styles need to be implemented, use will
 *     result in a FIXME:
 *           CBES_EX_NOEDITIMAGEINDENT
 *	     CBES_EX_PATHWORDBREAKPROC
 *	     CBES_EX_NOSIZELIMIT
 *	     CBES_EX_CASESENSITIVE
 *  2. None of the following callback items are implemented. Therefor
 *     no CBEN_GETDISPINFO notifies are issued. Use in either CBEM_INSERTITEM
 *     or CBEM_SETITEM will result in a FIXME:
 *           LPSTR_TEXTCALLBACK
 *           I_IMAGECALLBACK
 *           I_INDENTCALLBACK
 *  3. No use is made of the iOverlay image.
 *  4. Notify CBEN_DRAGBEGIN is not implemented.
 *
 */

#include <string.h>
#include "winbase.h"
#include "commctrl.h"
#include "wine/debug.h"
#include "wine/unicode.h"

WINE_DEFAULT_DEBUG_CHANNEL(comboex);
/*
 * The following is necessary for the test done in COMBOEX_DrawItem
 * to determine whether to dump out the DRAWITEM structure or not.
 */
WINE_DECLARE_DEBUG_CHANNEL(message);

/* Item structure */
typedef struct
{
    VOID         *next;
    UINT         mask;
    LPWSTR       pszText;
    int          cchTextMax;
    int          iImage;
    int          iSelectedImage;
    int          iOverlay;
    int          iIndent;
    LPARAM       lParam;
} CBE_ITEMDATA;

/* ComboBoxEx structure */
typedef struct
{
    HIMAGELIST   himl;
    HWND         hwndSelf;         /* my own hwnd */
    HWND         hwndCombo;
    HWND         hwndEdit;
    WNDPROC      prevEditWndProc;  /* previous Edit WNDPROC value */
    WNDPROC      prevComboWndProc; /* previous Combo WNDPROC value */
    DWORD        dwExtStyle;
    INT          selected;         /* index of selected item */
    DWORD        flags;            /* WINE internal flags */
    HFONT        defaultFont;
    HFONT        font;
    INT          nb_items;         /* Number of items */
    BOOL         unicode;          /* TRUE if this window is Unicode   */
    BOOL         NtfUnicode;       /* TRUE if parent wants notify in Unicode */
    CBE_ITEMDATA *edit;            /* item data for edit item */
    CBE_ITEMDATA *items;           /* Array of items */
} COMBOEX_INFO;

/* internal flags in the COMBOEX_INFO structure */
#define  WCBE_ACTEDIT        0x00000001     /* Edit active i.e.
                                             * CBEN_BEGINEDIT issued
                                             * but CBEN_ENDEDIT{A|W}
                                             * not yet issued. */
#define  WCBE_EDITCHG        0x00000002     /* Edit issued EN_CHANGE */
#define  WCBE_EDITHASCHANGED (WCBE_ACTEDIT | WCBE_EDITCHG)
#define  WCBE_EDITFOCUSED    0x00000004     /* Edit control has focus */


#define ID_CB_EDIT    1001


/*
 * Special flag set in DRAWITEMSTRUCT itemState field. It is set by
 * the ComboEx version of the Combo Window Proc so that when the 
 * WM_DRAWITEM message is then passed to ComboEx, we know that this 
 * particular WM_DRAWITEM message is for listbox only items. Any messasges
 * without this flag is then for the Edit control field.
 *
 * We really cannot use the ODS_COMBOBOXEDIT flag because MSDN states that 
 * only version 4.0 applications will have ODS_COMBOBOXEDIT set.
 */
#define ODS_COMBOEXLBOX  0x4000



/* Height in pixels of control over the amount of the selected font */
#define CBE_EXTRA     3

/* Indent amount per MS documentation */
#define CBE_INDENT    10

/* Offset in pixels from left side for start of image or text */
#define CBE_STARTOFFSET   6

/* Offset between image and text */
#define CBE_SEP   4

#define COMBOEX_SUBCLASS_PROP "CCComboEx32SubclassInfo"
#define COMBOEX_GetInfoPtr(hwnd) ((COMBOEX_INFO *)GetWindowLongW (hwnd, 0))


/* Things common to the entire DLL */
static LRESULT WINAPI
COMBOEX_EditWndProc (HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static LRESULT WINAPI
COMBOEX_ComboWndProc (HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static void COMBOEX_DumpItem (CBE_ITEMDATA *item)
{
    TRACE("item %p - mask=%08x, pszText=%p, cchTM=%d, iImage=%d\n",
          item, item->mask, item->pszText, item->cchTextMax, item->iImage);
    TRACE("item %p - iSelectedImage=%d, iOverlay=%d, iIndent=%d, lParam=%08lx\n",
          item, item->iSelectedImage, item->iOverlay, item->iIndent, item->lParam);
    if ((item->mask & CBEIF_TEXT) && item->pszText)
        TRACE("item %p - pszText=%s\n", item, debugstr_w(item->pszText));
}


static void COMBOEX_DumpInput (COMBOBOXEXITEMW *input)
{
    TRACE("input - mask=%08x, iItem=%d, pszText=%p, cchTM=%d, iImage=%d\n",
          input->mask, input->iItem, input->pszText, input->cchTextMax,
          input->iImage);
    if ((input->mask & CBEIF_TEXT) && input->pszText)
        TRACE("input - pszText=<%s>\n", debugstr_w(input->pszText));
    TRACE("input - iSelectedImage=%d, iOverlay=%d, iIndent=%d, lParam=%08lx\n",
          input->iSelectedImage, input->iOverlay, input->iIndent, input->lParam);
}


inline static LRESULT
COMBOEX_Forward (COMBOEX_INFO *infoPtr, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (infoPtr->hwndCombo)    
	return SendMessageW (infoPtr->hwndCombo, uMsg, wParam, lParam);

    return 0;
}


static INT COMBOEX_Notify (COMBOEX_INFO *infoPtr, INT code, NMHDR *hdr)
{
    hdr->idFrom = GetDlgCtrlID (infoPtr->hwndSelf);
    hdr->hwndFrom = infoPtr->hwndSelf;
    hdr->code = code;
    if (infoPtr->NtfUnicode)
	return SendMessageW (GetParent(infoPtr->hwndSelf), WM_NOTIFY, 0,
			     (LPARAM)hdr);
    else
	return SendMessageA (GetParent(infoPtr->hwndSelf), WM_NOTIFY, 0,
			     (LPARAM)hdr);
}


static INT
COMBOEX_NotifyItem (COMBOEX_INFO *infoPtr, INT code, NMCOMBOBOXEXW *hdr)
{

    /* Change the Text item from Unicode to ANSI if necessary for NOTIFY */

    hdr->hdr.idFrom = GetDlgCtrlID (infoPtr->hwndSelf);
    hdr->hdr.hwndFrom = infoPtr->hwndSelf;
    hdr->hdr.code = code;
    if (infoPtr->NtfUnicode)
	return SendMessageW (GetParent(infoPtr->hwndSelf), WM_NOTIFY, 0,
			     (LPARAM)hdr);
    else {
	LPWSTR str, ostr = NULL;
	INT ret, len = 0;

	if (hdr->ceItem.mask & CBEIF_TEXT) {
	    ostr = hdr->ceItem.pszText;
	    str = ostr;
	    if (!str) str = (LPWSTR)L"";
	    len = WideCharToMultiByte (CP_ACP, 0, str, -1, 0, 0, NULL, NULL);
	    if (len > 0) {
		hdr->ceItem.pszText = (LPWSTR)COMCTL32_Alloc ((len + 1)*sizeof(CHAR));
		//FIXME: what if alloc failed?
		WideCharToMultiByte (CP_ACP, 0, str, -1, (LPSTR)hdr->ceItem.pszText,
				     len, NULL, NULL);
	    }
	}

	ret = SendMessageA (GetParent(infoPtr->hwndSelf), WM_NOTIFY, 0,
			    (LPARAM)hdr);
	if (hdr->ceItem.mask & CBEIF_TEXT) {
	    if (len > 0)
		COMCTL32_Free (hdr->ceItem.pszText);
	    hdr->ceItem.pszText = ostr;
	}
	return ret;
    }
}


static INT
COMBOEX_NotifyEndEdit (COMBOEX_INFO *infoPtr, NMCBEENDEDITW *hdr, LPWSTR itemText)
{

    /* Change the Text item from Unicode to ANSI if necessary for NOTIFY */

    hdr->hdr.idFrom = GetDlgCtrlID (infoPtr->hwndSelf);
    hdr->hdr.hwndFrom = infoPtr->hwndSelf;
    hdr->hdr.code = (infoPtr->NtfUnicode) ? CBEN_ENDEDITW : CBEN_ENDEDITA;
    if (infoPtr->NtfUnicode)
	return SendMessageW (GetParent(infoPtr->hwndSelf), WM_NOTIFY, 0,
			     (LPARAM)hdr);
    else {
	NMCBEENDEDITA ansi;

	memcpy (&ansi.hdr, &hdr->hdr, sizeof(NMHDR));
	ansi.fChanged = hdr->fChanged;
	ansi.iNewSelection = hdr->iNewSelection;
	WideCharToMultiByte (CP_ACP, 0, itemText, -1,
			     (LPSTR)&ansi.szText, CBEMAXSTRLEN, NULL, NULL);
	ansi.iWhy = hdr->iWhy;
	return SendMessageA (GetParent(infoPtr->hwndSelf), WM_NOTIFY, 0,
			     (LPARAM)&ansi);
    }
}


static void COMBOEX_GetComboFontSize (COMBOEX_INFO *infoPtr, SIZE *size)
{
    HFONT nfont, ofont;
    HDC mydc;

    mydc = GetDC (0); /* why the entire screen???? */
    nfont = SendMessageW (infoPtr->hwndCombo, WM_GETFONT, 0, 0);
    ofont = (HFONT) SelectObject (mydc, nfont);
    GetTextExtentPointA (mydc, "A", 1, size);
    SelectObject (mydc, ofont);
    ReleaseDC (0, mydc);
    TRACE("selected font hwnd=%08x, height=%ld\n", nfont, size->cy);
}


static void COMBOEX_CopyItem (CBE_ITEMDATA *item, COMBOBOXEXITEMW *cit)
{
    if (cit->mask & CBEIF_TEXT) {
        /*
         * when given a text buffer actually use that buffer
         */
        if (cit->pszText)
        {
            lstrcpynW(cit->pszText,item->pszText,cit->cchTextMax);
        }
        else
        {
            cit->pszText        = item->pszText;
            cit->cchTextMax     = item->cchTextMax;
        }
    }
    if (cit->mask & CBEIF_IMAGE)
	cit->iImage         = item->iImage;
    if (cit->mask & CBEIF_SELECTEDIMAGE)
	cit->iSelectedImage = item->iSelectedImage;
    if (cit->mask & CBEIF_OVERLAY)
	cit->iOverlay       = item->iOverlay;
    if (cit->mask & CBEIF_INDENT)
	cit->iIndent        = item->iIndent;
    if (cit->mask & CBEIF_LPARAM)
	cit->lParam         = item->lParam;
}


static void COMBOEX_AdjustEditPos (COMBOEX_INFO *infoPtr)
{
    SIZE mysize;
    IMAGEINFO iinfo;
    INT x, y, w, h, xoff = 0;
    RECT rect;

    if (!infoPtr->hwndEdit) return;

    iinfo.rcImage.left = iinfo.rcImage.right = 0;
    if (infoPtr->himl) {
	ImageList_GetImageInfo(infoPtr->himl, 0, &iinfo);
	xoff = iinfo.rcImage.right - iinfo.rcImage.left + CBE_SEP;
    }
    GetClientRect (infoPtr->hwndCombo, &rect);
    InflateRect (&rect, -2, -2);
    InvalidateRect (infoPtr->hwndCombo, &rect, TRUE);

    /* reposition the Edit control based on whether icon exists */
    COMBOEX_GetComboFontSize (infoPtr, &mysize);
    TRACE("Combo font x=%ld, y=%ld\n", mysize.cx, mysize.cy);
    x = xoff + CBE_STARTOFFSET + 1;
    w = rect.right-rect.left - x - GetSystemMetrics(SM_CXVSCROLL) - 1;
    h = mysize.cy + 1;
    y = rect.bottom - h - 1;

    TRACE("Combo client (%d,%d)-(%d,%d), setting Edit to (%d,%d)-(%d,%d)\n",
	  rect.left, rect.top, rect.right, rect.bottom, x, y, x + w, y + h);
    SetWindowPos(infoPtr->hwndEdit, HWND_TOP, x, y, w, h,
		 SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOZORDER);
}


static void COMBOEX_ReSize (COMBOEX_INFO *infoPtr)
{
    SIZE mysize;
    UINT cy;
    IMAGEINFO iinfo;

    COMBOEX_GetComboFontSize (infoPtr, &mysize);
    cy = mysize.cy + CBE_EXTRA;
    if (infoPtr->himl) {
	ImageList_GetImageInfo(infoPtr->himl, 0, &iinfo);
	cy = max (iinfo.rcImage.bottom - iinfo.rcImage.top, cy);
	TRACE("upgraded height due to image:  height=%d\n", cy);
    }
    SendMessageW (infoPtr->hwndSelf, CB_SETITEMHEIGHT, (WPARAM)-1, (LPARAM)cy);
    if (infoPtr->hwndCombo)
        SendMessageW (infoPtr->hwndCombo, CB_SETITEMHEIGHT,
		      (WPARAM) 0, (LPARAM) cy);
}


static void COMBOEX_SetEditText (COMBOEX_INFO *infoPtr, CBE_ITEMDATA *item)
{
    if (!infoPtr->hwndEdit) return;
    /* native issues the following messages to the {Edit} control */
    /*      WM_SETTEXT (0,addr)     */
    /*      EM_SETSEL32 (0,0)       */
    /*      EM_SETSEL32 (0,-1)      */
    if (item->mask & CBEIF_TEXT) {
	SendMessageW (infoPtr->hwndEdit, WM_SETTEXT, 0, (LPARAM)item->pszText);
	SendMessageW (infoPtr->hwndEdit, EM_SETSEL, 0, 0);
	SendMessageW (infoPtr->hwndEdit, EM_SETSEL, 0, -1);
    }
}

 
static CBE_ITEMDATA * COMBOEX_FindItem(COMBOEX_INFO *infoPtr, INT index)
{
    CBE_ITEMDATA *item;
    INT i;

    if ((index > infoPtr->nb_items) || (index < -1))
	return 0;
    if (index == -1)
	return infoPtr->edit;
    item = infoPtr->items;
    i = infoPtr->nb_items - 1;

    /* find the item in the list */
    while (item && (i > index)) {
	item = (CBE_ITEMDATA *)item->next;
	i--;
    }
    if (!item || (i != index)) {
	ERR("COMBOBOXEX item structures broken. Please report!\n");
	return 0;
    }
    return item;
}


static inline BOOL COMBOEX_HasEdit(COMBOEX_INFO *infoPtr)
{
    return infoPtr->hwndEdit;
}
 

static void COMBOEX_WarnCallBack (CBE_ITEMDATA *item)
{
    if (item->pszText == LPSTR_TEXTCALLBACKW)
	FIXME("Callback not implemented yet for pszText\n");
    if (item->iImage == I_IMAGECALLBACK)
	FIXME("Callback not implemented yet for iImage\n");
    if (item->iSelectedImage == I_IMAGECALLBACK)
	FIXME("Callback not implemented yet for iSelectedImage\n");
    if (item->iOverlay == I_IMAGECALLBACK)
	FIXME("Callback not implemented yet for iOverlay\n");
    if (item->iIndent == I_INDENTCALLBACK)
	FIXME("Callback not implemented yet for iIndent\n");
}


/* ***  CBEM_xxx message support  *** */


static INT COMBOEX_DeleteItem (COMBOEX_INFO *infoPtr, INT index)
{
    CBE_ITEMDATA *item;

    TRACE("(index=%d)\n", index);

    /* if item number requested does not exist then return failure */
    if ((index > infoPtr->nb_items) || (index < 0)) return CB_ERR;
    if (!(item = COMBOEX_FindItem(infoPtr, index))) return CB_ERR;

    /* doing this will result in WM_DELETEITEM being issued */
    SendMessageW (infoPtr->hwndCombo, CB_DELETESTRING, (WPARAM)index, 0);

    return infoPtr->nb_items;
}


static BOOL COMBOEX_GetItemW (COMBOEX_INFO *infoPtr, COMBOBOXEXITEMW *cit)
{
    INT index = cit->iItem;
    CBE_ITEMDATA *item;

    TRACE("(...)\n");

    /* if item number requested does not exist then return failure */
    if ((index > infoPtr->nb_items) || (index < -1)) return FALSE;

    /* if the item is the edit control and there is no edit control, skip */
    if ((index == -1) && !COMBOEX_HasEdit(infoPtr)) return FALSE;

    if (!(item = COMBOEX_FindItem(infoPtr, index))) return FALSE;

    COMBOEX_CopyItem (item, cit);

    return TRUE;
}


static BOOL COMBOEX_GetItemA (COMBOEX_INFO *infoPtr, COMBOBOXEXITEMA *cit)
{
    COMBOBOXEXITEMW tmpcit;

    TRACE("(...)\n");

    tmpcit.mask = cit->mask;
    tmpcit.iItem = cit->iItem;
    tmpcit.pszText = 0;
    if(!COMBOEX_GetItemW (infoPtr, &tmpcit)) return FALSE;

    WideCharToMultiByte (CP_ACP, 0, tmpcit.pszText, -1, 
			 cit->pszText, cit->cchTextMax, NULL, NULL);

    cit->iImage = tmpcit.iImage;
    cit->iSelectedImage = tmpcit.iSelectedImage;
    cit->iOverlay = tmpcit.iOverlay;
    cit->iIndent = tmpcit.iIndent;
    cit->lParam = tmpcit.lParam;

    return TRUE;
}


inline static BOOL COMBOEX_HasEditChanged (COMBOEX_INFO *infoPtr)
{
    return COMBOEX_HasEdit(infoPtr) &&
	   (infoPtr->flags & WCBE_EDITHASCHANGED) == WCBE_EDITHASCHANGED;
}


static INT COMBOEX_InsertItemW (COMBOEX_INFO *infoPtr, COMBOBOXEXITEMW *cit)
{
    INT index;
    CBE_ITEMDATA *item;
    NMCOMBOBOXEXW nmcit;

    TRACE("\n");

    if (TRACE_ON(comboex)) COMBOEX_DumpInput (cit);

    /* get real index of item to insert */
    index = cit->iItem;
    if (index == -1) index = infoPtr->nb_items;
    if (index > infoPtr->nb_items) index = infoPtr->nb_items;

    /* get space and chain it in */
    if(!(item = (CBE_ITEMDATA *)COMCTL32_Alloc (sizeof(*item)))) return -1;
    
    item->next = NULL;
    item->pszText = NULL;

    /* locate position to insert new item in */
    if (index == infoPtr->nb_items) {
        /* fast path for iItem = -1 */
        item->next = infoPtr->items;
	infoPtr->items = item;
    }
    else {
        INT i = infoPtr->nb_items-1;
	CBE_ITEMDATA *moving = infoPtr->items;

	while ((i > index) && moving) {
	    moving = (CBE_ITEMDATA *)moving->next;
	    i--;
	}
	if (!moving) {
	    ERR("COMBOBOXEX item structures broken. Please report!\n");
	    COMCTL32_Free(item);
	    return -1;
	}
	item->next = moving->next;
	moving->next = item;
    }

    /* fill in our hidden item structure */
    item->mask           = cit->mask;
    if (item->mask & CBEIF_TEXT) {
        LPWSTR str;
	INT len;

        str = cit->pszText;
        if (!str) str = (LPWSTR) L"";
	len = strlenW (str);
	if (len > 0) {
	    item->pszText = (LPWSTR)COMCTL32_Alloc ((len + 1)*sizeof(WCHAR));
	    //FIXME: what if allocation fails?
	    strcpyW (item->pszText, str);
	}
	else
	    item->pszText = NULL;
        item->cchTextMax   = cit->cchTextMax;
    }
    if (item->mask & CBEIF_IMAGE)
        item->iImage         = cit->iImage;
    if (item->mask & CBEIF_SELECTEDIMAGE)
        item->iSelectedImage = cit->iSelectedImage;
    if (item->mask & CBEIF_OVERLAY)
        item->iOverlay       = cit->iOverlay;
    if (item->mask & CBEIF_INDENT)
        item->iIndent        = cit->iIndent;
    if (item->mask & CBEIF_LPARAM)
        item->lParam         = cit->lParam;
    infoPtr->nb_items++;

    COMBOEX_WarnCallBack (item);

    if (TRACE_ON(comboex)) COMBOEX_DumpItem (item);

    SendMessageW (infoPtr->hwndCombo, CB_INSERTSTRING, 
		  (WPARAM)cit->iItem, (LPARAM)item);

    memset (&nmcit.ceItem, 0, sizeof(nmcit.ceItem));
    COMBOEX_CopyItem (item, &nmcit.ceItem);
    COMBOEX_NotifyItem (infoPtr, CBEN_INSERTITEM, &nmcit);

    return index;

}


static INT COMBOEX_InsertItemA (COMBOEX_INFO *infoPtr, COMBOBOXEXITEMA *cit)
{
    COMBOBOXEXITEMW citW;
    INT	ret;

    memcpy(&citW,cit,sizeof(COMBOBOXEXITEMA));
    if (cit->mask & CBEIF_TEXT) {
        LPSTR str = cit->pszText;
	INT len = 0;

        if (str) len = MultiByteToWideChar (CP_ACP, 0, str, -1, NULL, 0);
	if (len > 0) {
	    citW.pszText = (LPWSTR)COMCTL32_Alloc ((len + 1)*sizeof(WCHAR));
	    if (!citW.pszText) return -1;
	    MultiByteToWideChar (CP_ACP, 0, str, -1, citW.pszText, len);
	}
    }
    ret = COMBOEX_InsertItemW(infoPtr, &citW);;
    
    if (citW.pszText) COMCTL32_Free(citW.pszText);
    
    return ret;
}


static DWORD 
COMBOEX_SetExtendedStyle (COMBOEX_INFO *infoPtr, DWORD mask, DWORD style)
{
    DWORD dwTemp;

    TRACE("(mask=x%08lx, style=0x%08lx)\n", mask, style);

    dwTemp = infoPtr->dwExtStyle;

    if (style &  (CBES_EX_NOEDITIMAGEINDENT |
		  CBES_EX_PATHWORDBREAKPROC |
		  CBES_EX_NOSIZELIMIT |
		  CBES_EX_CASESENSITIVE))
	FIXME("Extended style not implemented %08lx\n", style);

    if (mask)
	infoPtr->dwExtStyle = (infoPtr->dwExtStyle & ~mask) | style;
    else
	infoPtr->dwExtStyle = style;

    /*
     * native does this for CBES_EX_NOEDITIMAGE state change
     */
    if ((infoPtr->dwExtStyle & CBES_EX_NOEDITIMAGE) ^
	(dwTemp & CBES_EX_NOEDITIMAGE)) {
	/* if state of EX_NOEDITIMAGE changes, invalidate all */
	TRACE("EX_NOEDITIMAGE state changed to %ld\n",
	      infoPtr->dwExtStyle & CBES_EX_NOEDITIMAGE);
	InvalidateRect (infoPtr->hwndSelf, NULL, TRUE);
	COMBOEX_AdjustEditPos (infoPtr);
	if (infoPtr->hwndEdit)
	    InvalidateRect (infoPtr->hwndEdit, NULL, TRUE);
    }

    return dwTemp;
}


static HIMAGELIST COMBOEX_SetImageList (COMBOEX_INFO *infoPtr, HIMAGELIST himl)
{
    HIMAGELIST himlTemp = infoPtr->himl;

    TRACE("(...)\n");

    infoPtr->himl = himl;

    COMBOEX_ReSize (infoPtr);
    InvalidateRect (infoPtr->hwndCombo, NULL, TRUE);

    /* reposition the Edit control based on whether icon exists */
    COMBOEX_AdjustEditPos (infoPtr);
    return himlTemp;
}

static BOOL COMBOEX_SetItemW (COMBOEX_INFO *infoPtr, COMBOBOXEXITEMW *cit)
{
    INT index = cit->iItem;
    CBE_ITEMDATA *item;

    if (TRACE_ON(comboex)) COMBOEX_DumpInput (cit);

    /* if item number requested does not exist then return failure */
    if ((index > infoPtr->nb_items) || (index < -1)) return FALSE;

    /* if the item is the edit control and there is no edit control, skip */
    if ((index == -1) && !COMBOEX_HasEdit(infoPtr)) return FALSE;

    if (!(item = COMBOEX_FindItem(infoPtr, index))) return FALSE;

    /* add/change stuff to the internal item structure */ 
    item->mask |= cit->mask;
    if (cit->mask & CBEIF_TEXT) {
        LPWSTR str = cit->pszText;
	INT len = 0;

        if (str) len = strlenW(str);
	if (len > 0) {
	    item->pszText = (LPWSTR)COMCTL32_Alloc ((len + 1)*sizeof(WCHAR));
	    if (!item->pszText) return FALSE;
	    strcpyW(item->pszText, str);
	}
        item->cchTextMax   = cit->cchTextMax;
    }
    if (cit->mask & CBEIF_IMAGE)
        item->iImage         = cit->iImage;
    if (cit->mask & CBEIF_SELECTEDIMAGE)
        item->iSelectedImage = cit->iSelectedImage;
    if (cit->mask & CBEIF_OVERLAY)
        item->iOverlay       = cit->iOverlay;
    if (cit->mask & CBEIF_INDENT)
        item->iIndent        = cit->iIndent;
    if (cit->mask & CBEIF_LPARAM)
        cit->lParam         = cit->lParam;

    COMBOEX_WarnCallBack (item);

    if (TRACE_ON(comboex)) COMBOEX_DumpItem (item);

    /* if original request was to update edit control, do some fast foot work */
    if (cit->iItem == -1) {
	COMBOEX_SetEditText (infoPtr, item);
	RedrawWindow (infoPtr->hwndCombo, 0, 0, RDW_ERASE | RDW_INVALIDATE);
    }
    return TRUE;
}

static BOOL COMBOEX_SetItemA (COMBOEX_INFO *infoPtr, COMBOBOXEXITEMA *cit)
{
    COMBOBOXEXITEMW	citW;
    BOOL		ret;

    memcpy(&citW, cit, sizeof(COMBOBOXEXITEMA));
    if (cit->mask & CBEIF_TEXT) {
        LPSTR str = cit->pszText;
	INT len = 0;

        str = cit->pszText;
        if (str) len = MultiByteToWideChar (CP_ACP, 0, str, -1, NULL, 0);
	if (len > 0) {
	    citW.pszText = (LPWSTR)COMCTL32_Alloc ((len + 1)*sizeof(WCHAR));
	    if (!citW.pszText) return FALSE;
	    MultiByteToWideChar (CP_ACP, 0, str, -1, citW.pszText, len);
	}
    }
    ret = COMBOEX_SetItemW(infoPtr, &citW);;

    if (citW.pszText) COMCTL32_Free(citW.pszText);

    return ret;
}


static BOOL COMBOEX_SetUnicodeFormat (COMBOEX_INFO *infoPtr, BOOL value)
{
    BOOL bTemp = infoPtr->unicode;

    TRACE("to %s, was %s\n", value ? "TRUE":"FALSE", bTemp ? "TRUE":"FALSE");

    infoPtr->unicode = value;

    return bTemp;
}


/* ***  CB_xxx message support  *** */

static INT
COMBOEX_FindStringExact (COMBOEX_INFO *infoPtr, INT start, LPCSTR str)
{
    INT i, count;
    CBE_ITEMDATA *item;
    LPWSTR desired = NULL;

    i = MultiByteToWideChar (CP_ACP, 0, str, -1, NULL, 0);
    if (i > 0) {
	desired = (LPWSTR)COMCTL32_Alloc ((i + 1)*sizeof(WCHAR));
	if (!desired) return CB_ERR;
	MultiByteToWideChar (CP_ACP, 0, str, -1, desired, i);
    }

    count = SendMessageW (infoPtr->hwndCombo, CB_GETCOUNT, 0, 0);

    /* now search from after starting loc and wrapping back to start */
    for(i=start+1; i<count; i++) {
	item = (CBE_ITEMDATA *)SendMessageW (infoPtr->hwndCombo, 
			  CB_GETITEMDATA, (WPARAM)i, 0);
	TRACE("desired=%s, item=%s\n", 
	      debugstr_w(desired), debugstr_w(item->pszText));
	if (lstrcmpiW(item->pszText, desired) == 0) {
	    COMCTL32_Free (desired);
	    return i;
	}
    }
    for(i=0; i<=start; i++) {
	item = (CBE_ITEMDATA *)SendMessageW (infoPtr->hwndCombo, 
			  CB_GETITEMDATA, (WPARAM)i, 0);
	TRACE("desired=%s, item=%s\n", 
	      debugstr_w(desired), debugstr_w(item->pszText));
	if (lstrcmpiW(item->pszText, desired) == 0) {
	    COMCTL32_Free (desired);
	    return i;
	}
    }
    COMCTL32_Free(desired);
    return CB_ERR;
}


static DWORD COMBOEX_GetItemData (COMBOEX_INFO *infoPtr, INT index)
{
    CBE_ITEMDATA *item1, *item2;
    DWORD ret = 0;

    item1 = (CBE_ITEMDATA *)COMBOEX_Forward (infoPtr, CB_GETITEMDATA, index, 0);
    if ((item1 != NULL) && ((LRESULT)item1 != CB_ERR)) {
	item2 = COMBOEX_FindItem (infoPtr, index);
	if (item2 != item1) {
	    ERR("data structures damaged!\n");
	    return CB_ERR;
	}
	if (item1->mask & CBEIF_LPARAM) ret = item1->lParam;
	TRACE("returning 0x%08lx\n", ret);
    } else {
        ret = (DWORD)item1;
        TRACE("non-valid result from combo, returning 0x%08lx\n", ret);
    }
    return ret;
}


static INT COMBOEX_SetCursel (COMBOEX_INFO *infoPtr, INT index)
{
    CBE_ITEMDATA *item;
    INT sel;

    if (!(item = COMBOEX_FindItem(infoPtr, index)))
	return SendMessageW (infoPtr->hwndCombo, CB_SETCURSEL, index, 0);

    TRACE("selecting item %d text=%s\n", index, (item->pszText) ?
	  debugstr_w(item->pszText) : "<null>");
    infoPtr->selected = index;

    sel = (INT)SendMessageW (infoPtr->hwndCombo, CB_SETCURSEL, index, 0);
    COMBOEX_SetEditText (infoPtr, item);
    return sel;
}


static DWORD COMBOEX_SetItemData (COMBOEX_INFO *infoPtr, INT index, DWORD data)
{
    CBE_ITEMDATA *item1, *item2;

    item1 = (CBE_ITEMDATA *)COMBOEX_Forward (infoPtr,CB_GETITEMDATA,index,data);
    
    if ((item1 != NULL) && ((LRESULT)item1 != CB_ERR)) {
	item2 = COMBOEX_FindItem (infoPtr, index);
	if (item2 != item1) {
	    ERR("data structures damaged!\n");
	    return CB_ERR;
	}
	item1->mask |= CBEIF_LPARAM;
	item1->lParam = data;
	TRACE("setting lparam to 0x%08lx\n", data);
	return 0;
    }
    TRACE("non-valid result from combo 0x%08lx\n", (DWORD)item1);
    return (LRESULT)item1;
}


static INT COMBOEX_SetItemHeight (COMBOEX_INFO *infoPtr, INT index, UINT height)
{
    RECT cb_wrect, cbx_wrect, cbx_crect;

    /* First, lets forward the message to the normal combo control
       just like Windows.     */
    if (infoPtr->hwndCombo)
       if (SendMessageW (infoPtr->hwndCombo, CB_SETITEMHEIGHT,
			 index, height) == CB_ERR) return CB_ERR;

    GetWindowRect (infoPtr->hwndCombo, &cb_wrect);
    GetWindowRect (infoPtr->hwndSelf, &cbx_wrect);
    GetClientRect (infoPtr->hwndSelf, &cbx_crect);
    /* the height of comboex as height of the combo + comboex border */ 
    height = cb_wrect.bottom-cb_wrect.top
             + cbx_wrect.bottom-cbx_wrect.top
             - (cbx_crect.bottom-cbx_crect.top);
    TRACE("EX window=(%d,%d)-(%d,%d), client=(%d,%d)-(%d,%d)\n",
	  cbx_wrect.left, cbx_wrect.top, cbx_wrect.right, cbx_wrect.bottom,
	  cbx_crect.left, cbx_crect.top, cbx_crect.right, cbx_crect.bottom);
    TRACE("CB window=(%d,%d)-(%d,%d), EX setting=(0,0)-(%d,%d)\n",
	  cb_wrect.left, cb_wrect.top, cb_wrect.right, cb_wrect.bottom,
	  cbx_wrect.right-cbx_wrect.left, height);
    SetWindowPos (infoPtr->hwndSelf, HWND_TOP, 0, 0,
		  cbx_wrect.right-cbx_wrect.left, height,
		  SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE);

    return 0;
}


/* ***  WM_xxx message support  *** */


static LRESULT COMBOEX_Create (HWND hwnd, LPCREATESTRUCTA cs)
{
    WCHAR COMBOBOX[] = { 'C', 'o', 'm', 'b', 'o', 'B', 'o', 'x', 0 };
    WCHAR EDIT[] = { 'E', 'D', 'I', 'T', 0 };
    WCHAR NIL[] = { 0 };
    COMBOEX_INFO *infoPtr;
    DWORD dwComboStyle;
    LOGFONTW mylogfont;
    RECT wnrc1, clrc1, cmbwrc;
    INT i;

    /* allocate memory for info structure */
    infoPtr = (COMBOEX_INFO *)COMCTL32_Alloc (sizeof(COMBOEX_INFO));
    if (!infoPtr) return -1;

    /* initialize info structure */
    /* note that infoPtr is allocated zero-filled */
    
    infoPtr->hwndSelf = hwnd;
    infoPtr->selected = -1;

    infoPtr->unicode = IsWindowUnicode (hwnd);

    i = SendMessageW(GetParent (hwnd), WM_NOTIFYFORMAT, hwnd, NF_QUERY);
    if ((i != NFR_ANSI) && (i != NFR_UNICODE)) {
	WARN("wrong response to WM_NOTIFYFORMAT (%d), assuming ANSI\n", i);
	i = NFR_ANSI;
    }
    infoPtr->NtfUnicode = (i == NFR_UNICODE);

    SetWindowLongW (hwnd, 0, (DWORD)infoPtr);

    /* create combo box */
    dwComboStyle = GetWindowLongW (hwnd, GWL_STYLE) &
			(CBS_SIMPLE|CBS_DROPDOWN|CBS_DROPDOWNLIST|WS_CHILD);

    GetWindowRect(hwnd, &wnrc1);
    GetClientRect(hwnd, &clrc1);
    TRACE("EX window=(%d,%d)-(%d,%d) client=(%d,%d)-(%d,%d)\n",
	  wnrc1.left, wnrc1.top, wnrc1.right, wnrc1.bottom,
	  clrc1.left, clrc1.top, clrc1.right, clrc1.bottom);
    TRACE("combo style=%08lx, adding style=%08lx\n", dwComboStyle,
          WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VSCROLL |
          CBS_NOINTEGRALHEIGHT | CBS_DROPDOWNLIST |
	  WS_CHILD | WS_VISIBLE | CBS_OWNERDRAWFIXED);

    /* Native version of ComboEx creates the ComboBox with DROPDOWNLIST */
    /* specified. It then creates it's own version of the EDIT control  */
    /* and makes the ComboBox the parent. This is because a normal      */
    /* DROPDOWNLIST does not have a EDIT control, but we need one.      */
    /* We also need to place the edit control at the proper location    */
    /* (allow space for the icons).                                     */

    infoPtr->hwndCombo = CreateWindowW (COMBOBOX, NIL,
			 /* following line added to match native */
                         WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VSCROLL |
                         CBS_NOINTEGRALHEIGHT | CBS_DROPDOWNLIST | 
			 /* was base and is necessary */
			 WS_CHILD | WS_VISIBLE | CBS_OWNERDRAWFIXED | dwComboStyle,
			 cs->y, cs->x, cs->cx, cs->cy, hwnd,
			 (HMENU) GetWindowLongW (hwnd, GWL_ID),
			 GetWindowLongW (hwnd, GWL_HINSTANCE), NULL);

    /* 
     * native does the following at this point according to trace:
     *  GetWindowThreadProcessId(hwndCombo,0)
     *  GetCurrentThreadId()
     *  GetWindowThreadProcessId(hwndCombo, &???)
     *  GetCurrentProcessId()
     */

    /*
     * Setup a property to hold the pointer to the COMBOBOXEX 
     * data structure.
     */
    SetPropA(infoPtr->hwndCombo, COMBOEX_SUBCLASS_PROP, hwnd);
    infoPtr->prevComboWndProc = (WNDPROC)SetWindowLongW(infoPtr->hwndCombo, 
	                        GWL_WNDPROC, (LONG)COMBOEX_ComboWndProc);
    infoPtr->font = SendMessageW (infoPtr->hwndCombo, WM_GETFONT, 0, 0);


    /*
     * Now create our own EDIT control so we can position it.
     * It is created only for CBS_DROPDOWN style
     */
    if ((cs->style & CBS_DROPDOWNLIST) == CBS_DROPDOWN) {
	infoPtr->hwndEdit = CreateWindowExW (0, EDIT, NIL,
		    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | ES_AUTOHSCROLL,
		    0, 0, 0, 0,  /* will set later */
		    infoPtr->hwndCombo, 
		    (HMENU) GetWindowLongW (hwnd, GWL_ID),
		    GetWindowLongW (hwnd, GWL_HINSTANCE),
		    NULL);

	/* native does the following at this point according to trace:
	 *  GetWindowThreadProcessId(hwndEdit,0)
	 *  GetCurrentThreadId()
	 *  GetWindowThreadProcessId(hwndEdit, &???)
	 *  GetCurrentProcessId()
	 */

	/*
	 * Setup a property to hold the pointer to the COMBOBOXEX 
	 * data structure.
	 */
        SetPropA(infoPtr->hwndEdit, COMBOEX_SUBCLASS_PROP, hwnd);
	infoPtr->prevEditWndProc = (WNDPROC)SetWindowLongW(infoPtr->hwndEdit, 
				 GWL_WNDPROC, (LONG)COMBOEX_EditWndProc);
	infoPtr->font = SendMessageW (infoPtr->hwndCombo, WM_GETFONT, 0, 0);
    }

    /*
     * Locate the default font if necessary and then set it in
     * all associated controls
     */
    if (!infoPtr->font) {
	SystemParametersInfoW (SPI_GETICONTITLELOGFONT, sizeof(mylogfont), 
			       &mylogfont, 0);
	infoPtr->font = infoPtr->defaultFont = CreateFontIndirectW (&mylogfont);
    }
    SendMessageW (infoPtr->hwndCombo, WM_SETFONT, (WPARAM)infoPtr->font, 0);
    if (infoPtr->hwndEdit) {
	SendMessageW (infoPtr->hwndEdit, WM_SETFONT, (WPARAM)infoPtr->font, 0);
	SendMessageW (infoPtr->hwndEdit, EM_SETMARGINS, (WPARAM)EC_USEFONTINFO, 0);
    }

    COMBOEX_ReSize (infoPtr);

    /* Above is fairly certain, below is much less certain. */

    GetWindowRect(hwnd, &wnrc1);
    GetClientRect(hwnd, &clrc1);
    GetWindowRect(infoPtr->hwndCombo, &cmbwrc);
    TRACE("EX window=(%d,%d)-(%d,%d) client=(%d,%d)-(%d,%d) CB wnd=(%d,%d)-(%d,%d)\n",
	  wnrc1.left, wnrc1.top, wnrc1.right, wnrc1.bottom,
	  clrc1.left, clrc1.top, clrc1.right, clrc1.bottom,
	  cmbwrc.left, cmbwrc.top, cmbwrc.right, cmbwrc.bottom);
    SetWindowPos(infoPtr->hwndCombo, HWND_TOP, 
		 0, 0, wnrc1.right-wnrc1.left, wnrc1.bottom-wnrc1.top,
		 SWP_NOACTIVATE | SWP_NOREDRAW);

    GetWindowRect(infoPtr->hwndCombo, &cmbwrc);
    TRACE("CB window=(%d,%d)-(%d,%d)\n",
	  cmbwrc.left, cmbwrc.top, cmbwrc.right, cmbwrc.bottom);
    SetWindowPos(hwnd, HWND_TOP, 
		 0, 0, cmbwrc.right-cmbwrc.left, cmbwrc.bottom-cmbwrc.top,
		 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE);

    COMBOEX_AdjustEditPos (infoPtr);

    /*
     * Create an item structure to represent the data in the 
     * EDIT control. It is allocated zero-filled.
     */
    infoPtr->edit = (CBE_ITEMDATA *)COMCTL32_Alloc (sizeof (CBE_ITEMDATA));
    //FIXME: what if allocation failed?

    return 0;
}


static LRESULT COMBOEX_Command (COMBOEX_INFO *infoPtr, WPARAM wParam, LPARAM lParam)
{
    LRESULT lret;
    INT command = HIWORD(wParam);
    CBE_ITEMDATA *item = 0;
    WCHAR wintext[520];
    INT cursel, n, oldItem;
    NMCBEENDEDITW cbeend;
    DWORD oldflags;
    HWND parent = GetParent (infoPtr->hwndSelf);

    TRACE("for command %d\n", command);

    switch (command)
    {
    case CBN_DROPDOWN:
	SetFocus (infoPtr->hwndCombo);
	ShowWindow (infoPtr->hwndEdit, SW_HIDE);
	return SendMessageW (parent, WM_COMMAND, wParam, (LPARAM)infoPtr->hwndSelf);
	
    case CBN_CLOSEUP:
	SendMessageW (parent, WM_COMMAND, wParam, (LPARAM)infoPtr->hwndSelf);
	/*
	 * from native trace of first dropdown after typing in URL in IE4 
	 *  CB_GETCURSEL(Combo)
	 *  GetWindowText(Edit)
	 *  CB_GETCURSEL(Combo)
	 *  CB_GETCOUNT(Combo)
	 *  CB_GETITEMDATA(Combo, n)
	 *  WM_NOTIFY(parent, CBEN_ENDEDITA|W)
	 *  CB_GETCURSEL(Combo)
	 *  CB_SETCURSEL(COMBOEX, n)
	 *  SetFocus(Combo)
	 * the rest is supposition  
	 */
	ShowWindow (infoPtr->hwndEdit, SW_SHOW);
	InvalidateRect (infoPtr->hwndCombo, 0, TRUE);
	InvalidateRect (infoPtr->hwndEdit, 0, TRUE);
	cursel = SendMessageW (infoPtr->hwndCombo, CB_GETCURSEL, 0, 0);
	if (cursel == -1) {
	    /* find match from edit against those in Combobox */
	    GetWindowTextW (infoPtr->hwndEdit, wintext, 520);
	    n = SendMessageW (infoPtr->hwndCombo, CB_GETCOUNT, 0, 0);
	    for (cursel = 0; cursel < n; cursel++){
		item = (CBE_ITEMDATA *)SendMessageW (infoPtr->hwndCombo, 
						     CB_GETITEMDATA, 
						     cursel, 0);
		if ((INT)item == CB_ERR) break;
		if (lstrcmpiW(item->pszText, wintext) == 0) break;
	    }
	    if ((cursel == n) || ((INT)item == CB_ERR)) {
		TRACE("failed to find match??? item=%p cursel=%d\n",
		      item, cursel);
		if (infoPtr->hwndEdit)
		    SetFocus(infoPtr->hwndEdit);
		return 0;
	    }
	}
	else {
	    item = (CBE_ITEMDATA *)SendMessageW (infoPtr->hwndCombo, 
						 CB_GETITEMDATA, 
						 cursel, 0);
	    if ((INT)item == CB_ERR) {
		TRACE("failed to find match??? item=%p cursel=%d\n",
		      item, cursel);
		if (infoPtr->hwndEdit)
		    SetFocus(infoPtr->hwndEdit);
		return 0;
	    }
	}

	/* Save flags for testing and reset them */
	oldflags = infoPtr->flags;
	infoPtr->flags &= ~(WCBE_ACTEDIT | WCBE_EDITCHG);

	if (oldflags & WCBE_ACTEDIT) {
	    cbeend.fChanged = (oldflags & WCBE_EDITCHG);
	    cbeend.iNewSelection = SendMessageW (infoPtr->hwndCombo,
						 CB_GETCURSEL, 0, 0);
	    cbeend.iWhy = CBENF_DROPDOWN;

	    if (COMBOEX_NotifyEndEdit (infoPtr, &cbeend, item->pszText)) {
		/* abort the change */
		TRACE("Notify requested abort of change\n");
		return 0;
	    }
	}

	/* if selection has changed the set the new current selection */
	cursel = SendMessageW (infoPtr->hwndCombo, CB_GETCURSEL, 0, 0);
	if ((oldflags & WCBE_EDITCHG) || (cursel != infoPtr->selected)) {
	    infoPtr->selected = cursel;
	    SendMessageW (infoPtr->hwndSelf, CB_SETCURSEL, cursel, 0);
	    SetFocus(infoPtr->hwndCombo);
	}
	return 0;

    case CBN_SELCHANGE:
	/*
	 * CB_GETCURSEL(Combo)
	 * CB_GETITEMDATA(Combo)   < simulated by COMBOEX_FindItem
	 * lstrlenA
	 * WM_SETTEXT(Edit)
	 * WM_GETTEXTLENGTH(Edit)
	 * WM_GETTEXT(Edit)
	 * EM_SETSEL(Edit, 0,0)
	 * WM_GETTEXTLENGTH(Edit)
	 * WM_GETTEXT(Edit)
	 * EM_SETSEL(Edit, 0,len)
	 * return WM_COMMAND to parent
	 */
	oldItem = SendMessageW (infoPtr->hwndCombo, CB_GETCURSEL, 0, 0);
	if (!(item = COMBOEX_FindItem(infoPtr, oldItem))) {
	    ERR("item %d not found. Problem!\n", oldItem);
	    break;
	}
	infoPtr->selected = oldItem;
	COMBOEX_SetEditText (infoPtr, item);
	return SendMessageW (parent, WM_COMMAND, wParam, (LPARAM)infoPtr->hwndSelf);

    case CBN_SELENDOK:
	/* 
	 * We have to change the handle since we are the control
	 * issuing the message. IE4 depends on this.
	 */
	return SendMessageW (parent, WM_COMMAND, wParam, (LPARAM)infoPtr->hwndSelf);

    case CBN_KILLFOCUS:
	/*
	 * from native trace:
	 *
	 *  pass to parent
	 *  WM_GETTEXT(Edit, 104)
	 *  CB_GETCURSEL(Combo) rets -1
	 *  WM_NOTIFY(CBEN_ENDEDITA) with CBENF_KILLFOCUS
	 *  CB_GETCURSEL(Combo)
	 *  InvalidateRect(Combo, 0, 0)
	 *  return 0
	 */
	SendMessageW (parent, WM_COMMAND, wParam, (LPARAM)infoPtr->hwndSelf);
	if (infoPtr->flags & WCBE_ACTEDIT) {
	    GetWindowTextW (infoPtr->hwndEdit, wintext, 260);
	    cbeend.fChanged = (infoPtr->flags & WCBE_EDITCHG);
	    cbeend.iNewSelection = SendMessageW (infoPtr->hwndCombo,
						 CB_GETCURSEL, 0, 0);
	    cbeend.iWhy = CBENF_KILLFOCUS;

	    infoPtr->flags &= ~(WCBE_ACTEDIT | WCBE_EDITCHG);
	    if (COMBOEX_NotifyEndEdit (infoPtr, &cbeend, wintext)) {
		/* abort the change */
		TRACE("Notify requested abort of change\n");
		return 0;
	    }
	}
	/* possible CB_GETCURSEL */
	InvalidateRect (infoPtr->hwndCombo, 0, 0);
	return 0;

    default:
	/* 
	 * We have to change the handle since we are the control
	 * issuing the message. IE4 depends on this.
	 * We also need to set the focus back to the Edit control
	 * after passing the command to the parent of the ComboEx.
	 */
	lret = SendMessageW (parent, WM_COMMAND, wParam, (LPARAM)infoPtr->hwndSelf);
	if (infoPtr->hwndEdit)
	    SetFocus(infoPtr->hwndEdit);
	return lret;
    }
    return 0;
}


static BOOL COMBOEX_WM_DeleteItem (COMBOEX_INFO *infoPtr, DELETEITEMSTRUCT *dis)
{
    CBE_ITEMDATA *item, *olditem;
    NMCOMBOBOXEXW nmcit;
    INT i;

    TRACE("CtlType=%08x, CtlID=%08x, itemID=%08x, hwnd=%x, data=%08lx\n",
	  dis->CtlType, dis->CtlID, dis->itemID, dis->hwndItem, dis->itemData);

    if (dis->itemID >= infoPtr->nb_items) return FALSE;

    olditem = infoPtr->items;
    i = infoPtr->nb_items - 1;

    if (i == dis->itemID) {
	infoPtr->items = infoPtr->items->next;
    }
    else {
	item = olditem;
	i--;

	/* find the prior item in the list */
	while (item->next && (i > dis->itemID)) {
	    item = (CBE_ITEMDATA *)item->next;
	    i--;
	}
	if (!item->next || (i != dis->itemID)) {
	    ERR("COMBOBOXEX item structures broken. Please report!\n");
	    return FALSE;
	}
	olditem = item->next;
	item->next = (CBE_ITEMDATA *)((CBE_ITEMDATA *)item->next)->next;
    }
    infoPtr->nb_items--;

    memset (&nmcit.ceItem, 0, sizeof(nmcit.ceItem));
    COMBOEX_CopyItem (olditem, &nmcit.ceItem);
    COMBOEX_NotifyItem (infoPtr, CBEN_DELETEITEM, &nmcit);

    if (olditem->pszText) COMCTL32_Free(olditem->pszText);
    COMCTL32_Free(olditem);

    return TRUE;
}


static LRESULT COMBOEX_DrawItem (COMBOEX_INFO *infoPtr, DRAWITEMSTRUCT *dis)
{
    WCHAR nil[] = { 0 };
    CBE_ITEMDATA *item = 0;
    SIZE txtsize;
    RECT rect;
    LPCWSTR str = nil;
    int drawimage, drawstate;
    UINT xbase, x, y;
    UINT xioff = 0;               /* size and spacer of image if any */
    IMAGEINFO iinfo;
    INT len;
    COLORREF nbkc, ntxc, bkc, txc;

    if (!IsWindowEnabled(infoPtr->hwndCombo)) return 0;

    /* dump the DRAWITEMSTRUCT if tracing "comboex" but not "message" */
    if (!TRACE_ON(message)) {
	TRACE("DRAWITEMSTRUCT: CtlType=0x%08x CtlID=0x%08x\n", 
	      dis->CtlType, dis->CtlID);
	TRACE("itemID=0x%08x itemAction=0x%08x itemState=0x%08x\n", 
	      dis->itemID, dis->itemAction, dis->itemState);
	TRACE("hWnd=0x%04x hDC=0x%04x (%d,%d)-(%d,%d) itemData=0x%08lx\n",
	      dis->hwndItem, dis->hDC, dis->rcItem.left, 
	      dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom, 
	      dis->itemData);
    }

    /* MSDN says:                                                       */
    /*     "itemID - Specifies the menu item identifier for a menu      */
    /*      item or the index of the item in a list box or combo box.   */
    /*      For an empty list box or combo box, this member can be -1.  */
    /*      This allows the application to draw only the focus          */
    /*      rectangle at the coordinates specified by the rcItem        */
    /*      member even though there are no items in the control.       */
    /*      This indicates to the user whether the list box or combo    */
    /*      box has the focus. How the bits are set in the itemAction   */
    /*      member determines whether the rectangle is to be drawn as   */
    /*      though the list box or combo box has the focus.             */
    if (dis->itemID == 0xffffffff) {
	if ( ( (dis->itemAction & ODA_FOCUS) && (dis->itemState & ODS_SELECTED)) ||
	     ( (dis->itemAction & (ODA_SELECT | ODA_DRAWENTIRE)) && (dis->itemState & ODS_FOCUS) ) ) { 

	    TRACE("drawing item -1 special focus, rect=(%d,%d)-(%d,%d)\n",
		  dis->rcItem.left, dis->rcItem.top,
		  dis->rcItem.right, dis->rcItem.bottom);
	}
	else if ((dis->CtlType == ODT_COMBOBOX) && 
		 (dis->itemAction == ODA_DRAWENTIRE)) {
	    /* draw of edit control data */

	    /* testing */
	    {
		RECT exrc, cbrc, edrc;
		GetWindowRect (infoPtr->hwndSelf, &exrc);
		GetWindowRect (infoPtr->hwndCombo, &cbrc);
		edrc.left=edrc.top=edrc.right=edrc.bottom=-1;
		if (infoPtr->hwndEdit)
		    GetWindowRect (infoPtr->hwndEdit, &edrc);
		TRACE("window rects ex=(%d,%d)-(%d,%d), cb=(%d,%d)-(%d,%d), ed=(%d,%d)-(%d,%d)\n",
		      exrc.left, exrc.top, exrc.right, exrc.bottom,
		      cbrc.left, cbrc.top, cbrc.right, cbrc.bottom,
		      edrc.left, edrc.top, edrc.right, edrc.bottom);
	    }
	}
	else {
	    ERR("NOT drawing item  -1 special focus, rect=(%d,%d)-(%d,%d), action=%08x, state=%08x\n",
		dis->rcItem.left, dis->rcItem.top,
		dis->rcItem.right, dis->rcItem.bottom,
		dis->itemAction, dis->itemState);
	    return 0;
	}
    }

    /* If draw item is -1 (edit control) setup the item pointer */
    if (dis->itemID == 0xffffffff) {
	item = infoPtr->edit;

	if (infoPtr->hwndEdit) {
	    INT len;

	    /* free previous text of edit item */
	    if (item->pszText) {
		COMCTL32_Free(item->pszText);
		item->pszText = 0;
		item->mask &= ~CBEIF_TEXT;
	    }
	    if( (len = GetWindowTextLengthW(infoPtr->hwndEdit)) ) {
		item->mask |= CBEIF_TEXT;
		item->pszText = (LPWSTR)COMCTL32_Alloc ((len + 1)*sizeof(WCHAR));
		//FIXME: what if alloc failed?
		GetWindowTextW(infoPtr->hwndEdit, item->pszText, len+1);
	    
	       TRACE("edit control hwndEdit=%0x, text len=%d str=%s\n",
		     infoPtr->hwndEdit, len, debugstr_w(item->pszText));
	    }
	}
    }


    /* if the item pointer is not set, then get the data and locate it */
    if (!item) {
	item = (CBE_ITEMDATA *)SendMessageW (infoPtr->hwndCombo,
			     CB_GETITEMDATA, (WPARAM)dis->itemID, 0);
	if (item == (CBE_ITEMDATA *)CB_ERR) {
	    ERR("invalid item for id %d \n", dis->itemID);
	    return 0;
	}
    }

    if (TRACE_ON(comboex)) COMBOEX_DumpItem (item);

    xbase = CBE_STARTOFFSET;
    if ((item->mask & CBEIF_INDENT) && (dis->itemState & ODS_COMBOEXLBOX))
        xbase += (item->iIndent * CBE_INDENT);
    if (item->mask & CBEIF_IMAGE) {
	ImageList_GetImageInfo(infoPtr->himl, item->iImage, &iinfo);
	xioff = (iinfo.rcImage.right - iinfo.rcImage.left + CBE_SEP);
    }

    /* setup pointer to text to be drawn */
    if (item && (item->mask & CBEIF_TEXT) && item->pszText)
	str = item->pszText;
    else
	str = nil;

    len = strlenW (str);
    GetTextExtentPoint32W (dis->hDC, str, len, &txtsize);
    
    if (dis->itemAction & (ODA_SELECT | ODA_DRAWENTIRE)) {
        drawimage = -1;
	drawstate = ILD_NORMAL;
	if (!(infoPtr->dwExtStyle & CBES_EX_NOEDITIMAGE)) {
	    if (item->mask & CBEIF_IMAGE) 
		drawimage = item->iImage;
	    if (dis->itemState & ODS_COMBOEXLBOX) {
		/* drawing listbox entry */
		if (dis->itemState & ODS_SELECTED) {
		    if (item->mask & CBEIF_SELECTEDIMAGE)
			drawimage = item->iSelectedImage;
		    drawstate = ILD_SELECTED;
		}
	    }
	    else {
		/* drawing combo/edit entry */
		if (IsWindowVisible(infoPtr->hwndEdit)) {
		    /* if we have an edit control, the slave the 
                     * selection state to the Edit focus state 
		     */
		    if (infoPtr->flags & WCBE_EDITFOCUSED) {
			if (item->mask & CBEIF_SELECTEDIMAGE)
			    drawimage = item->iSelectedImage;
			drawstate = ILD_SELECTED;
		    }
		}
		else {
		    /* if we don't have an edit control, use 
		     * the requested state.
		     */
		    if (dis->itemState & ODS_SELECTED) {
			if (item->mask & CBEIF_SELECTEDIMAGE)
			    drawimage = item->iSelectedImage;
			drawstate = ILD_SELECTED;
		    }
		}
	    }
	}
	if (drawimage != -1) {
	    TRACE("drawing image state=%d\n", dis->itemState & ODS_SELECTED);
	    ImageList_Draw (infoPtr->himl, drawimage, dis->hDC, 
			    xbase, dis->rcItem.top, drawstate);
	}

	/* now draw the text */
	if (!IsWindowVisible (infoPtr->hwndEdit)) {
	nbkc = GetSysColor ((dis->itemState & ODS_SELECTED) ?
			    COLOR_HIGHLIGHT : COLOR_WINDOW);
	bkc = SetBkColor (dis->hDC, nbkc);
	ntxc = GetSysColor ((dis->itemState & ODS_SELECTED) ?
			    COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
	txc = SetTextColor (dis->hDC, ntxc);
	x = xbase + xioff;
	y = dis->rcItem.top +
	    (dis->rcItem.bottom - dis->rcItem.top - txtsize.cy) / 2;
	rect.left = x;
	rect.right = x + txtsize.cx;
	rect.top = dis->rcItem.top + 1;
	rect.bottom = dis->rcItem.bottom - 1;
	TRACE("drawing item %d text, rect=(%d,%d)-(%d,%d)\n",
	      dis->itemID, rect.left, rect.top, rect.right, rect.bottom);
	ExtTextOutW (dis->hDC, x, y, ETO_OPAQUE | ETO_CLIPPED,
		     &rect, str, len, 0);
	SetBkColor (dis->hDC, bkc);
	SetTextColor (dis->hDC, txc);
	}
    }
    
    if (dis->itemAction & ODA_FOCUS) {
	rect.left = xbase + xioff - 1;
	rect.right = rect.left + txtsize.cx + 2;
	rect.top = dis->rcItem.top;
	rect.bottom = dis->rcItem.bottom;
	DrawFocusRect(dis->hDC, &rect);
    }

    return 0;
}


static LRESULT COMBOEX_Destroy (COMBOEX_INFO *infoPtr)
{
    if (infoPtr->hwndCombo)
	DestroyWindow (infoPtr->hwndCombo);

    if (infoPtr->edit) {
	COMCTL32_Free (infoPtr->edit);
	infoPtr->edit = 0;
    }

    if (infoPtr->items) {
        CBE_ITEMDATA *this, *next;

	this = infoPtr->items;
	while (this) {
	    next = (CBE_ITEMDATA *)this->next;
	    if ((this->mask & CBEIF_TEXT) && this->pszText)
	        COMCTL32_Free (this->pszText);
	    COMCTL32_Free (this);
	    this = next;
	}
	infoPtr->items = 0;
    }

    if (infoPtr->defaultFont) 
	DeleteObject (infoPtr->defaultFont);

    /* free comboex info data */
    COMCTL32_Free (infoPtr);
    SetWindowLongW (infoPtr->hwndSelf, 0, 0);
    return 0;
}


static LRESULT COMBOEX_MeasureItem (COMBOEX_INFO *infoPtr, MEASUREITEMSTRUCT *mis)
{
    SIZE mysize;
    HDC hdc;

    hdc = GetDC (0);
    GetTextExtentPointA (hdc, "W", 1, &mysize);
    ReleaseDC (0, hdc);
    mis->itemHeight = mysize.cy + CBE_EXTRA;

    TRACE("adjusted height hwnd=%08x, height=%d\n",
	  infoPtr->hwndSelf, mis->itemHeight);

    return 0;
}


static LRESULT COMBOEX_NCCreate (HWND hwnd)
{
    /* WARNING: The COMBOEX_INFO structure is not yet created */
    DWORD oldstyle, newstyle;

    oldstyle = (DWORD)GetWindowLongW (hwnd, GWL_STYLE);
    newstyle = oldstyle & ~(WS_VSCROLL | WS_HSCROLL);
    if (newstyle != oldstyle) {
	TRACE("req style %08lx, reseting style %08lx\n",
	      oldstyle, newstyle);
	SetWindowLongW (hwnd, GWL_STYLE, newstyle);
    }
    return 1;
}


static LRESULT COMBOEX_NotifyFormat (COMBOEX_INFO *infoPtr, LPARAM lParam)
{
    if (lParam == NF_REQUERY) {
	INT i = SendMessageW(GetParent (infoPtr->hwndSelf),
			 WM_NOTIFYFORMAT, infoPtr->hwndSelf, NF_QUERY);
	if ((i < NFR_ANSI) || (i > NFR_UNICODE)) {
	    ERR("wrong response to WM_NOTIFYFORMAT (%d), set to ANSI\n", i);
	    i = NFR_ANSI;
	}
	infoPtr->NtfUnicode = (i == NFR_UNICODE) ? 1 : 0;
	return (LRESULT)i;
    }
    return (LRESULT)(infoPtr->unicode ? NFR_UNICODE : NFR_ANSI);
}


static LRESULT COMBOEX_Size (COMBOEX_INFO *infoPtr, INT width, INT height)
{
    TRACE("(width=%d, height=%d)\n", width, height); 

    MoveWindow (infoPtr->hwndCombo, 0, 0, width, height, TRUE);

    COMBOEX_AdjustEditPos (infoPtr);

    return 0;
}


static LRESULT COMBOEX_WindowPosChanging (COMBOEX_INFO *infoPtr, WINDOWPOS *wp)
{
    RECT cbx_wrect, cbx_crect, cb_wrect;
    UINT width, height;

    GetWindowRect (infoPtr->hwndSelf, &cbx_wrect);
    GetClientRect (infoPtr->hwndSelf, &cbx_crect);
    GetWindowRect (infoPtr->hwndCombo, &cb_wrect);

    /* width is winpos value + border width of comboex */
    width = wp->cx
	    + (cbx_wrect.right-cbx_wrect.left) 
            - (cbx_crect.right-cbx_crect.left); 

    TRACE("winpos=(%d,%d %dx%d) flags=0x%08x\n",
	  wp->x, wp->y, wp->cx, wp->cy, wp->flags);
    TRACE("EX window=(%d,%d)-(%d,%d), client=(%d,%d)-(%d,%d)\n",
	  cbx_wrect.left, cbx_wrect.top, cbx_wrect.right, cbx_wrect.bottom,
	  cbx_crect.left, cbx_crect.top, cbx_crect.right, cbx_crect.bottom);
    TRACE("CB window=(%d,%d)-(%d,%d), EX setting=(0,0)-(%d,%d)\n",
	  cb_wrect.left, cb_wrect.top, cb_wrect.right, cb_wrect.bottom,
	  width, cb_wrect.bottom-cb_wrect.top);

    if (width) SetWindowPos (infoPtr->hwndCombo, HWND_TOP, 0, 0,
			     width,
			     cb_wrect.bottom-cb_wrect.top,
			     SWP_NOACTIVATE);

    GetWindowRect (infoPtr->hwndCombo, &cb_wrect);

    /* height is combo window height plus border width of comboex */
    height =   (cb_wrect.bottom-cb_wrect.top)
	     + (cbx_wrect.bottom-cbx_wrect.top)
             - (cbx_crect.bottom-cbx_crect.top);
    if (wp->cy < height) wp->cy = height;
    if (infoPtr->hwndEdit) {
	COMBOEX_AdjustEditPos (infoPtr);
	InvalidateRect (infoPtr->hwndCombo, 0, TRUE);
    }

    return 0;
}


static LRESULT WINAPI
COMBOEX_EditWndProc (HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HWND hwndComboex = (HWND)GetPropA(hwnd, COMBOEX_SUBCLASS_PROP);
    COMBOEX_INFO *infoPtr = COMBOEX_GetInfoPtr (hwndComboex);
    NMCBEENDEDITW cbeend;
    WCHAR edit_text[260];
    COLORREF obkc;
    HDC hDC;
    RECT rect;
    LRESULT lret;

    TRACE("hwnd=%x msg=%x wparam=%x lParam=%lx, info_ptr=%p\n", 
	  hwnd, uMsg, wParam, lParam, infoPtr);

    if (!infoPtr) return 0;

    switch (uMsg)
    {

	case WM_CHAR:
	    /* handle (ignore) the return character */
	    if (wParam == VK_RETURN) return 0;
	    /* all other characters pass into the real Edit */
	    return CallWindowProcW (infoPtr->prevEditWndProc, 
				   hwnd, uMsg, wParam, lParam);

	case WM_ERASEBKGND:
	    /*
	     * The following was determined by traces of the native 
	     */
            hDC = (HDC) wParam;
	    obkc = SetBkColor (hDC, GetSysColor (COLOR_WINDOW));
            GetClientRect (hwnd, &rect);
	    TRACE("erasing (%d,%d)-(%d,%d)\n",
		  rect.left, rect.top, rect.right, rect.bottom);
	    ExtTextOutW (hDC, 0, 0, ETO_OPAQUE, &rect, 0, 0, 0);
            SetBkColor (hDC, obkc);
	    return CallWindowProcW (infoPtr->prevEditWndProc, 
				   hwnd, uMsg, wParam, lParam);

	case WM_KEYDOWN: {
	    INT oldItem, selected, step = 1;
	    CBE_ITEMDATA *item;

	    switch ((INT)wParam)
	    {
	    case VK_ESCAPE:
		/* native version seems to do following for COMBOEX */
		/*
		 *   GetWindowTextA(Edit,&?, 0x104)             x
		 *   CB_GETCURSEL to Combo rets -1              x
		 *   WM_NOTIFY to COMBOEX parent (rebar)        x
		 *     (CBEN_ENDEDIT{A|W} 
		 *      fChanged = FALSE                        x
		 *      inewSelection = -1                      x
		 *      txt="www.hoho"                          x
		 *      iWhy = 3                                x
		 *   CB_GETCURSEL to Combo rets -1              x
		 *   InvalidateRect(Combo, 0)                   x
		 *   WM_SETTEXT to Edit                         x
		 *   EM_SETSEL to Edit (0,0)                    x
		 *   EM_SETSEL to Edit (0,-1)                   x
		 *   RedrawWindow(Combo, 0, 0, 5)               x
		 */
		TRACE("special code for VK_ESCAPE\n");

		GetWindowTextW (infoPtr->hwndEdit, edit_text, 260);

		infoPtr->flags &= ~(WCBE_ACTEDIT | WCBE_EDITCHG);
		cbeend.fChanged = FALSE;
		cbeend.iNewSelection = SendMessageW (infoPtr->hwndCombo,
						     CB_GETCURSEL, 0, 0);
		cbeend.iWhy = CBENF_ESCAPE;

		if (COMBOEX_NotifyEndEdit (infoPtr, &cbeend, edit_text)) {
		    /* abort the change */
		    TRACE("Notify requested abort of change\n");
		    return 0;
		}
		oldItem = SendMessageW (infoPtr->hwndCombo, CB_GETCURSEL, 0, 0);
		InvalidateRect (infoPtr->hwndCombo, 0, 0);
		if (!(item = COMBOEX_FindItem(infoPtr, oldItem))) {
		    ERR("item %d not found. Problem!\n", oldItem);
		    break;
		}
		infoPtr->selected = oldItem;		  
		COMBOEX_SetEditText (infoPtr, item);
		RedrawWindow (infoPtr->hwndCombo, 0, 0, RDW_ERASE | 
			      RDW_INVALIDATE);
		break;

	    case VK_RETURN:
		/* native version seems to do following for COMBOEX */
		/*
		 *   GetWindowTextA(Edit,&?, 0x104)             x
		 *   CB_GETCURSEL to Combo  rets -1             x
		 *   CB_GETCOUNT to Combo  rets 0
		 *   if >0 loop 
		 *       CB_GETITEMDATA to match
		 * *** above 3 lines simulated by FindItem      x  
		 *   WM_NOTIFY to COMBOEX parent (rebar)        x
		 *     (CBEN_ENDEDIT{A|W}                       x
		 *        fChanged = TRUE (-1)                  x
		 *        iNewSelection = -1 or selected        x
		 *        txt=                                  x
		 *        iWhy = 2 (CBENF_RETURN)               x
		 *   CB_GETCURSEL to Combo  rets -1             x
		 *   if -1 send CB_SETCURSEL to Combo -1        x
		 *   InvalidateRect(Combo, 0, 0)                x
		 *   SetFocus(Edit)                             x
		 *   CallWindowProc(406615a8, Edit, 0x100, 0xd, 0x1c0001)
		 */

		TRACE("special code for VK_RETURN\n");

		GetWindowTextW (infoPtr->hwndEdit, edit_text, 260);

		infoPtr->flags &= ~(WCBE_ACTEDIT | WCBE_EDITCHG);
		selected = SendMessageW (infoPtr->hwndCombo,
					 CB_GETCURSEL, 0, 0);

		if (selected != -1) {
		    item = COMBOEX_FindItem (infoPtr, selected);
		    TRACE("handling VK_RETURN, selected = %d, selected_text=%s\n",
			  selected, debugstr_w(item->pszText));
		    TRACE("handling VK_RETURN, edittext=%s\n",
			  debugstr_w(edit_text));
		    if (lstrcmpiW (item->pszText, edit_text)) {
			/* strings not equal -- indicate edit has changed */
			selected = -1;
		    }
		}

		cbeend.iNewSelection = selected;
		cbeend.fChanged = TRUE; 
		cbeend.iWhy = CBENF_RETURN;
		if (COMBOEX_NotifyEndEdit (infoPtr, &cbeend, edit_text)) {
		    /* abort the change, restore previous */
		    TRACE("Notify requested abort of change\n");
		    COMBOEX_SetEditText (infoPtr, infoPtr->edit);
		    RedrawWindow (infoPtr->hwndCombo, 0, 0, RDW_ERASE | 
				  RDW_INVALIDATE);
		    return 0;
		}
		oldItem = SendMessageW (infoPtr->hwndCombo,CB_GETCURSEL, 0, 0);
		if (oldItem != -1) {
		    /* if something is selected, then deselect it */
		    SendMessageW (infoPtr->hwndCombo, CB_SETCURSEL, 
				  (WPARAM)-1, 0);
		}
		InvalidateRect (infoPtr->hwndCombo, 0, 0);
		SetFocus(infoPtr->hwndEdit);
		break;

	    case VK_UP:
		step = -1;
	    case VK_DOWN:
		/* by default, step is 1 */
		oldItem = SendMessageW (infoPtr->hwndSelf, CB_GETCURSEL, 0, 0);
		if (oldItem >= 0 && oldItem + step >= 0)
		    SendMessageW (infoPtr->hwndSelf, CB_SETCURSEL, oldItem + step, 0);
	    	return 0;
	    default:
		return CallWindowProcW (infoPtr->prevEditWndProc,
				       hwnd, uMsg, wParam, lParam);
	    }
	    return 0;
            }

	case WM_SETFOCUS:
	    /* remember the focus to set state of icon */
	    lret = CallWindowProcW (infoPtr->prevEditWndProc, 
				   hwnd, uMsg, wParam, lParam);
	    infoPtr->flags |= WCBE_EDITFOCUSED;
	    return lret;

	case WM_KILLFOCUS:
	    /* 
	     * do NOTIFY CBEN_ENDEDIT with CBENF_KILLFOCUS
	     */
	    infoPtr->flags &= ~WCBE_EDITFOCUSED;
	    if (infoPtr->flags & WCBE_ACTEDIT) {
		infoPtr->flags &= ~(WCBE_ACTEDIT | WCBE_EDITCHG);

		GetWindowTextW (infoPtr->hwndEdit, edit_text, 260);
		cbeend.fChanged = FALSE;
		cbeend.iNewSelection = SendMessageW (infoPtr->hwndCombo,
						     CB_GETCURSEL, 0, 0);
		cbeend.iWhy = CBENF_KILLFOCUS;

		COMBOEX_NotifyEndEdit (infoPtr, &cbeend, edit_text);
	    }
	    /* fall through */

	default:
	    return CallWindowProcW (infoPtr->prevEditWndProc, 
				   hwnd, uMsg, wParam, lParam);
    }
    return 0;
}


static LRESULT WINAPI
COMBOEX_ComboWndProc (HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HWND hwndComboex = (HWND)GetPropA(hwnd, COMBOEX_SUBCLASS_PROP);
    COMBOEX_INFO *infoPtr = COMBOEX_GetInfoPtr (hwndComboex);
    NMCBEENDEDITW cbeend;
    NMMOUSE nmmse;
    COLORREF obkc;
    HDC hDC;
    HWND focusedhwnd;
    RECT rect;
    WCHAR edit_text[260];

    TRACE("hwnd=%x msg=%x wparam=%x lParam=%lx, info_ptr=%p\n", 
	  hwnd, uMsg, wParam, lParam, infoPtr);

    if (!infoPtr) return 0;

    switch (uMsg)
    {

    case CB_FINDSTRINGEXACT:
	    return COMBOEX_FindStringExact (infoPtr, (INT)wParam, (LPCSTR)lParam);

    case WM_DRAWITEM:
	    /*
	     * The only way this message should come is from the
	     * child Listbox issuing the message. Flag this so
	     * that ComboEx knows this is listbox.
	     */
	    ((DRAWITEMSTRUCT *)lParam)->itemState |= ODS_COMBOEXLBOX;
	    return CallWindowProcW (infoPtr->prevComboWndProc, 
				   hwnd, uMsg, wParam, lParam);

    case WM_ERASEBKGND:
	    /*
	     * The following was determined by traces of the native 
	     */
            hDC = (HDC) wParam;
	    obkc = SetBkColor (hDC, GetSysColor (COLOR_WINDOW));
            GetClientRect (hwnd, &rect);
	    TRACE("erasing (%d,%d)-(%d,%d)\n",
		  rect.left, rect.top, rect.right, rect.bottom);
	    ExtTextOutW (hDC, 0, 0, ETO_OPAQUE, &rect, 0, 0, 0);
            SetBkColor (hDC, obkc);
	    return CallWindowProcW (infoPtr->prevComboWndProc, 
				   hwnd, uMsg, wParam, lParam);

    case WM_SETCURSOR:
	    /*
	     *  WM_NOTIFY to comboex parent (rebar)
	     *   with NM_SETCURSOR with extra words of 0,0,0,0,0x02010001
	     *  CallWindowProc (previous)
	     */
	    nmmse.dwItemSpec = 0;
	    nmmse.dwItemData = 0;
	    nmmse.pt.x = 0;
	    nmmse.pt.y = 0;
	    nmmse.dwHitInfo = lParam;
	    COMBOEX_Notify (infoPtr, NM_SETCURSOR, (NMHDR *)&nmmse);
	    return CallWindowProcW (infoPtr->prevComboWndProc, 
				   hwnd, uMsg, wParam, lParam);

    case WM_COMMAND:
	    switch (HIWORD(wParam)) {

	    case EN_UPDATE:
		/* traces show that COMBOEX does not issue CBN_EDITUPDATE
		 * on the EN_UPDATE
		 */
		return 0;

	    case EN_KILLFOCUS:
		/*
		 * Native does:
		 *
		 *  GetFocus() retns AA
		 *  GetWindowTextA(Edit)
		 *  CB_GETCURSEL(Combo) (got -1)
		 *  WM_NOTIFY(CBEN_ENDEDITA) with CBENF_KILLFOCUS
		 *  CB_GETCURSEL(Combo) (got -1)
		 *  InvalidateRect(Combo, 0, 0)
		 *  WM_KILLFOCUS(Combo, AA)
		 *  return 0;
		 */
		focusedhwnd = GetFocus();
		if (infoPtr->flags & WCBE_ACTEDIT) {
		    GetWindowTextW (infoPtr->hwndEdit, edit_text, 260);
		    cbeend.fChanged = (infoPtr->flags & WCBE_EDITCHG);
		    cbeend.iNewSelection = SendMessageW (infoPtr->hwndCombo,
							 CB_GETCURSEL, 0, 0);
		    cbeend.iWhy = CBENF_KILLFOCUS;

		    infoPtr->flags &= ~(WCBE_ACTEDIT | WCBE_EDITCHG);
		    if (COMBOEX_NotifyEndEdit (infoPtr, &cbeend, edit_text)) {
			/* abort the change */
			TRACE("Notify requested abort of change\n");
			return 0;
		    }
		}
		/* possible CB_GETCURSEL */
		InvalidateRect (infoPtr->hwndCombo, 0, 0);
		if (focusedhwnd)
		    SendMessageW (infoPtr->hwndCombo, WM_KILLFOCUS,
				  (WPARAM)focusedhwnd, 0); 
		return 0;

	    case EN_SETFOCUS: {
		/* 
		 * For EN_SETFOCUS this issues the same calls and messages
		 *  as the native seems to do.
		 *
		 * for some cases however native does the following:
		 *   (noticed after SetFocus during LBUTTONDOWN on
		 *    on dropdown arrow)
		 *  WM_GETTEXTLENGTH (Edit);
		 *  WM_GETTEXT (Edit, len+1, str);
		 *  EM_SETSEL (Edit, 0, 0);
		 *  WM_GETTEXTLENGTH (Edit);
		 *  WM_GETTEXT (Edit, len+1, str);
		 *  EM_SETSEL (Edit, 0, len);
		 *  WM_NOTIFY (parent, CBEN_BEGINEDIT)
		 */
		NMHDR hdr;

		SendMessageW (infoPtr->hwndEdit, EM_SETSEL, 0, 0);
		SendMessageW (infoPtr->hwndEdit, EM_SETSEL, 0, -1);
		COMBOEX_Notify (infoPtr, CBEN_BEGINEDIT, &hdr);
		infoPtr->flags |= WCBE_ACTEDIT;
		infoPtr->flags &= ~WCBE_EDITCHG; /* no change yet */
		return 0;
	        }

	    case EN_CHANGE: {
		/* 
		 * For EN_CHANGE this issues the same calls and messages
		 *  as the native seems to do.
		 */
		WCHAR edit_text[260];
		WCHAR *lastwrk;
		INT selected, cnt;
		CBE_ITEMDATA *item;

		selected = SendMessageW (infoPtr->hwndCombo,
					 CB_GETCURSEL, 0, 0);

		/* lstrlenA( lastworkingURL ) */

		GetWindowTextW (infoPtr->hwndEdit, edit_text, 260);
		if (selected == -1) {
		    lastwrk = infoPtr->edit->pszText;
		    cnt = lstrlenW (lastwrk);
		    if (cnt >= 259) cnt = 259;
		}
		else {
		    item = COMBOEX_FindItem (infoPtr, selected);
		    cnt = lstrlenW (item->pszText);
		    lastwrk = item->pszText;
		    if (cnt >= 259) cnt = 259;
		}

		TRACE("handling EN_CHANGE, selected = %d, selected_text=%s\n",
		      selected, debugstr_w(lastwrk));
		TRACE("handling EN_CHANGE, edittext=%s\n",
		      debugstr_w(edit_text));

		/* lstrcmpiW is between lastworkingURL and GetWindowText */

		if (lstrcmpiW (lastwrk, edit_text)) {
		    /* strings not equal -- indicate edit has changed */
		    infoPtr->flags |= WCBE_EDITCHG;
		}
		SendMessageW ( GetParent(infoPtr->hwndSelf), WM_COMMAND,
			       MAKEWPARAM(GetDlgCtrlID (infoPtr->hwndSelf),
					  CBN_EDITCHANGE),
			       infoPtr->hwndSelf);
		return 0;
	        }

	    case LBN_SELCHANGE:
		/*
		 * Therefore from traces there is no additional code here
		 */

		/*
		 * Using native COMCTL32 gets the following:
		 *  1 == SHDOCVW.DLL  issues call/message
		 *  2 == COMCTL32.DLL  issues call/message
		 *  3 == WINE  issues call/message
		 *
		 *
		 * for LBN_SELCHANGE:
		 *    1  CB_GETCURSEL(ComboEx)
		 *    1  CB_GETDROPPEDSTATE(ComboEx)
		 *    1  CallWindowProc( *2* for WM_COMMAND(LBN_SELCHANGE)
		 *    2  CallWindowProc( *3* for WM_COMMAND(LBN_SELCHANGE)
		 **   call CBRollUp( xxx, TRUE for LBN_SELCHANGE, TRUE)
		 *    3  WM_COMMAND(ComboEx, CBN_SELENDOK)
		 *      WM_USER+49(ComboLB, 1,0)  <=============!!!!!!!!!!!
		 *    3  ShowWindow(ComboLB, SW_HIDE)
		 *    3  RedrawWindow(Combo,  RDW_UPDATENOW)
		 *    3  WM_COMMAND(ComboEX, CBN_CLOSEUP)
		 **   end of CBRollUp
		 *    3  WM_COMMAND(ComboEx, CBN_SELCHANGE)  (echo to parent)
		 *    ?  LB_GETCURSEL              <==|
		 *    ?  LB_GETTEXTLEN                |
		 *    ?  LB_GETTEXT                   | Needs to be added to
		 *    ?  WM_CTLCOLOREDIT(ComboEx)     | Combo processing
		 *    ?  LB_GETITEMDATA               |
		 *    ?  WM_DRAWITEM(ComboEx)      <==|
		 */
	    default:
		break;
	    }/* fall through */
    default:
	    return CallWindowProcW (infoPtr->prevComboWndProc, 
				   hwnd, uMsg, wParam, lParam);
    }
    return 0;
}


static LRESULT WINAPI
COMBOEX_WindowProc (HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    COMBOEX_INFO *infoPtr = COMBOEX_GetInfoPtr (hwnd);

    TRACE("hwnd=%x msg=%x wparam=%x lParam=%lx\n", hwnd, uMsg, wParam, lParam);

    if (!infoPtr) {
	if (uMsg == WM_CREATE)
	    return COMBOEX_Create (hwnd, (LPCREATESTRUCTA)lParam);
	if (uMsg == WM_NCCREATE)
	    COMBOEX_NCCreate (hwnd);
        return DefWindowProcW (hwnd, uMsg, wParam, lParam);
    }

    switch (uMsg)
    {
        case CBEM_DELETEITEM:
	    return COMBOEX_DeleteItem (infoPtr, wParam);

	case CBEM_GETCOMBOCONTROL:
	    return infoPtr->hwndCombo;

	case CBEM_GETEDITCONTROL:
	    return infoPtr->hwndEdit;

	case CBEM_GETEXTENDEDSTYLE:
	    return infoPtr->dwExtStyle;

	case CBEM_GETIMAGELIST:
	    return (LRESULT)infoPtr->himl;

	case CBEM_GETITEMA:
	    return (LRESULT)COMBOEX_GetItemA (infoPtr, (COMBOBOXEXITEMA *)lParam);

	case CBEM_GETITEMW:
	    return (LRESULT)COMBOEX_GetItemW (infoPtr, (COMBOBOXEXITEMW *)lParam);

	case CBEM_GETUNICODEFORMAT:
	    return infoPtr->unicode;

	case CBEM_HASEDITCHANGED:
	    return COMBOEX_HasEditChanged (infoPtr);

	case CBEM_INSERTITEMA:
	    return COMBOEX_InsertItemA (infoPtr, (COMBOBOXEXITEMA *)lParam);

	case CBEM_INSERTITEMW:
	    return COMBOEX_InsertItemW (infoPtr, (COMBOBOXEXITEMW *)lParam);

	case CBEM_SETEXSTYLE:
	case CBEM_SETEXTENDEDSTYLE:
	    return COMBOEX_SetExtendedStyle (infoPtr, (DWORD)wParam, (DWORD)lParam);

	case CBEM_SETIMAGELIST:
	    return (LRESULT)COMBOEX_SetImageList (infoPtr, (HIMAGELIST)lParam);

	case CBEM_SETITEMA:
	    return COMBOEX_SetItemA (infoPtr, (COMBOBOXEXITEMA *)lParam);

	case CBEM_SETITEMW:
	    return COMBOEX_SetItemW (infoPtr, (COMBOBOXEXITEMW *)lParam);

	case CBEM_SETUNICODEFORMAT:
	    return COMBOEX_SetUnicodeFormat (infoPtr, wParam);

	/* case CBEM_SETWINDOWTHEME: FIXME */

/*   Combo messages we are not sure if we need to process or just forward */
	case CB_GETDROPPEDCONTROLRECT:
	case CB_GETITEMHEIGHT:
	case CB_GETLBTEXT:
	case CB_GETLBTEXTLEN:
	case CB_GETEXTENDEDUI:
	case CB_LIMITTEXT:
	case CB_RESETCONTENT:
	case CB_SELECTSTRING:
	case WM_SETTEXT:
	case WM_GETTEXT:
	    FIXME("(0x%x 0x%x 0x%lx): possibly missing function\n",
		  uMsg, wParam, lParam);

/*   Combo messages OK to just forward to the regular COMBO */
	case CB_GETCOUNT:        
	case CB_GETCURSEL:
	case CB_GETDROPPEDSTATE:
        case CB_SETDROPPEDWIDTH:
        case CB_SETEXTENDEDUI:
        case CB_SHOWDROPDOWN:
	    return COMBOEX_Forward (infoPtr, uMsg, wParam, lParam);

/*   Combo messages we need to process specially */
        case CB_FINDSTRINGEXACT:
	    return COMBOEX_FindStringExact (infoPtr, (INT)wParam, (LPCSTR)lParam);

	case CB_GETITEMDATA:
	    return COMBOEX_GetItemData (infoPtr, (INT)wParam);

	case CB_SETCURSEL:
	    return COMBOEX_SetCursel (infoPtr, (INT)wParam);

	case CB_SETITEMDATA:
	    return COMBOEX_SetItemData (infoPtr, (INT)wParam, (DWORD)lParam);

	case CB_SETITEMHEIGHT:
	    return COMBOEX_SetItemHeight (infoPtr, (INT)wParam, (UINT)lParam);



/*   Window messages passed to parent */
	case WM_COMMAND:
	    return COMBOEX_Command (infoPtr, wParam, lParam);

	case WM_NOTIFY:
	    if (infoPtr->NtfUnicode)
		return SendMessageW (GetParent (hwnd), uMsg, wParam, lParam);
	    else
		return SendMessageA (GetParent (hwnd), uMsg, wParam, lParam);


/*   Window messages we need to process */
        case WM_DELETEITEM:
	    return COMBOEX_WM_DeleteItem (infoPtr, (DELETEITEMSTRUCT *)lParam);

        case WM_DRAWITEM:
            return COMBOEX_DrawItem (infoPtr, (DRAWITEMSTRUCT *)lParam);

	case WM_DESTROY:
	    return COMBOEX_Destroy (infoPtr);

        case WM_MEASUREITEM:
            return COMBOEX_MeasureItem (infoPtr, (MEASUREITEMSTRUCT *)lParam);

        case WM_NOTIFYFORMAT:
	    return COMBOEX_NotifyFormat (infoPtr, lParam);

	case WM_SIZE:
	    return COMBOEX_Size (infoPtr, LOWORD(lParam), HIWORD(lParam));

        case WM_WINDOWPOSCHANGING:
	    return COMBOEX_WindowPosChanging (infoPtr, (WINDOWPOS *)lParam);

	default:
	    if (uMsg >= WM_USER)
		ERR("unknown msg %04x wp=%08x lp=%08lx\n",uMsg,wParam,lParam);
	    return DefWindowProcW (hwnd, uMsg, wParam, lParam);
    }
    return 0;
}


void COMBOEX_Register (void)
{
    WNDCLASSW wndClass;

    ZeroMemory (&wndClass, sizeof(WNDCLASSW));
    wndClass.style         = CS_GLOBALCLASS;
    wndClass.lpfnWndProc   = (WNDPROC)COMBOEX_WindowProc;
    wndClass.cbClsExtra    = 0;
    wndClass.cbWndExtra    = sizeof(COMBOEX_INFO *);
    wndClass.hCursor       = LoadCursorW (0, IDC_ARROWW);
    wndClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wndClass.lpszClassName = WC_COMBOBOXEXW;
 
    RegisterClassW (&wndClass);
}


void COMBOEX_Unregister (void)
{
    UnregisterClassW (WC_COMBOBOXEXW, (HINSTANCE)NULL);
}

