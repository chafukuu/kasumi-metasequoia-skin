// Key tables and preserved keys: the keystroke table, preserved-key setup and registration,
// eligibility and actions, OnPreservedKey, and XPreservedKey.

#include "Private.h"
#include "MetasequoiaIME.h"
#include "CompositionProcessorEngine.h"
#include "TfInputProcessorProfile.h"
#include "Globals.h"
#include "FanyDefines.h"
#include "Compartment.h"
#include "LanguageBar.h"
#include "RegKey.h"
#include "define.h"
#include <msctf.h>
#include <string>
#include <fmt/xchar.h>
#include "Ipc.h"
#include "FanyUtils.h"
#include "FanyLog.h"
#include "EditSession.h"
#include "TfTextLayoutSink.h"
#include <new>

//+---------------------------------------------------------------------------
//
// SetupKeystroke
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupKeystroke()
{
    SetKeystrokeTable(&_KeystrokeComposition);
    return;
}

//+---------------------------------------------------------------------------
//
// SetKeystrokeTable
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetKeystrokeTable(_Inout_ CMetasequoiaImeArray<_KEYSTROKE> *pKeystroke)
{
    for (int i = 0; i < 26; i++)
    {
        _KEYSTROKE *pKS = nullptr;

        pKS = pKeystroke->Append();
        if (!pKS)
        {
            break;
        }
        *pKS = _keystrokeTable[i];
    }
}

//+---------------------------------------------------------------------------
//
// SetupPreserved
// Setup hotkeys
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupPreserved(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    UNREFERENCED_PARAMETER(pThreadMgr);
    UNREFERENCED_PARAMETER(tfClientId);

    TF_PRESERVEDKEY preservedKeyImeMode;
    preservedKeyImeMode.uVKey = VK_SHIFT;
    preservedKeyImeMode.uModifiers = _TF_MOD_ON_KEYUP_SHIFT_ONLY;
    SetPreservedKey(                                  //
        Global::MetasequoiaIMEGuidImeModePreserveKey, //
        preservedKeyImeMode,                          //
        Global::ImeModeDescription,                   //
        &_PreservedKey_IMEMode                        //
    );

    TF_PRESERVEDKEY preservedKeyImeMode02;
    preservedKeyImeMode02.uVKey = VK_SPACE;
    preservedKeyImeMode02.uModifiers = TF_MOD_CONTROL | TF_MOD_ALT;
    SetPreservedKey(                                    //
        Global::MetasequoiaIMEGuidImeModePreserveKey02, //
        preservedKeyImeMode02,                          //
        Global::ImeModeDescription02,                   //
        &_PreservedKey_IMEMode02                        //
    );

    TF_PRESERVEDKEY preservedKeyImeMode03;
    preservedKeyImeMode03.uVKey = VK_CONTROL;
    preservedKeyImeMode03.uModifiers = _TF_MOD_ON_KEYUP_CONTROL_ONLY;
    SetPreservedKey(                                    //
        Global::MetasequoiaIMEGuidImeModePreserveKey03, //
        preservedKeyImeMode03,                          //
        Global::ImeModeDescription03,                   //
        &_PreservedKey_IMEMode03                        //
    );

    TF_PRESERVEDKEY preservedKeyEnglishInputMode;
    preservedKeyEnglishInputMode.uVKey = 'E';
    preservedKeyEnglishInputMode.uModifiers = TF_MOD_CONTROL | TF_MOD_SHIFT;
    SetPreservedKey(                                           //
        Global::MetasequoiaIMEGuidEnglishInputModePreserveKey, //
        preservedKeyEnglishInputMode,                          //
        Global::EnglishInputModeDescription,                   //
        &_PreservedKey_EnglishInputMode                        //
    );

    TF_PRESERVEDKEY preservedKeyDoubleSingleByte;
    preservedKeyDoubleSingleByte.uVKey = VK_SPACE;
    preservedKeyDoubleSingleByte.uModifiers = TF_MOD_SHIFT | TF_MOD_CONTROL;
    SetPreservedKey(                                           //
        Global::MetasequoiaIMEGuidDoubleSingleBytePreserveKey, //
        preservedKeyDoubleSingleByte,                          //
        Global::DoubleSingleByteDescription,                   //
        &_PreservedKey_DoubleSingleByte                        //
    );

    TF_PRESERVEDKEY preservedKeyPunctuation;
    preservedKeyPunctuation.uVKey = VK_OEM_PERIOD;
    preservedKeyPunctuation.uModifiers = TF_MOD_CONTROL;
    SetPreservedKey(                                      //
        Global::MetasequoiaIMEGuidPunctuationPreserveKey, //
        preservedKeyPunctuation,                          //
        Global::PunctuationDescription,                   //
        &_PreservedKey_Punctuation                        //
    );

    /* Keep GUID/action tables for deferred hotkey dispatch. All input-mode
     * shortcuts (including Ctrl+Shift+E) are detected from ITfKeyEventSink. */
    return;
}

//+---------------------------------------------------------------------------
//
// SetKeystrokeTable
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetPreservedKey(const CLSID clsid, TF_PRESERVEDKEY &tfPreservedKey,
                                                  _In_z_ LPCWSTR pwszDescription, _Out_ XPreservedKey *pXPreservedKey)
{
    pXPreservedKey->Guid = clsid;

    TF_PRESERVEDKEY *ptfPsvKey1 = pXPreservedKey->TSFPreservedKeyTable.Append();
    if (!ptfPsvKey1)
    {
        return;
    }
    *ptfPsvKey1 = tfPreservedKey;

    size_t srgKeystrokeBufLen = 0;
    if (StringCchLength(pwszDescription, STRSAFE_MAX_CCH, &srgKeystrokeBufLen) != S_OK)
    {
        return;
    }
    pXPreservedKey->Description = new (std::nothrow) WCHAR[srgKeystrokeBufLen + 1];
    if (!pXPreservedKey->Description)
    {
        return;
    }

    StringCchCopy((LPWSTR)pXPreservedKey->Description, srgKeystrokeBufLen, pwszDescription);

    return;
}
//+---------------------------------------------------------------------------
//
// InitPreservedKey
//
// Register a hotkey.
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::InitPreservedKey(_In_ XPreservedKey *pXPreservedKey, _In_ ITfThreadMgr *pThreadMgr,
                                                   TfClientId tfClientId)
{
    ITfKeystrokeMgr *pKeystrokeMgr = nullptr;
    BOOL registered = TRUE;

    if (IsEqualGUID(pXPreservedKey->Guid, GUID_NULL))
    {
        return FALSE;
    }

    if (pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr) != S_OK)
    {
        return FALSE;
    }

    for (UINT i = 0; i < pXPreservedKey->TSFPreservedKeyTable.Count(); i++)
    {
        TF_PRESERVEDKEY preservedKey = *pXPreservedKey->TSFPreservedKeyTable.GetAt(i);
        preservedKey.uModifiers &= 0xffff;

        size_t lenOfDesc = 0;
        if (StringCchLength(pXPreservedKey->Description, STRSAFE_MAX_CCH, &lenOfDesc) != S_OK)
        {
            return FALSE;
        }
        if (FAILED(pKeystrokeMgr->PreserveKey(tfClientId, pXPreservedKey->Guid, &preservedKey,
                                              pXPreservedKey->Description, static_cast<ULONG>(lenOfDesc))))
        {
            registered = FALSE;
        }
    }

    pKeystrokeMgr->Release();

    return registered;
}

//+---------------------------------------------------------------------------
//
// CheckShiftKeyOnly
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::CheckShiftKeyOnly(_In_ CMetasequoiaImeArray<TF_PRESERVEDKEY> *pTSFPreservedKeyTable)
{
    for (UINT i = 0; i < pTSFPreservedKeyTable->Count(); i++)
    {
        TF_PRESERVEDKEY *ptfPsvKey = pTSFPreservedKeyTable->GetAt(i);

        if (((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_SHIFT_ONLY & 0xffff0000)) && !Global::IsShiftKeyDownOnly) ||
            ((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_CONTROL_ONLY & 0xffff0000)) && !Global::IsControlKeyDownOnly) ||
            ((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_ALT_ONLY & 0xffff0000)) && !Global::IsAltKeyDownOnly))
        {
            return FALSE;
        }
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// OnPreservedKey
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsPreservedKeyEligible(REFGUID rguid)
{
    const FanyUtils::SwitchLanguageHotkeys hotkeys = FanyUtils::ReadConfiguredSwitchLanguageHotkeys();
    if (IsEqualGUID(rguid, _PreservedKey_IMEMode.Guid))
    {
        return hotkeys.shift && CheckShiftKeyOnly(&_PreservedKey_IMEMode.TSFPreservedKeyTable);
    }
    if (IsEqualGUID(rguid, _PreservedKey_IMEMode02.Guid))
    {
        return hotkeys.ctrl_alt_space && CheckShiftKeyOnly(&_PreservedKey_IMEMode02.TSFPreservedKeyTable);
    }
    if (IsEqualGUID(rguid, _PreservedKey_IMEMode03.Guid))
    {
        return hotkeys.ctrl && CheckShiftKeyOnly(&_PreservedKey_IMEMode03.TSFPreservedKeyTable);
    }
    if (IsEqualGUID(rguid, _PreservedKey_EnglishInputMode.Guid))
    {
        return CheckShiftKeyOnly(&_PreservedKey_EnglishInputMode.TSFPreservedKeyTable);
    }
    if (IsEqualGUID(rguid, _PreservedKey_DoubleSingleByte.Guid))
    {
        return CheckShiftKeyOnly(&_PreservedKey_DoubleSingleByte.TSFPreservedKeyTable);
    }
    if (IsEqualGUID(rguid, _PreservedKey_Punctuation.Guid))
    {
        return CheckShiftKeyOnly(&_PreservedKey_Punctuation.TSFPreservedKeyTable);
    }
    return FALSE;
}

CCompositionProcessorEngine::PreservedKeyAction CCompositionProcessorEngine::GetPreservedKeyAction(REFGUID rguid) const
{
    if (IsEqualGUID(rguid, _PreservedKey_IMEMode.Guid) || IsEqualGUID(rguid, _PreservedKey_IMEMode02.Guid) ||
        IsEqualGUID(rguid, _PreservedKey_IMEMode03.Guid))
    {
        return PreservedKeyAction::ToggleImeMode;
    }
    if (IsEqualGUID(rguid, _PreservedKey_DoubleSingleByte.Guid))
    {
        return PreservedKeyAction::ToggleDoubleSingleByteMode;
    }
    if (IsEqualGUID(rguid, _PreservedKey_Punctuation.Guid))
    {
        return PreservedKeyAction::TogglePunctuationMode;
    }
    return PreservedKeyAction::None;
}

void CCompositionProcessorEngine::OnPreservedKey( //
    ITfContext * /*pContext*/,                    //
    REFGUID rguid,                                //
    _Out_ BOOL *pIsEaten,                         //
    _In_ ITfThreadMgr *pThreadMgr,                //
    TfClientId tfClientId,                        //
    BOOL *pNeedToggleIMEMode,                     //
    BOOL isPrevalidated,                          //
    BOOL notifyServer                             //
)
{
    if (IsEqualGUID(rguid, _PreservedKey_IMEMode.Guid) || IsEqualGUID(rguid, _PreservedKey_IMEMode02.Guid) ||
        IsEqualGUID(rguid, _PreservedKey_IMEMode03.Guid))
    {
        if (!isPrevalidated)
        {
            CMetasequoiaImeArray<TF_PRESERVEDKEY> *table = &_PreservedKey_IMEMode.TSFPreservedKeyTable;
            if (IsEqualGUID(rguid, _PreservedKey_IMEMode02.Guid))
            {
                table = &_PreservedKey_IMEMode02.TSFPreservedKeyTable;
            }
            else if (IsEqualGUID(rguid, _PreservedKey_IMEMode03.Guid))
            {
                table = &_PreservedKey_IMEMode03.TSFPreservedKeyTable;
            }
            if (!CheckShiftKeyOnly(table) || !IsPreservedKeyEligible(rguid))
            {
                *pIsEaten = FALSE;
                return;
            }
        }
        BOOL isOpen = FALSE;
        CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
        CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen);
        isOpen = isOpen ? FALSE : TRUE;
        ReleaseConfiguredImeModeDefense();
        FanyUtils::RefreshPunctuationLockFromConfig();

        // Closing CN→EN while composing: keep KEYBOARD_OPENCLOSE open until
        // after EndComposition. CUAS/Win32 EDIT double-commits if we close
        // first and finalize later (Ctrl+Space / Chrome are fine).
        const BOOL deferCloseUntilCompositionCommit = !isOpen && _pTextService && _pTextService->_IsComposing();
        if (deferCloseUntilCompositionCommit)
        {
            _pendingImeModeAfterCompositionCommit = isOpen;
            _hasPendingImeModeAfterCompositionCommit = TRUE;
        }
        else
        {
            _hasPendingImeModeAfterCompositionCommit = FALSE;
            SetKeyboardOpenCompartment(pThreadMgr, tfClientId, isOpen);
            SyncPunctuationWithImeMode(pThreadMgr, tfClientId, isOpen);
        }
        // Announce the mode the user chose, even while a deferred close is
        // still waiting for the composition to commit.
        SendCaretStateSwitchEvent(FanyImePipeEventType::IMESwitch, isOpen != FALSE);

        *pIsEaten = TRUE;
        *pNeedToggleIMEMode = TRUE;

        Global::Keycode = VK_SHIFT;
        if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
            Global::ModifiersDown |= 0b00000001;
        else
            Global::ModifiersDown &= ~0b00000001;
        if (notifyServer)
        {
            WriteDataToSharedMemory(Global::Keycode, L'\0', Global::ModifiersDown, nullptr, 0, L"", 0b000111);
            SendKeyEventToUIProcess();
        }
    }
    else if (IsEqualGUID(rguid, _PreservedKey_DoubleSingleByte.Guid))
    {
        if (!isPrevalidated && !CheckShiftKeyOnly(&_PreservedKey_DoubleSingleByte.TSFPreservedKeyTable))
        {
            *pIsEaten = FALSE;
            return;
        }
        BOOL isDouble = FALSE;
        CCompartment CompartmentDoubleSingleByte(pThreadMgr, tfClientId,
                                                 Global::MetasequoiaIMEGuidCompartmentDoubleSingleByte);
        CompartmentDoubleSingleByte._GetCompartmentBOOL(isDouble);
        CompartmentDoubleSingleByte._SetCompartmentBOOL(isDouble ? FALSE : TRUE);
        SendCaretStateSwitchEvent(FanyImePipeEventType::DoubleSingleByteSwitch, isDouble == FALSE);
        *pIsEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, _PreservedKey_Punctuation.Guid))
    {
        if (!isPrevalidated && !CheckShiftKeyOnly(&_PreservedKey_Punctuation.TSFPreservedKeyTable))
        {
            *pIsEaten = FALSE;
            return;
        }
        // Ctrl + .: toggle Chinese/English punctuation
        BOOL isPunctuation = FALSE;
        CCompartment CompartmentPunctuation(pThreadMgr, tfClientId, Global::MetasequoiaIMEGuidCompartmentPunctuation);
        CompartmentPunctuation._GetCompartmentBOOL(isPunctuation);
        SetPunctuationMode(pThreadMgr, tfClientId, isPunctuation ? FALSE : TRUE);
        // A configured punctuation lock can reject the toggle; stay silent then.
        const BOOL punctuationNow = GetPunctuationMode(pThreadMgr, tfClientId);
        if (punctuationNow != isPunctuation)
        {
            SendCaretStateSwitchEvent(FanyImePipeEventType::PuncSwitch, punctuationNow != FALSE);
        }
        *pIsEaten = TRUE;
    }
    else
    {
        *pIsEaten = FALSE;
    }
    *pIsEaten = TRUE;
}

//////////////////////////////////////////////////////////////////////
//
// XPreservedKey implementation.
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// UninitPreservedKey
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::XPreservedKey::UninitPreservedKey(_In_ ITfThreadMgr *pThreadMgr)
{
    ITfKeystrokeMgr *pKeystrokeMgr = nullptr;

    if (IsEqualGUID(Guid, GUID_NULL))
    {
        return FALSE;
    }

    if (FAILED(pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr)))
    {
        return FALSE;
    }

    for (UINT i = 0; i < TSFPreservedKeyTable.Count(); i++)
    {
        TF_PRESERVEDKEY pPreservedKey = *TSFPreservedKeyTable.GetAt(i);
        pPreservedKey.uModifiers &= 0xffff;

        pKeystrokeMgr->UnpreserveKey(Guid, &pPreservedKey);
    }

    pKeystrokeMgr->Release();

    return TRUE;
}

CCompositionProcessorEngine::XPreservedKey::XPreservedKey()
{
    Guid = GUID_NULL;
    Description = nullptr;
}

CCompositionProcessorEngine::XPreservedKey::~XPreservedKey()
{
    ITfThreadMgr *pThreadMgr = nullptr;

    HRESULT hr =
        CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void **)&pThreadMgr);
    if (SUCCEEDED(hr))
    {
        UninitPreservedKey(pThreadMgr);
        pThreadMgr->Release();
        pThreadMgr = nullptr;
    }

    if (Description)
    {
        delete[] Description;
    }
}

void CCompositionProcessorEngine::InitKeyStrokeTable()
{
    for (int i = 0; i < 26; i++)
    {
        _keystrokeTable[i].VirtualKey = 'A' + i;
        _keystrokeTable[i].Modifiers = 0;
        _keystrokeTable[i].Function = FUNCTION_INPUT;
    }
}
